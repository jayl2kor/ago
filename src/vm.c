#include "runtime.h"
#include "chunk.h"
#include "vm.h"
#include "compiler.h"
#include "parser.h"
#include "sema.h"
#include "builtins_core.h"

/* ---- VM state ---- */

#define VM_STACK_MAX 1024
#define VM_FRAMES_MAX 512

typedef struct {
    AglFnVal *fn;
    uint8_t *ip;
    int stack_base;
    int env_saved_count;
    bool is_closure;
    AglEnv *saved_env;      /* arena-allocated, for closures */
} VmFrame;

typedef struct {
    AglVal stack[VM_STACK_MAX];
    int stack_top;

    VmFrame frames[VM_FRAMES_MAX];
    int frame_count;

    AglEnv env;
    AglCtx *ctx;
    AglArena *arena;
    AglGc *gc;
    const char *file;

    int current_line;

    AglModule modules[MAX_MODULES];
    int module_count;
} Vm;

/* ---- Stack operations ---- */

static void vm_push(Vm *vm, AglVal val) {
    if (vm->stack_top >= VM_STACK_MAX) {
        agl_error_set(vm->ctx, AGL_ERR_RUNTIME,
                      agl_loc(NULL, vm->current_line, 0), "stack overflow");
        return;
    }
    vm->stack[vm->stack_top++] = val;
}

static AglVal vm_pop(Vm *vm) {
    if (vm->stack_top <= 0) {
        agl_error_set(vm->ctx, AGL_ERR_RUNTIME,
                      agl_loc(NULL, vm->current_line, 0), "stack underflow");
        return val_nil();
    }
    return vm->stack[--vm->stack_top];
}

static AglVal vm_peek(Vm *vm, int distance) {
    return vm->stack[vm->stack_top - 1 - distance];
}

/* ---- Helpers ---- */

static inline uint16_t read_u16(uint8_t *ip) {
    return (uint16_t)(ip[0] | (ip[1] << 8));
}

/* ---- GC root marking for VM ---- */

static void vm_gc_collect(Vm *vm) {
    /* Mark value stack */
    for (int i = 0; i < vm->stack_top; i++) {
        mark_val(vm->stack[i]);
    }
    /* Mark environment */
    for (int i = 0; i < vm->env.count; i++) {
        mark_val(vm->env.values[i]);
    }
    /* Mark call frame functions and saved envs */
    for (int i = 0; i < vm->frame_count; i++) {
        if (vm->frames[i].fn) {
            agl_gc_mark(&vm->frames[i].fn->obj);
        }
        if (vm->frames[i].saved_env) {
            for (int j = 0; j < vm->frames[i].saved_env->count; j++) {
                mark_val(vm->frames[i].saved_env->values[j]);
            }
        }
    }
    agl_gc_sweep(vm->gc);
}

/* Forward declarations */
static AglVal call_fn_direct_vm(Vm *vm, AglFnVal *fn, AglVal *args, int argc,
                                int line, int col);
static int vm_execute(Vm *vm, AglChunk *chunk);

/* ---- Trace capture for VM errors ---- */

static void vm_capture_trace(void *data, AglError *err) {
    Vm *vm = data;
    int n = vm->frame_count;
    if (n > AGL_MAX_TRACE) n = AGL_MAX_TRACE;
    err->trace_count = n;
    for (int i = 0; i < n; i++) {
        int src_idx = vm->frame_count - 1 - i;
        VmFrame *f = &vm->frames[src_idx];
        AglTraceFrame *t = &err->trace[i];
        t->line = vm->current_line;
        t->column = 0;
        t->name[0] = '\0';
        if (f->fn && f->fn->decl) {
            const char *fname = f->fn->decl->as.fn_decl.name;
            int flen = f->fn->decl->as.fn_decl.name_length;
            if (fname && flen > 0) {
                if (flen >= (int)sizeof(t->name)) flen = (int)sizeof(t->name) - 1;
                memcpy(t->name, fname, (size_t)flen);
                t->name[flen] = '\0';
            }
        }
    }
}

/* ---- VM callback for map/filter ---- */

static AglVal vm_call_fn_cb(void *caller_data, AglFnVal *fn,
                             AglVal *args, int argc, int line, int col) {
    Vm *vm = caller_data;
    return call_fn_direct_vm(vm, fn, args, argc, line, col);
}

/* ---- Builtin dispatch (pre-evaluated args) ---- */

static AglVal vm_call_builtin(Vm *vm, int builtin_id, AglVal *args, int argc) {
    AglBuiltinCtx bctx;
    bctx.arena = vm->arena;
    bctx.gc = vm->gc;
    bctx.ctx = vm->ctx;
    bctx.line = vm->current_line;
    bctx.col = 0;
    bctx.call_fn = vm_call_fn_cb;
    bctx.caller_data = vm;
    return agl_builtin_dispatch(builtin_id, args, argc, &bctx);
}


