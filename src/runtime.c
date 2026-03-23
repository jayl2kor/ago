#include "runtime.h"

/* ---- String content helper: strips quotes if present ---- */

const char *str_content(AgoVal s, int *out_len) {
    if (s.as.string.length >= 2 && s.as.string.data[0] == '"') {
        *out_len = s.as.string.length - 2;
        return s.as.string.data + 1;
    }
    *out_len = s.as.string.length;
    return s.as.string.data;
}

/* ---- GC cleanup callbacks ---- */

void array_cleanup(void *p) {
    AgoArrayVal *arr = p;
    free(arr->elements);
}

void fn_cleanup(void *p) {
    AgoFnVal *fn = p;
    free(fn->captured_names);
    free(fn->captured_name_lengths);
    free(fn->captured_values);
    free(fn->captured_immutable);
}

/* ---- Environment ---- */

void env_init(AgoEnv *env) {
    env->count = 0;
}

bool env_define(AgoEnv *env, const char *name, int length, AgoVal val,
                bool is_immutable) {
    if (env->count >= MAX_VARS) return false;
    env->names[env->count] = name;
    env->name_lengths[env->count] = length;
    env->values[env->count] = val;
    env->immutable[env->count] = is_immutable;
    env->count++;
    return true;
}

AgoVal *env_get(AgoEnv *env, const char *name, int length) {
    for (int i = env->count - 1; i >= 0; i--) {
        if (ago_str_eq(env->names[i], env->name_lengths[i], name, length)) {
            return &env->values[i];
        }
    }
    return NULL;
}

int env_assign(AgoEnv *env, const char *name, int length, AgoVal val) {
    for (int i = env->count - 1; i >= 0; i--) {
        if (ago_str_eq(env->names[i], env->name_lengths[i], name, length)) {
            if (env->immutable[i]) return 2;
            env->values[i] = val;
            return 0;
        }
    }
    return 1;
}

/* ---- GC: mark reachable values ---- */

void mark_val(AgoVal val) {
    switch (val.kind) {
    case VAL_ARRAY:
        if (!val.as.array || val.as.array->obj.marked) break;
        ago_gc_mark(&val.as.array->obj);
        for (int i = 0; i < val.as.array->count; i++) {
            mark_val(val.as.array->elements[i]);
        }
        break;
    case VAL_STRUCT:
        if (!val.as.strct || val.as.strct->obj.marked) break;
        ago_gc_mark(&val.as.strct->obj);
        for (int i = 0; i < val.as.strct->field_count; i++) {
            mark_val(val.as.strct->field_values[i]);
        }
        break;
    case VAL_FN:
        if (!val.as.fn || val.as.fn->obj.marked) break;
        ago_gc_mark(&val.as.fn->obj);
        for (int i = 0; i < val.as.fn->captured_count; i++) {
            mark_val(val.as.fn->captured_values[i]);
        }
        break;
    case VAL_RESULT:
        if (!val.as.result || val.as.result->obj.marked) break;
        ago_gc_mark(&val.as.result->obj);
        mark_val(val.as.result->value);
        break;
    default:
        break;  /* scalars: no heap objects */
    }
}

void gc_collect(AgoInterp *interp) {
    for (int i = 0; i < interp->env.count; i++) {
        mark_val(interp->env.values[i]);
    }
    mark_val(interp->return_value);
    ago_gc_sweep(interp->gc);
}

/* ---- Truthiness ---- */

bool is_truthy(AgoVal val) {
    switch (val.kind) {
    case VAL_BOOL:   return val.as.boolean;
    case VAL_NIL:    return false;
    case VAL_INT:    return val.as.integer != 0;
    case VAL_FLOAT:  return val.as.floating != 0.0;
    case VAL_STRING: return val.as.string.length > 0;
    case VAL_FN:     return true;
    case VAL_ARRAY:  return val.as.array->count > 0;
    case VAL_STRUCT: return true;
    case VAL_RESULT: return val.as.result->is_ok;
    }
    return false;
}

/* ---- Value printing ---- */

void print_val_inline(AgoVal val) {
    int slen;
    const char *sdata;
    switch (val.kind) {
    case VAL_INT:    printf("%lld", (long long)val.as.integer); break;
    case VAL_FLOAT:  printf("%g", val.as.floating); break;
    case VAL_BOOL:   printf("%s", val.as.boolean ? "true" : "false"); break;
    case VAL_STRING:
        sdata = str_content(val, &slen);
        printf("%.*s", slen, sdata);
        break;
    case VAL_NIL:    printf("nil"); break;
    case VAL_FN:     printf("<fn>"); break;
    case VAL_ARRAY:
        printf("[");
        for (int i = 0; i < val.as.array->count; i++) {
            if (i > 0) printf(", ");
            AgoVal elem = val.as.array->elements[i];
            if (elem.kind == VAL_STRING) {
                sdata = str_content(elem, &slen);
                printf("\"%.*s\"", slen, sdata);
            } else {
                print_val_inline(elem);
            }
        }
        printf("]");
        break;
    case VAL_STRUCT:
        printf("<struct %.*s>", val.as.strct->type_name_length, val.as.strct->type_name);
        break;
    case VAL_RESULT:
        printf("%s(", val.as.result->is_ok ? "ok" : "err");
        print_val_inline(val.as.result->value);
        printf(")");
        break;
    }
}