static AglVal call_fn_direct_vm(Vm *vm, AglFnVal *fn, AglVal *args, int argc,
                                int line, int col) {
    /* For now, delegate to the tree-walk call_fn_direct through a thin wrapper.
     * This requires constructing a temporary AglInterp. We'll use the VM's env. */
    if (!fn->chunk) {
        /* Tree-walk fallback — should not happen in full VM mode */
        agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col),
                      "cannot call non-compiled function in VM");
        return val_nil();
    }

    /* VM-native function call */
    if (fn->arity != argc) {
        agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col),
                      "expected %d arguments, got %d", fn->arity, argc);
        return val_nil();
    }

    if (vm->frame_count >= VM_FRAMES_MAX) {
        agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col),
                      "maximum call depth exceeded (limit %d)", VM_FRAMES_MAX);
        return val_nil();
    }

    /* Save current env for closure */
    AglEnv *saved_env = NULL;
    int saved_count = vm->env.count;
    if (fn->captured_count > 0) {
        saved_env = agl_arena_alloc(vm->arena, sizeof(AglEnv));
        if (!saved_env) {
            agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, line, col), "out of memory");
            return val_nil();
        }
        *saved_env = vm->env;
        env_init(&vm->env);
        for (int i = 0; i < fn->captured_count; i++) {
            env_define(&vm->env, fn->captured_names[i], fn->captured_name_lengths[i],
                       fn->captured_values[i], fn->captured_immutable[i]);
        }
    }

    /* Define parameters */
    for (int i = 0; i < argc; i++) {
        env_define(&vm->env, fn->decl->as.fn_decl.param_names[i],
                   fn->decl->as.fn_decl.param_name_lengths[i],
                   args[i], true);
    }

    /* Push frame */
    VmFrame *frame = &vm->frames[vm->frame_count++];
    frame->fn = fn;
    frame->ip = fn->chunk->code;
    frame->stack_base = vm->stack_top;
    frame->env_saved_count = saved_count;
    frame->is_closure = (saved_env != NULL);
    frame->saved_env = saved_env;

    /* Execute the function's bytecode using full dispatch */
    int base_frame = vm->frame_count;
    int rc = vm_execute(vm, fn->chunk);

    /* Get return value (pushed by OP_RETURN or default nil) */
    AglVal result = val_nil();
    if (rc == 0 && vm->stack_top > frame->stack_base) {
        result = vm_pop(vm);
    }

    /* Restore state (in case OP_RETURN didn't clean up properly) */
    if (vm->frame_count >= base_frame) {
        vm->frame_count = base_frame - 1;
    }
    vm->stack_top = frame->stack_base;
    if (saved_env) {
        vm->env = *saved_env;
    } else {
        vm->env.count = saved_count;
    }

    if (rc != 0 && !agl_error_occurred(vm->ctx)) {
        return val_nil();
    }

    return result;
}

/* ---- Main execution loop ---- */

static int vm_execute(Vm *vm, AglChunk *chunk) {
    uint8_t *ip = chunk->code;
    uint8_t *end = chunk->code + chunk->code_count;

    while (ip < end) {
        if (agl_error_occurred(vm->ctx)) return -1;

        uint8_t op = *ip++;

        switch (op) {

        case AGL_OP_CONST: {
            uint16_t idx = read_u16(ip); ip += 2;
            vm_push(vm, chunk->constants[idx]);
            break;
        }

        case AGL_OP_NIL:   vm_push(vm, val_nil()); break;
        case AGL_OP_TRUE:  vm_push(vm, val_bool(true)); break;
        case AGL_OP_FALSE: vm_push(vm, val_bool(false)); break;

        case AGL_OP_ADD: {
            AglVal b = vm_pop(vm), a = vm_pop(vm);
            if (a.kind == VAL_INT && b.kind == VAL_INT) { vm_push(vm, val_int(a.as.integer + b.as.integer)); break; }
            if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) { vm_push(vm, val_float(a.as.floating + b.as.floating)); break; }
            if ((a.kind == VAL_INT && b.kind == VAL_FLOAT) || (a.kind == VAL_FLOAT && b.kind == VAL_INT)) {
                double la = a.kind == VAL_FLOAT ? a.as.floating : (double)a.as.integer;
                double lb = b.kind == VAL_FLOAT ? b.as.floating : (double)b.as.integer;
                vm_push(vm, val_float(la + lb)); break;
            }
            if (a.kind == VAL_STRING && b.kind == VAL_STRING) {
                int llen, rlen; const char *ld = str_content(a, &llen); const char *rd = str_content(b, &rlen);
                int total = llen + rlen;
                char *buf = agl_arena_alloc(vm->arena, (size_t)total);
                if (!buf) { agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
                memcpy(buf, ld, (size_t)llen); memcpy(buf + llen, rd, (size_t)rlen);
                vm_push(vm, val_string(buf, total)); break;
            }
            agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "invalid binary operation");
            return -1;
        }

        case AGL_OP_SUB: {
            AglVal b = vm_pop(vm), a = vm_pop(vm);
            if (a.kind == VAL_INT && b.kind == VAL_INT) { vm_push(vm, val_int(a.as.integer - b.as.integer)); break; }
            if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) { vm_push(vm, val_float(a.as.floating - b.as.floating)); break; }
            if ((a.kind == VAL_INT && b.kind == VAL_FLOAT) || (a.kind == VAL_FLOAT && b.kind == VAL_INT)) {
                double la = a.kind == VAL_FLOAT ? a.as.floating : (double)a.as.integer;
                double lb = b.kind == VAL_FLOAT ? b.as.floating : (double)b.as.integer;
                vm_push(vm, val_float(la - lb)); break;
            }
            agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "invalid binary operation"); return -1;
        }

        case AGL_OP_MUL: {
            AglVal b = vm_pop(vm), a = vm_pop(vm);
            if (a.kind == VAL_INT && b.kind == VAL_INT) { vm_push(vm, val_int(a.as.integer * b.as.integer)); break; }
            if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) { vm_push(vm, val_float(a.as.floating * b.as.floating)); break; }
            if ((a.kind == VAL_INT && b.kind == VAL_FLOAT) || (a.kind == VAL_FLOAT && b.kind == VAL_INT)) {
                double la = a.kind == VAL_FLOAT ? a.as.floating : (double)a.as.integer;
                double lb = b.kind == VAL_FLOAT ? b.as.floating : (double)b.as.integer;
                vm_push(vm, val_float(la * lb)); break;
            }
            agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "invalid binary operation"); return -1;
        }

        case AGL_OP_DIV: {
            AglVal b = vm_pop(vm), a = vm_pop(vm);
            if (a.kind == VAL_INT && b.kind == VAL_INT) {
                if (b.as.integer == 0) { agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "division by zero"); return -1; }
                vm_push(vm, val_int(a.as.integer / b.as.integer)); break;
            }
            if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) { vm_push(vm, val_float(a.as.floating / b.as.floating)); break; }
            if ((a.kind == VAL_INT && b.kind == VAL_FLOAT) || (a.kind == VAL_FLOAT && b.kind == VAL_INT)) {
                double la = a.kind == VAL_FLOAT ? a.as.floating : (double)a.as.integer;
                double lb = b.kind == VAL_FLOAT ? b.as.floating : (double)b.as.integer;
                vm_push(vm, val_float(la / lb)); break;
            }
            agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "invalid binary operation"); return -1;
        }

        case AGL_OP_MOD: {
            AglVal b = vm_pop(vm), a = vm_pop(vm);
            if (a.kind == VAL_INT && b.kind == VAL_INT) {
                if (b.as.integer == 0) { agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "division by zero"); return -1; }
                vm_push(vm, val_int(a.as.integer % b.as.integer)); break;
            }
            agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "invalid binary operation"); return -1;
        }

        case AGL_OP_NEGATE: {
            AglVal a = vm_pop(vm);
            if (a.kind == VAL_INT) { vm_push(vm, val_int(-a.as.integer)); break; }
            if (a.kind == VAL_FLOAT) { vm_push(vm, val_float(-a.as.floating)); break; }
            agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "invalid unary operator"); return -1;
        }

        case AGL_OP_NOT: {
            AglVal a = vm_pop(vm);
            if (a.kind == VAL_BOOL) { vm_push(vm, val_bool(!a.as.boolean)); break; }
            agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "invalid unary operator"); return -1;
        }

        /* Comparison ops */
        case AGL_OP_EQ: case AGL_OP_NEQ:
        case AGL_OP_LT: case AGL_OP_GT:
        case AGL_OP_LE: case AGL_OP_GE: {
            AglVal b = vm_pop(vm), a = vm_pop(vm);
            bool result = false;
            if (a.kind == VAL_INT && b.kind == VAL_INT) {
                int64_t l = a.as.integer, r = b.as.integer;
                switch (op) {
                case AGL_OP_EQ: result = l == r; break; case AGL_OP_NEQ: result = l != r; break;
                case AGL_OP_LT: result = l < r; break; case AGL_OP_GT: result = l > r; break;
                case AGL_OP_LE: result = l <= r; break; case AGL_OP_GE: result = l >= r; break;
                default: break;
                }
            } else if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) {
                double l = a.as.floating, r = b.as.floating;
                switch (op) {
                case AGL_OP_EQ: result = l == r; break; case AGL_OP_NEQ: result = l != r; break;
                case AGL_OP_LT: result = l < r; break; case AGL_OP_GT: result = l > r; break;
                case AGL_OP_LE: result = l <= r; break; case AGL_OP_GE: result = l >= r; break;
                default: break;
                }
            } else if ((a.kind == VAL_INT && b.kind == VAL_FLOAT) || (a.kind == VAL_FLOAT && b.kind == VAL_INT)) {
                double l = a.kind == VAL_FLOAT ? a.as.floating : (double)a.as.integer;
                double r = b.kind == VAL_FLOAT ? b.as.floating : (double)b.as.integer;
                switch (op) {
                case AGL_OP_EQ: result = l == r; break; case AGL_OP_NEQ: result = l != r; break;
                case AGL_OP_LT: result = l < r; break; case AGL_OP_GT: result = l > r; break;
                case AGL_OP_LE: result = l <= r; break; case AGL_OP_GE: result = l >= r; break;
                default: break;
                }
            } else if (a.kind == VAL_BOOL && b.kind == VAL_BOOL) {
                switch (op) {
                case AGL_OP_EQ: result = a.as.boolean == b.as.boolean; break;
                case AGL_OP_NEQ: result = a.as.boolean != b.as.boolean; break;
                default: agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "invalid binary operation"); return -1;
                }
            } else if (a.kind == VAL_STRING && b.kind == VAL_STRING) {
                int llen, rlen; const char *ld = str_content(a, &llen); const char *rd = str_content(b, &rlen);
                switch (op) {
                case AGL_OP_EQ: result = llen == rlen && memcmp(ld, rd, (size_t)llen) == 0; break;
                case AGL_OP_NEQ: result = llen != rlen || memcmp(ld, rd, (size_t)llen) != 0; break;
                case AGL_OP_LT: case AGL_OP_GT: case AGL_OP_LE: case AGL_OP_GE: {
                    int minlen = llen < rlen ? llen : rlen;
                    int cmp = memcmp(ld, rd, (size_t)minlen);
                    if (cmp == 0) cmp = (llen > rlen) - (llen < rlen);
                    if (op == AGL_OP_LT) result = cmp < 0;
                    else if (op == AGL_OP_GT) result = cmp > 0;
                    else if (op == AGL_OP_LE) result = cmp <= 0;
                    else result = cmp >= 0;
                    break;
                }
                default: break;
                }
            } else {
                agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "invalid binary operation"); return -1;
            }
            vm_push(vm, val_bool(result));
            break;
        }

        /* Variables */
        case AGL_OP_DEFINE_LET: case AGL_OP_DEFINE_VAR: {
            uint16_t idx = read_u16(ip); ip += 2;
            AglVal val = vm_pop(vm);
            AglVal name = chunk->constants[idx];
            bool immut = (op == AGL_OP_DEFINE_LET);
            if (!env_define(&vm->env, name.as.string.data, name.as.string.length, val, immut)) {
                agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "too many variables (max %d)", MAX_VARS);
                return -1;
            }
            break;
        }

        case AGL_OP_GET_VAR: {
            uint16_t idx = read_u16(ip); ip += 2;
            AglVal name = chunk->constants[idx];
            AglVal *v = env_get(&vm->env, name.as.string.data, name.as.string.length);
            if (!v) {
                agl_error_set(vm->ctx, AGL_ERR_NAME, agl_loc(NULL, vm->current_line, 0),
                              "undefined variable '%.*s'", name.as.string.length, name.as.string.data);
                return -1;
            }
            vm_push(vm, *v);
            break;
        }

        case AGL_OP_SET_VAR: {
            uint16_t idx = read_u16(ip); ip += 2;
            AglVal val = vm_pop(vm);
            AglVal name = chunk->constants[idx];
            int rc = env_assign(&vm->env, name.as.string.data, name.as.string.length, val);
            if (rc == 1) { agl_error_set(vm->ctx, AGL_ERR_NAME, agl_loc(NULL, vm->current_line, 0), "undefined variable '%.*s'", name.as.string.length, name.as.string.data); return -1; }
            if (rc == 2) { agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "cannot assign to immutable variable '%.*s'", name.as.string.length, name.as.string.data); return -1; }
            break;
        }

        case AGL_OP_POP: vm_pop(vm); break;

        case AGL_OP_POP_SCOPE: {
            uint8_t count = *ip++;
            vm->env.count -= count;
            break;
        }

        /* Control flow */
        case AGL_OP_JUMP: { uint16_t offset = read_u16(ip); ip += 2; ip += offset; break; }
        case AGL_OP_JUMP_BACK: { uint16_t offset = read_u16(ip); ip += 2; ip -= offset; break; }
        case AGL_OP_JUMP_IF_FALSE: {
            uint16_t offset = read_u16(ip); ip += 2;
            AglVal cond = vm_pop(vm);
            if (!is_truthy(cond)) ip += offset;
            break;
        }
        case AGL_OP_JUMP_IF_TRUE: {
            uint16_t offset = read_u16(ip); ip += 2;
            AglVal cond = vm_pop(vm);
            if (is_truthy(cond)) ip += offset;
            break;
        }

        /* Functions */
        case AGL_OP_CLOSURE: {
            uint16_t idx = read_u16(ip); ip += 2;
            AglVal fn_template = chunk->constants[idx];
            /* Create a closure: copy the function and capture current env */
            AglFnVal *orig = fn_template.as.fn;
            AglFnVal *fn = agl_gc_alloc(vm->gc, sizeof(AglFnVal), fn_cleanup);
            if (!fn) { agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
            fn->decl = orig->decl;
            fn->chunk = orig->chunk;
            fn->arity = orig->arity;
            /* Capture current environment */
            fn->captured_count = vm->env.count;
            fn->captured_names = NULL; fn->captured_name_lengths = NULL;
            fn->captured_values = NULL; fn->captured_immutable = NULL;
            if (fn->captured_count > 0) {
                size_t n = (size_t)fn->captured_count;
                fn->captured_names = malloc(sizeof(char *) * n);
                fn->captured_name_lengths = malloc(sizeof(int) * n);
                fn->captured_values = malloc(sizeof(AglVal) * n);
                fn->captured_immutable = malloc(sizeof(bool) * n);
                if (!fn->captured_names || !fn->captured_name_lengths ||
                    !fn->captured_values || !fn->captured_immutable) {
                    agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "out of memory");
                    return -1;
                }
                memcpy(fn->captured_names, vm->env.names, sizeof(char *) * n);
                memcpy(fn->captured_name_lengths, vm->env.name_lengths, sizeof(int) * n);
                memcpy(fn->captured_values, vm->env.values, sizeof(AglVal) * n);
                memcpy(fn->captured_immutable, vm->env.immutable, sizeof(bool) * n);
            }
            AglVal fv; fv.kind = VAL_FN; fv.as.fn = fn;
            vm_push(vm, fv);
            break;
        }

        case AGL_OP_CALL: {
            uint8_t argc = *ip++;
            AglVal callee = vm->stack[vm->stack_top - 1 - argc];
            if (callee.kind != VAL_FN) {
                agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "expression is not callable");
                return -1;
            }
            AglFnVal *fn = callee.as.fn;
            if (!fn->chunk) {
                agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "cannot call non-compiled function");
                return -1;
            }
            if (fn->arity != argc) {
                agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0),
                              "expected %d arguments, got %d", fn->arity, argc);
                return -1;
            }
            /* Collect args from stack */
            AglVal args[64];
            for (int i = 0; i < argc; i++) {
                args[i] = vm->stack[vm->stack_top - argc + i];
            }
            /* Pop callee + args */
            vm->stack_top -= (argc + 1);

            AglVal result = call_fn_direct_vm(vm, fn, args, argc, vm->current_line, 0);
            if (agl_error_occurred(vm->ctx)) return -1;
            vm_push(vm, result);
            break;
        }

        case AGL_OP_RETURN: {
            /* Return value is already on stack (TOS) — leave it there for caller */
            return 0;
        }
        case AGL_OP_RETURN_NIL:
            vm_push(vm, val_nil());
            return 0;

        /* Builtins */
        case AGL_OP_CALL_BUILTIN: {
            uint16_t bid = read_u16(ip); ip += 2;
            uint8_t argc = *ip++;
            AglVal args[64];
            for (int i = argc - 1; i >= 0; i--) {
                args[i] = vm_pop(vm);
            }
            AglVal result = vm_call_builtin(vm, bid, args, argc);
            if (agl_error_occurred(vm->ctx)) return -1;
            vm_push(vm, result);
            break;
        }

        /* Compound types */
        case AGL_OP_ARRAY: {
            uint16_t count = read_u16(ip); ip += 2;
            AglArrayVal *arr = agl_gc_alloc(vm->gc, sizeof(AglArrayVal), array_cleanup);
            if (!arr) { agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
            arr->count = count;
            arr->elements = count > 0 ? malloc(sizeof(AglVal) * count) : NULL;
            for (int i = count - 1; i >= 0; i--) { arr->elements[i] = vm_pop(vm); }
            AglVal v; v.kind = VAL_ARRAY; v.as.array = arr;
            vm_push(vm, v);
            break;
        }

        case AGL_OP_INDEX: {
            AglVal idx_val = vm_pop(vm);
            AglVal obj = vm_pop(vm);
            if (obj.kind == VAL_MAP) {
                if (idx_val.kind != VAL_STRING) { agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "map key must be a string"); return -1; }
                int klen; const char *kdata = str_content(idx_val, &klen);
                AglMapVal *m = obj.as.map;
                bool found = false;
                for (int i = 0; i < m->count; i++) {
                    if (m->key_lengths[i] == klen && memcmp(m->keys[i], kdata, (size_t)klen) == 0) {
                        vm_push(vm, m->values[i]);
                        found = true;
                        break;
                    }
                }
                if (!found) { vm_push(vm, val_nil()); }
                break;
            }
            if (obj.kind != VAL_ARRAY) { agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "cannot index non-array value"); return -1; }
            if (idx_val.kind != VAL_INT) { agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "array index must be an integer"); return -1; }
            int i = (int)idx_val.as.integer;
            if (i < 0 || i >= obj.as.array->count) { agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "index %d out of bounds (length %d)", i, obj.as.array->count); return -1; }
            vm_push(vm, obj.as.array->elements[i]);
            break;
        }

        case AGL_OP_STRUCT: {
            uint16_t type_idx = read_u16(ip); ip += 2;
            uint8_t field_count = *ip++;
            AglStructVal *s = agl_gc_alloc(vm->gc, sizeof(AglStructVal), NULL);
            if (!s) { agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
            AglVal type_name = chunk->constants[type_idx];
            s->type_name = type_name.as.string.data;
            s->type_name_length = type_name.as.string.length;
            s->field_count = field_count;
            /* Pop name+value pairs (pushed as: name0, val0, name1, val1, ...) */
            for (int i = field_count - 1; i >= 0; i--) {
                s->field_values[i] = vm_pop(vm);
                AglVal fname = vm_pop(vm);
                s->field_names[i] = fname.as.string.data;
                s->field_name_lengths[i] = fname.as.string.length;
            }
            AglVal v; v.kind = VAL_STRUCT; v.as.strct = s;
            vm_push(vm, v);
            break;
        }

        case AGL_OP_GET_FIELD: {
            uint16_t idx = read_u16(ip); ip += 2;
            AglVal obj = vm_pop(vm);
            if (obj.kind != VAL_STRUCT) { agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "cannot access field on non-struct value"); return -1; }
            AglVal fname = chunk->constants[idx];
            AglStructVal *s = obj.as.strct;
            bool found = false;
            for (int i = 0; i < s->field_count; i++) {
                if (agl_str_eq(s->field_names[i], s->field_name_lengths[i],
                               fname.as.string.data, fname.as.string.length)) {
                    vm_push(vm, s->field_values[i]);
                    found = true;
                    break;
                }
            }
            if (!found) {
                agl_error_set(vm->ctx, AGL_ERR_NAME, agl_loc(NULL, vm->current_line, 0),
                              "no field '%.*s'", fname.as.string.length, fname.as.string.data);
                return -1;
            }
            break;
        }

        case AGL_OP_MAP: {
            uint16_t count = read_u16(ip); ip += 2;
            AglMapVal *m = agl_gc_alloc(vm->gc, sizeof(AglMapVal), map_cleanup);
            if (!m) { agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
            m->count = count;
            m->capacity = count > 0 ? count : 0;
            m->keys = count > 0 ? malloc(sizeof(char *) * count) : NULL;
            m->key_lengths = count > 0 ? malloc(sizeof(int) * count) : NULL;
            m->values = count > 0 ? malloc(sizeof(AglVal) * count) : NULL;
            /* Stack has: key0, val0, key1, val1, ... (pushed in order) */
            /* Pop in reverse: valN-1, keyN-1, ..., val0, key0 */
            for (int i = count - 1; i >= 0; i--) {
                m->values[i] = vm_pop(vm);
                AglVal key = vm_pop(vm);
                int klen; const char *kdata = str_content(key, &klen);
                m->keys[i] = kdata;
                m->key_lengths[i] = klen;
            }
            AglVal v; v.kind = VAL_MAP; v.as.map = m;
            vm_push(vm, v);
            break;
        }

        case AGL_OP_RESULT_OK: {
            AglVal inner = vm_pop(vm);
            AglResultVal *rv = agl_gc_alloc(vm->gc, sizeof(AglResultVal), NULL);
            if (!rv) { agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
            rv->is_ok = true; rv->value = inner;
            AglVal v; v.kind = VAL_RESULT; v.as.result = rv;
            vm_push(vm, v);
            break;
        }

        case AGL_OP_RESULT_ERR: {
            AglVal inner = vm_pop(vm);
            AglResultVal *rv = agl_gc_alloc(vm->gc, sizeof(AglResultVal), NULL);
            if (!rv) { agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0), "out of memory"); return -1; }
            rv->is_ok = false; rv->value = inner;
            AglVal v; v.kind = VAL_RESULT; v.as.result = rv;
            vm_push(vm, v);
            break;
        }

        case AGL_OP_MATCH: {
            uint16_t ok_name_idx = read_u16(ip); ip += 2;
            uint16_t err_name_idx = read_u16(ip); ip += 2;
            uint16_t ok_offset = read_u16(ip); ip += 2;
            uint16_t err_offset = read_u16(ip); ip += 2;
            AglVal subject = vm_pop(vm);
            if (subject.kind != VAL_RESULT) {
                agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "match requires a result value");
                return -1;
            }
            uint8_t *base = ip;
            if (subject.as.result->is_ok) {
                AglVal name = chunk->constants[ok_name_idx];
                env_define(&vm->env, name.as.string.data, name.as.string.length,
                           subject.as.result->value, true);
                ip = base + ok_offset;
            } else {
                AglVal name = chunk->constants[err_name_idx];
                env_define(&vm->env, name.as.string.data, name.as.string.length,
                           subject.as.result->value, true);
                ip = base + err_offset;
            }
            /* Note: the match arm body will execute and leave a value on stack.
             * The ok arm ends with JUMP past err arm. Env is restored after. */
            /* We need to restore env after the match body executes.
             * For now, we let the match body run, and POP_SCOPE handles cleanup.
             * Actually, match is an expression in the compiler — the body just pushes
             * a value. We'll restore env count after both arms converge. */
            /* Restore happens naturally when the enclosing scope ends. For match as
             * an expression, we need explicit cleanup. Let's emit POP_SCOPE 1 in
             * the compiler after each arm. For now, just continue. */
            break;
        }

        case AGL_OP_TRY: {
            AglVal val = vm_pop(vm);
            if (val.kind != VAL_RESULT) {
                agl_error_set(vm->ctx, AGL_ERR_TYPE,
                              agl_loc(NULL, vm->current_line, 0),
                              "'?' operator requires a result value");
                return -1;
            }
            if (val.as.result->is_ok) {
                vm_push(vm, val.as.result->value);
            } else {
                /* Propagate err: push the err result and return from function */
                vm_push(vm, val);
                return 0;
            }
            break;
        }

        case AGL_OP_ITER_SETUP: {
            AglVal arr = vm_pop(vm);
            if (arr.kind != VAL_ARRAY) {
                agl_error_set(vm->ctx, AGL_ERR_TYPE, agl_loc(NULL, vm->current_line, 0), "for-in requires an array");
                return -1;
            }
            /* Push iteration state: array, length, index */
            vm_push(vm, arr);
            vm_push(vm, val_int(arr.as.array->count));
            vm_push(vm, val_int(0)); /* current index */
            break;
        }

        case AGL_OP_ITER_NEXT: {
            uint16_t end_offset = read_u16(ip); ip += 2;
            /* Stack: [..., array, len, index] */
            AglVal idx = vm_peek(vm, 0);
            AglVal len = vm_peek(vm, 1);
            if (idx.as.integer >= len.as.integer) {
                ip += end_offset;
                break;
            }
            AglVal arr = vm_peek(vm, 2);
            vm_push(vm, arr.as.array->elements[(int)idx.as.integer]);
            /* Increment index */
            vm->stack[vm->stack_top - 2].as.integer++; /* update index in-place */
            break;
        }

        case AGL_OP_ITER_CLEANUP: {
            /* Pop array, len, index */
            vm_pop(vm); vm_pop(vm); vm_pop(vm);
            break;
        }

        case AGL_OP_IMPORT: {
            uint16_t idx = read_u16(ip); ip += 2;
            AglVal path_val = chunk->constants[idx];
            const char *import_path = path_val.as.string.data;
            int import_len = path_val.as.string.length;

            /* Resolve path */
            char resolved[512];
            if (!resolve_import_path(vm->file, import_path, import_len,
                                     resolved, sizeof(resolved))) {
                agl_error_set(vm->ctx, AGL_ERR_IO, agl_loc(NULL, vm->current_line, 0),
                              "invalid import path '%.*s'", import_len, import_path);
                return -1;
            }

            /* Check module cache */
            bool already_loaded = false;
            for (int i = 0; i < vm->module_count; i++) {
                if (strcmp(vm->modules[i].path, resolved) == 0) {
                    already_loaded = true;
                    break;
                }
            }
            if (already_loaded) break;

            /* Read module file */
            char *mod_source = agl_read_file(resolved);
            if (!mod_source) {
                agl_error_set(vm->ctx, AGL_ERR_IO, agl_loc(NULL, vm->current_line, 0),
                              "cannot open module '%.*s'", import_len, import_path);
                return -1;
            }

            /* Parse */
            AglArena *mod_arena = agl_arena_new();
            if (!mod_arena) { free(mod_source); return -1; }
            AglParser mod_parser;
            agl_parser_init(&mod_parser, mod_source, resolved, mod_arena, vm->ctx);
            AglNode *mod_prog = agl_parser_parse(&mod_parser);
            if (!mod_prog || agl_error_occurred(vm->ctx)) {
                agl_arena_free(mod_arena); free(mod_source); return -1;
            }

            /* Register module before execution (prevents circular imports) */
            if (vm->module_count < MAX_MODULES) {
                AglModule *m = &vm->modules[vm->module_count++];
                m->path = strdup(resolved);
                m->source = mod_source;
                m->arena = mod_arena;
            } else {
                agl_arena_free(mod_arena); free(mod_source);
                agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0),
                              "too many modules (max %d)", MAX_MODULES);
                return -1;
            }

            /* Compile + execute module */
            AglChunk *mod_chunk = agl_compile(mod_prog, vm->ctx, mod_arena, vm->gc);
            if (!mod_chunk || agl_error_occurred(vm->ctx)) return -1;

            const char *saved_file = vm->file;
            vm->file = resolved;
            int rc = vm_execute(vm, mod_chunk);
            vm->file = saved_file;
            agl_chunk_free(mod_chunk);
            if (rc != 0) return -1;
            break;
        }

        case AGL_OP_LINE: {
            uint16_t line = read_u16(ip); ip += 2;
            vm->current_line = line;
            /* GC check at statement boundaries */
            if (agl_gc_should_collect(vm->gc)) {
                vm_gc_collect(vm);
            }
            break;
        }

        default:
            agl_error_set(vm->ctx, AGL_ERR_RUNTIME, agl_loc(NULL, vm->current_line, 0),
                          "unknown opcode %d", op);
            return -1;
        }
    }

    return 0;
}