void builtin_print(AgoVal val) {
    print_val_inline(val);
    printf("\n");
}

/* ---- Trace capture callback (called by ago_error_set) ---- */

void capture_trace(void *data, AgoError *err) {
    AgoInterp *interp = data;
    int n = interp->call_depth;
    if (n > AGO_MAX_TRACE) n = AGO_MAX_TRACE;
    err->trace_count = n;
    /* Copy frames innermost-first (most recent call at index 0) */
    for (int i = 0; i < n; i++) {
        int src_idx = interp->call_depth - 1 - i;
        AgoCallFrame *f = &interp->call_frames[src_idx];
        AgoTraceFrame *t = &err->trace[i];
        int name_len = f->name_len;
        if (name_len >= (int)sizeof(t->name)) name_len = (int)sizeof(t->name) - 1;
        if (f->name && name_len > 0) {
            memcpy(t->name, f->name, (size_t)name_len);
            t->name[name_len] = '\0';
        } else {
            t->name[0] = '\0';
        }
        t->line = f->line;
        t->column = f->column;
    }
}

/* ---- Call user function with pre-evaluated arguments ---- */

AgoVal call_fn_direct(AgoInterp *interp, AgoFnVal *fn,
                      AgoVal *args, int arg_count,
                      int line, int column) {
    if (interp->call_depth >= MAX_CALL_DEPTH) {
        ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                      ago_loc(NULL, line, column),
                      "maximum call depth exceeded (limit %d)", MAX_CALL_DEPTH);
        return val_nil();
    }

    /* Push call frame for trace */
    AgoCallFrame *frame = &interp->call_frames[interp->call_depth];
    frame->name = fn->decl->as.fn_decl.name;
    frame->name_len = fn->decl->as.fn_decl.name_length;
    frame->line = line;
    frame->column = column;

    interp->call_depth++;

    AgoNode *decl = fn->decl;
    int param_count = decl->as.fn_decl.param_count;

    if (arg_count != param_count) {
        ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                      ago_loc(NULL, line, column),
                      "expected %d arguments, got %d", param_count, arg_count);
        interp->call_depth--;
        return val_nil();
    }

    /* For closures: arena-allocate env snapshot */
    AgoEnv *saved_closure_env = NULL;
    if (fn->captured_count > 0) {
        saved_closure_env = ago_arena_alloc(interp->arena, sizeof(AgoEnv));
        if (!saved_closure_env) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, column), "out of memory");
            interp->call_depth--;
            return val_nil();
        }
        *saved_closure_env = interp->env;
        env_init(&interp->env);
        for (int i = 0; i < fn->captured_count; i++) {
            if (!env_define(&interp->env,
                            fn->captured_names[i],
                            fn->captured_name_lengths[i],
                            fn->captured_values[i],
                            fn->captured_immutable[i])) {
                ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                              ago_loc(NULL, line, column),
                              "too many variables (max %d)", MAX_VARS);
                interp->env = *saved_closure_env;
                interp->call_depth--;
                return val_nil();
            }
        }
    }

    int saved_count = interp->env.count;

    for (int i = 0; i < param_count; i++) {
        if (!env_define(&interp->env,
                        decl->as.fn_decl.param_names[i],
                        decl->as.fn_decl.param_name_lengths[i],
                        args[i], true)) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, line, column),
                          "too many variables (max %d)", MAX_VARS);
            if (saved_closure_env) interp->env = *saved_closure_env;
            else interp->env.count = saved_count;
            interp->call_depth--;
            return val_nil();
        }
    }

    interp->has_return = false;
    interp->return_value = val_nil();

    bool prev_jmp_set = interp->return_jmp_set;
    jmp_buf prev_jmp;
    if (prev_jmp_set) memcpy(prev_jmp, interp->return_jmp, sizeof(jmp_buf));

    interp->return_jmp_set = true;
    if (setjmp(interp->return_jmp) == 0) {
        if (decl->as.fn_decl.body) {
            exec_stmt(interp, decl->as.fn_decl.body);
        }
    }

    AgoVal result = interp->return_value;
    interp->has_return = false;

    interp->return_jmp_set = prev_jmp_set;
    if (prev_jmp_set) memcpy(interp->return_jmp, prev_jmp, sizeof(jmp_buf));

    if (saved_closure_env) {
        interp->env = *saved_closure_env;
    } else {
        interp->env.count = saved_count;
    }

    interp->call_depth--;
    return result;
}