/* ---- Public API ---- */

int agl_vm_run(AglChunk *chunk, const char *filename, AglCtx *ctx) {
    if (!chunk || !ctx) return -1;

    AglArena *arena = agl_arena_new();
    if (!arena) return -1;

    AglGc *gc = agl_gc_new();
    if (!gc) { agl_arena_free(arena); return -1; }

    Vm vm;
    memset(&vm, 0, sizeof(vm));
    env_init(&vm.env);
    vm.ctx = ctx;
    vm.arena = arena;
    vm.gc = gc;
    vm.file = filename ? filename : "<stdin>";
    vm.current_line = 1;

    ctx->trace_cb = vm_capture_trace;
    ctx->trace_data = &vm;

    int result = vm_execute(&vm, chunk);

    ctx->trace_cb = NULL;
    ctx->trace_data = NULL;
    /* Free module cache */
    for (int i = 0; i < vm.module_count; i++) {
        free(vm.modules[i].path);
        free(vm.modules[i].source);
        agl_arena_free(vm.modules[i].arena);
    }

    agl_gc_free(gc);
    agl_arena_free(arena);
    return result;
}

/* ---- Compile + execute AST via VM ---- */

int agl_vm_interpret(AglNode *program, const char *filename, AglCtx *ctx) {
    if (!program || program->kind != AGL_NODE_PROGRAM) return -1;

    AglArena *arena = agl_arena_new();
    if (!arena) return -1;

    AglGc *gc = agl_gc_new();
    if (!gc) { agl_arena_free(arena); return -1; }

    AglChunk *chunk = agl_compile(program, ctx, arena, gc);
    if (!chunk || agl_error_occurred(ctx)) {
        agl_gc_free(gc);
        agl_arena_free(arena);
        return -1;
    }

    Vm vm;
    memset(&vm, 0, sizeof(vm));
    env_init(&vm.env);
    vm.ctx = ctx;
    vm.arena = arena;
    vm.gc = gc;
    vm.file = filename ? filename : "<stdin>";
    vm.current_line = 1;

    ctx->trace_cb = vm_capture_trace;
    ctx->trace_data = &vm;

    int result = vm_execute(&vm, chunk);

    ctx->trace_cb = NULL;
    ctx->trace_data = NULL;
    agl_chunk_free(chunk);
    for (int i = 0; i < vm.module_count; i++) {
        free(vm.modules[i].path);
        free(vm.modules[i].source);
        agl_arena_free(vm.modules[i].arena);
    }
    agl_gc_free(gc);
    agl_arena_free(arena);
    return result;
}

/* ---- VM-based REPL ---- */

struct AglVmRepl {
    Vm vm;
    AglCtx *ctx;
};

AglVmRepl *agl_vm_repl_new(void) {
    AglVmRepl *repl = calloc(1, sizeof(AglVmRepl));
    if (!repl) return NULL;

    repl->ctx = agl_ctx_new();
    if (!repl->ctx) { free(repl); return NULL; }

    AglArena *arena = agl_arena_new();
    if (!arena) { agl_ctx_free(repl->ctx); free(repl); return NULL; }

    AglGc *gc = agl_gc_new();
    if (!gc) { agl_arena_free(arena); agl_ctx_free(repl->ctx); free(repl); return NULL; }

    env_init(&repl->vm.env);
    repl->vm.ctx = repl->ctx;
    repl->vm.arena = arena;
    repl->vm.gc = gc;
    repl->vm.file = "<repl>";
    repl->vm.current_line = 1;

    repl->ctx->trace_cb = vm_capture_trace;
    repl->ctx->trace_data = &repl->vm;

    return repl;
}

void agl_vm_repl_free(AglVmRepl *repl) {
    if (!repl) return;
    for (int i = 0; i < repl->vm.module_count; i++) {
        free(repl->vm.modules[i].path);
        free(repl->vm.modules[i].source);
        agl_arena_free(repl->vm.modules[i].arena);
    }
    agl_gc_free(repl->vm.gc);
    agl_arena_free(repl->vm.arena);
    repl->ctx->trace_cb = NULL;
    agl_ctx_free(repl->ctx);
    free(repl);
}

int agl_vm_repl_exec(AglVmRepl *repl, const char *source) {
    if (!repl || !source) return -1;

    agl_error_clear(repl->ctx);

    /* Copy source into arena for AST pointer safety */
    AglArena *parse_arena = agl_arena_new();
    if (!parse_arena) return -1;

    size_t src_len = strlen(source);
    char *src_copy = agl_arena_alloc(parse_arena, src_len + 1);
    if (!src_copy) { agl_arena_free(parse_arena); return -1; }
    memcpy(src_copy, source, src_len + 1);

    AglParser parser;
    agl_parser_init(&parser, src_copy, "<repl>", parse_arena, repl->ctx);
    AglNode *program = agl_parser_parse(&parser);

    if (!program || agl_error_occurred(repl->ctx)) {
        if (agl_error_occurred(repl->ctx)) {
            agl_error_print(agl_error_get(repl->ctx));
        }
        agl_arena_free(parse_arena);
        return -1;
    }

    AglChunk *chunk = agl_compile(program, repl->ctx, parse_arena, repl->vm.gc);
    if (!chunk || agl_error_occurred(repl->ctx)) {
        if (agl_error_occurred(repl->ctx)) {
            agl_error_print(agl_error_get(repl->ctx));
        }
        agl_arena_free(parse_arena);
        return -1;
    }

    int result = vm_execute(&repl->vm, chunk);
    if (agl_error_occurred(repl->ctx)) {
        agl_error_print(agl_error_get(repl->ctx));
        agl_chunk_free(chunk);
        /* Keep parse arena alive for fn decls that reference AST */
        return -1;
    }

    agl_chunk_free(chunk);
    /* Keep parse arena alive — fn decl AST nodes may be referenced */
    return result;
}
