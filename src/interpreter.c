#include "interpreter.h"
#include "parser.h"
#include "sema.h"
#include "arena.h"
#include "gc.h"
#include <setjmp.h>
#include <limits.h>

/* ---- Runtime Value ---- */

typedef enum {
    VAL_INT,
    VAL_FLOAT,
    VAL_BOOL,
    VAL_STRING,
    VAL_FN,
    VAL_ARRAY,
    VAL_STRUCT,
    VAL_RESULT,
    VAL_NIL,
} AgoValKind;

typedef struct AgoFnVal AgoFnVal;
typedef struct AgoArrayVal AgoArrayVal;
typedef struct AgoStructVal AgoStructVal;

typedef struct AgoResultVal AgoResultVal;

typedef struct {
    AgoValKind kind;
    union {
        int64_t integer;
        double floating;
        bool boolean;
        struct { const char *data; int length; } string;
        AgoFnVal *fn;
        AgoArrayVal *array;
        AgoStructVal *strct;
        AgoResultVal *result;
    } as;
} AgoVal;

struct AgoFnVal {
    AgoObj obj;     /* GC header */
    AgoNode *decl;
    int captured_count;
    const char **captured_names;        /* malloc'd */
    int *captured_name_lengths;         /* malloc'd */
    AgoVal *captured_values;            /* malloc'd */
    bool *captured_immutable;           /* malloc'd */
};

#define MAX_ARRAY_SIZE 1024

struct AgoArrayVal {
    AgoObj obj;     /* GC header */
    AgoVal *elements;   /* malloc'd */
    int count;
};

#define MAX_STRUCT_FIELDS 64

struct AgoStructVal {
    AgoObj obj;     /* GC header */
    const char *type_name;
    int type_name_length;
    const char *field_names[MAX_STRUCT_FIELDS];
    int field_name_lengths[MAX_STRUCT_FIELDS];
    AgoVal field_values[MAX_STRUCT_FIELDS];
    int field_count;
};

struct AgoResultVal {
    AgoObj obj;     /* GC header */
    bool is_ok;
    AgoVal value;
};

/* ---- String content helper: strips quotes if present ---- */

static const char *str_content(AgoVal s, int *out_len) {
    if (s.as.string.length >= 2 && s.as.string.data[0] == '"') {
        *out_len = s.as.string.length - 2;
        return s.as.string.data + 1;
    }
    *out_len = s.as.string.length;
    return s.as.string.data;
}

/* ---- Cleanup functions for GC ---- */

static void array_cleanup(void *p) {
    AgoArrayVal *arr = p;
    free(arr->elements);
}

static void fn_cleanup(void *p) {
    AgoFnVal *fn = p;
    free(fn->captured_names);
    free(fn->captured_name_lengths);
    free(fn->captured_values);
    free(fn->captured_immutable);
}

static AgoVal val_int(int64_t v)    { return (AgoVal){VAL_INT,    {.integer = v}}; }
static AgoVal val_float(double v)   { return (AgoVal){VAL_FLOAT,  {.floating = v}}; }
static AgoVal val_bool(bool v)      { return (AgoVal){VAL_BOOL,   {.boolean = v}}; }
static AgoVal val_nil(void)         { return (AgoVal){VAL_NIL,    {.integer = 0}}; }
static AgoVal val_string(const char *s, int len) {
    AgoVal v;
    v.kind = VAL_STRING;
    v.as.string.data = s;
    v.as.string.length = len;
    return v;
}

/* ---- Environment (variable bindings with scope) ---- */

#define MAX_VARS 256

typedef struct {
    const char *names[MAX_VARS];
    int name_lengths[MAX_VARS];
    AgoVal values[MAX_VARS];
    bool immutable[MAX_VARS];   /* true for let, false for var */
    int count;
} AgoEnv;

static void env_init(AgoEnv *env) {
    env->count = 0;
}

static bool env_define(AgoEnv *env, const char *name, int length, AgoVal val,
                       bool is_immutable) {
    if (env->count >= MAX_VARS) return false;
    env->names[env->count] = name;
    env->name_lengths[env->count] = length;
    env->values[env->count] = val;
    env->immutable[env->count] = is_immutable;
    env->count++;
    return true;
}

static AgoVal *env_get(AgoEnv *env, const char *name, int length) {
    for (int i = env->count - 1; i >= 0; i--) {
        if (ago_str_eq(env->names[i], env->name_lengths[i], name, length)) {
            return &env->values[i];
        }
    }
    return NULL;
}

/* Find binding and update in place. Returns: 0=ok, 1=not found, 2=immutable */
static int env_assign(AgoEnv *env, const char *name, int length, AgoVal val) {
    for (int i = env->count - 1; i >= 0; i--) {
        if (ago_str_eq(env->names[i], env->name_lengths[i], name, length)) {
            if (env->immutable[i]) return 2;
            env->values[i] = val;
            return 0;
        }
    }
    return 1;
}

/* ---- Interpreter state ---- */

#define MAX_MODULES 64
#define MAX_CALL_DEPTH 512

typedef struct {
    char *path;
    char *source;       /* malloc'd source text — AST tokens point into this */
    AgoArena *arena;    /* AST nodes live here */
} AgoModule;

typedef struct {
    AgoEnv env;
    AgoCtx *ctx;
    AgoArena *arena;    /* temporary allocations (saved_closure_env) */
    AgoGc *gc;          /* GC-tracked runtime objects */
    const char *file;   /* current file path (for import resolution) */
    /* Module cache */
    AgoModule modules[MAX_MODULES];
    int module_count;
    /* Return mechanism */
    bool has_return;
    AgoVal return_value;
    jmp_buf return_jmp;
    bool return_jmp_set;
    int call_depth;
} AgoInterp;

/* Forward declarations */
static AgoVal eval_expr(AgoInterp *interp, AgoNode *node);
static void exec_stmt(AgoInterp *interp, AgoNode *node);

/* ---- GC: mark reachable values ---- */

static void mark_val(AgoVal val) {
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

static void gc_collect(AgoInterp *interp) {
    /* Mark all reachable values from environment */
    for (int i = 0; i < interp->env.count; i++) {
        mark_val(interp->env.values[i]);
    }
    mark_val(interp->return_value);
    /* Sweep unreachable objects */
    ago_gc_sweep(interp->gc);
}

/* ---- Module loading helpers ---- */

/* Extract directory from a file path. Returns "" for bare filenames. */
static void path_dir(const char *filepath, char *buf, size_t bufsize) {
    const char *last_sep = NULL;
    for (const char *p = filepath; *p; p++) {
        if (*p == '/') last_sep = p;
    }
    if (!last_sep) {
        buf[0] = '\0';
        return;
    }
    size_t len = (size_t)(last_sep - filepath);
    if (len >= bufsize) len = bufsize - 1;
    memcpy(buf, filepath, len);
    buf[len] = '\0';
}

/* Resolve import path relative to current file. Appends .ago extension.
 * Returns false if path escapes the base directory (path traversal). */
static bool resolve_import(const char *base_file, const char *import_path,
                           int import_len, char *buf, size_t bufsize) {
    /* Reject paths containing ".." to prevent directory traversal */
    for (int i = 0; i < import_len - 1; i++) {
        if (import_path[i] == '.' && import_path[i + 1] == '.') return false;
    }

    char dir[512];
    path_dir(base_file, dir, sizeof(dir));
    int written;
    if (dir[0]) {
        written = snprintf(buf, bufsize, "%s/%.*s.ago", dir, import_len, import_path);
    } else {
        written = snprintf(buf, bufsize, "%.*s.ago", import_len, import_path);
    }
    /* Check for truncation */
    if (written < 0 || (size_t)written >= bufsize) return false;

    /* Canonicalize and verify path stays within base directory */
    char real_base[PATH_MAX], real_resolved[PATH_MAX];
    if (!realpath(dir[0] ? dir : ".", real_base)) return false;
    if (!realpath(buf, real_resolved)) return false;
    size_t base_len = strlen(real_base);
    if (strncmp(real_resolved, real_base, base_len) != 0) return false;
    /* Ensure the char after base prefix is '/' or '\0' */
    if (real_resolved[base_len] != '/' && real_resolved[base_len] != '\0') return false;
    return true;
}

/* Read entire file into malloc'd buffer. Returns NULL on failure. */
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t read = fread(buf, 1, (size_t)len, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

/* Check if module already loaded */
static bool module_loaded(AgoInterp *interp, const char *path) {
    for (int i = 0; i < interp->module_count; i++) {
        if (strcmp(interp->modules[i].path, path) == 0) return true;
    }
    return false;
}

/* Register module — takes ownership of path (strdup'd), source, and arena */
static bool module_register(AgoInterp *interp, const char *path,
                            char *source, AgoArena *arena) {
    if (interp->module_count >= MAX_MODULES) return false;
    AgoModule *m = &interp->modules[interp->module_count++];
    m->path = strdup(path);
    m->source = source;
    m->arena = arena;
    return true;
}

/* Free all module resources */
static void module_cache_free(AgoInterp *interp) {
    for (int i = 0; i < interp->module_count; i++) {
        free(interp->modules[i].path);
        free(interp->modules[i].source);
        ago_arena_free(interp->modules[i].arena);
    }
}

/* ---- Truthiness ---- */

static bool is_truthy(AgoVal val) {
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

/* Print value without trailing newline. Handles all types recursively. */
static void print_val_inline(AgoVal val) {
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

static void builtin_print(AgoVal val) {
    print_val_inline(val);
    printf("\n");
}

/* ---- Call user function ---- */

/* Call a function with pre-evaluated arguments. Core calling convention. */
static AgoVal call_fn_direct(AgoInterp *interp, AgoFnVal *fn,
                             AgoVal *args, int arg_count,
                             int line, int column) {
    if (interp->call_depth >= MAX_CALL_DEPTH) {
        ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                      ago_loc(NULL, line, column),
                      "maximum call depth exceeded (limit %d)", MAX_CALL_DEPTH);
        return val_nil();
    }
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

/* Call a user function from an AST call node (evaluates args from AST) */
static AgoVal call_user_fn(AgoInterp *interp, AgoFnVal *fn, AgoNode *call_node) {
    int arg_count = call_node->as.call.arg_count;
    if (arg_count > 64) {
        ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                      ago_loc(NULL, call_node->line, call_node->column),
                      "too many arguments (max 64)");
        return val_nil();
    }
    AgoVal args[64];
    for (int i = 0; i < arg_count; i++) {
        args[i] = eval_expr(interp, call_node->as.call.args[i]);
        if (ago_error_occurred(interp->ctx)) return val_nil();
    }
    return call_fn_direct(interp, fn, args, arg_count,
                          call_node->line, call_node->column);
}

/* ---- Expression evaluation ---- */

static AgoVal eval_expr(AgoInterp *interp, AgoNode *node) {
    if (!node || ago_error_occurred(interp->ctx)) return val_nil();

    switch (node->kind) {
    case AGO_NODE_INT_LIT:
        return val_int(node->as.int_lit.value);

    case AGO_NODE_FLOAT_LIT:
        return val_float(node->as.float_lit.value);

    case AGO_NODE_STRING_LIT:
        return val_string(node->as.string_lit.value, node->as.string_lit.length);

    case AGO_NODE_BOOL_LIT:
        return val_bool(node->as.bool_lit.value);

    case AGO_NODE_IDENT: {
        AgoVal *v = env_get(&interp->env, node->as.ident.name, node->as.ident.length);
        if (!v) {
            ago_error_set(interp->ctx, AGO_ERR_NAME,
                          ago_loc(NULL, node->line, node->column),
                          "undefined variable '%.*s'",
                          node->as.ident.length, node->as.ident.name);
            return val_nil();
        }
        return *v;
    }

    case AGO_NODE_UNARY: {
        AgoVal operand = eval_expr(interp, node->as.unary.operand);
        if (ago_error_occurred(interp->ctx)) return val_nil();

        if (node->as.unary.op == AGO_TOKEN_MINUS) {
            if (operand.kind == VAL_INT) return val_int(-operand.as.integer);
            if (operand.kind == VAL_FLOAT) return val_float(-operand.as.floating);
        }
        if (node->as.unary.op == AGO_TOKEN_NOT) {
            if (operand.kind == VAL_BOOL) return val_bool(!operand.as.boolean);
        }
        ago_error_set(interp->ctx, AGO_ERR_TYPE,
                      ago_loc(NULL, node->line, node->column),
                      "invalid unary operator");
        return val_nil();
    }

    case AGO_NODE_BINARY: {
        AgoTokenKind op = node->as.binary.op;

        /* Field access: don't evaluate right side (it's a field name, not an expression) */
        if (op == AGO_TOKEN_DOT) {
            AgoVal left = eval_expr(interp, node->as.binary.left);
            if (ago_error_occurred(interp->ctx)) return val_nil();
            AgoNode *field_node = node->as.binary.right;
            if (left.kind == VAL_STRUCT && field_node->kind == AGO_NODE_IDENT) {
                AgoStructVal *s = left.as.strct;
                for (int i = 0; i < s->field_count; i++) {
                    if (ago_str_eq(s->field_names[i], s->field_name_lengths[i],
                                   field_node->as.ident.name, field_node->as.ident.length)) {
                        return s->field_values[i];
                    }
                }
                ago_error_set(interp->ctx, AGO_ERR_NAME,
                              ago_loc(NULL, node->line, node->column),
                              "no field '%.*s'", field_node->as.ident.length,
                              field_node->as.ident.name);
            } else {
                ago_error_set(interp->ctx, AGO_ERR_TYPE,
                              ago_loc(NULL, node->line, node->column),
                              "cannot access field on non-struct value");
            }
            return val_nil();
        }

        AgoVal left = eval_expr(interp, node->as.binary.left);
        if (ago_error_occurred(interp->ctx)) return val_nil();
        AgoVal right = eval_expr(interp, node->as.binary.right);
        if (ago_error_occurred(interp->ctx)) return val_nil();

        if (left.kind == VAL_INT && right.kind == VAL_INT) {
            int64_t l = left.as.integer, r = right.as.integer;
            switch (op) {
            case AGO_TOKEN_PLUS:    return val_int(l + r);
            case AGO_TOKEN_MINUS:   return val_int(l - r);
            case AGO_TOKEN_STAR:    return val_int(l * r);
            case AGO_TOKEN_SLASH:
                if (r == 0) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                                  ago_loc(NULL, node->line, node->column),
                                  "division by zero");
                    return val_nil();
                }
                return val_int(l / r);
            case AGO_TOKEN_PERCENT:
                if (r == 0) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                                  ago_loc(NULL, node->line, node->column),
                                  "division by zero");
                    return val_nil();
                }
                return val_int(l % r);
            case AGO_TOKEN_EQ:      return val_bool(l == r);
            case AGO_TOKEN_NEQ:     return val_bool(l != r);
            case AGO_TOKEN_LT:      return val_bool(l < r);
            case AGO_TOKEN_GT:      return val_bool(l > r);
            case AGO_TOKEN_LE:      return val_bool(l <= r);
            case AGO_TOKEN_GE:      return val_bool(l >= r);
            default: break;
            }
        }

        /* Float arithmetic */
        if (left.kind == VAL_FLOAT && right.kind == VAL_FLOAT) {
            double l = left.as.floating, r = right.as.floating;
            switch (op) {
            case AGO_TOKEN_PLUS:    return val_float(l + r);
            case AGO_TOKEN_MINUS:   return val_float(l - r);
            case AGO_TOKEN_STAR:    return val_float(l * r);
            case AGO_TOKEN_SLASH:   return val_float(l / r);
            case AGO_TOKEN_EQ:      return val_bool(l == r);
            case AGO_TOKEN_NEQ:     return val_bool(l != r);
            case AGO_TOKEN_LT:      return val_bool(l < r);
            case AGO_TOKEN_GT:      return val_bool(l > r);
            case AGO_TOKEN_LE:      return val_bool(l <= r);
            case AGO_TOKEN_GE:      return val_bool(l >= r);
            default: break;
            }
        }

        /* Int-float promotion */
        if ((left.kind == VAL_INT && right.kind == VAL_FLOAT) ||
            (left.kind == VAL_FLOAT && right.kind == VAL_INT)) {
            double l = left.kind == VAL_FLOAT ? left.as.floating : (double)left.as.integer;
            double r = right.kind == VAL_FLOAT ? right.as.floating : (double)right.as.integer;
            switch (op) {
            case AGO_TOKEN_PLUS:    return val_float(l + r);
            case AGO_TOKEN_MINUS:   return val_float(l - r);
            case AGO_TOKEN_STAR:    return val_float(l * r);
            case AGO_TOKEN_SLASH:   return val_float(l / r);
            case AGO_TOKEN_EQ:      return val_bool(l == r);
            case AGO_TOKEN_NEQ:     return val_bool(l != r);
            case AGO_TOKEN_LT:      return val_bool(l < r);
            case AGO_TOKEN_GT:      return val_bool(l > r);
            case AGO_TOKEN_LE:      return val_bool(l <= r);
            case AGO_TOKEN_GE:      return val_bool(l >= r);
            default: break;
            }
        }

        if (left.kind == VAL_BOOL && right.kind == VAL_BOOL) {
            bool l = left.as.boolean, r = right.as.boolean;
            switch (op) {
            case AGO_TOKEN_AND:  return val_bool(l && r);
            case AGO_TOKEN_OR:   return val_bool(l || r);
            case AGO_TOKEN_EQ:   return val_bool(l == r);
            case AGO_TOKEN_NEQ:  return val_bool(l != r);
            default: break;
            }
        }

        /* String operations: ==, !=, + */
        if (left.kind == VAL_STRING && right.kind == VAL_STRING) {
            int llen, rlen;
            const char *ldata = str_content(left, &llen);
            const char *rdata = str_content(right, &rlen);
            switch (op) {
            case AGO_TOKEN_EQ:
                return val_bool(llen == rlen && memcmp(ldata, rdata, (size_t)llen) == 0);
            case AGO_TOKEN_NEQ:
                return val_bool(llen != rlen || memcmp(ldata, rdata, (size_t)llen) != 0);
            case AGO_TOKEN_PLUS: {
                if (llen > INT_MAX - rlen) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "string too large");
                    return val_nil();
                }
                int total = llen + rlen;
                char *buf = ago_arena_alloc(interp->arena, (size_t)total);
                if (!buf) { ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "out of memory"); return val_nil(); }
                memcpy(buf, ldata, (size_t)llen);
                memcpy(buf + llen, rdata, (size_t)rlen);
                return val_string(buf, total);
            }
            default: break;
            }
        }

        ago_error_set(interp->ctx, AGO_ERR_TYPE,
                      ago_loc(NULL, node->line, node->column),
                      "invalid binary operation");
        return val_nil();
    }

    case AGO_NODE_ARRAY_LIT: {
        AgoArrayVal *arr = ago_gc_alloc(interp->gc, sizeof(AgoArrayVal), array_cleanup);
        if (!arr) { ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "out of memory"); return val_nil(); }
        arr->count = node->as.array_lit.count;
        arr->elements = NULL;
        if (arr->count > 0) {
            arr->elements = malloc(sizeof(AgoVal) * (size_t)arr->count);
            if (!arr->elements) { ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "out of memory"); return val_nil(); }
        }
        for (int i = 0; i < arr->count; i++) {
            arr->elements[i] = eval_expr(interp, node->as.array_lit.elements[i]);
            if (ago_error_occurred(interp->ctx)) return val_nil();
        }
        AgoVal v;
        v.kind = VAL_ARRAY;
        v.as.array = arr;
        return v;
    }

    case AGO_NODE_INDEX: {
        AgoVal obj = eval_expr(interp, node->as.index_expr.object);
        if (ago_error_occurred(interp->ctx)) return val_nil();
        AgoVal idx = eval_expr(interp, node->as.index_expr.index);
        if (ago_error_occurred(interp->ctx)) return val_nil();
        if (obj.kind != VAL_ARRAY) {
            ago_error_set(interp->ctx, AGO_ERR_TYPE,
                          ago_loc(NULL, node->line, node->column),
                          "cannot index non-array value");
            return val_nil();
        }
        if (idx.kind != VAL_INT) {
            ago_error_set(interp->ctx, AGO_ERR_TYPE,
                          ago_loc(NULL, node->line, node->column),
                          "array index must be an integer");
            return val_nil();
        }
        int i = (int)idx.as.integer;
        if (i < 0 || i >= obj.as.array->count) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column),
                          "index %d out of bounds (length %d)", i, obj.as.array->count);
            return val_nil();
        }
        return obj.as.array->elements[i];
    }

    case AGO_NODE_STRUCT_LIT: {
        AgoStructVal *s = ago_gc_alloc(interp->gc, sizeof(AgoStructVal), NULL);
        if (!s) { ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "out of memory"); return val_nil(); }
        s->type_name = node->as.struct_lit.name;
        s->type_name_length = node->as.struct_lit.name_length;
        s->field_count = node->as.struct_lit.field_count;
        for (int i = 0; i < s->field_count; i++) {
            s->field_names[i] = node->as.struct_lit.field_names[i];
            s->field_name_lengths[i] = node->as.struct_lit.field_name_lengths[i];
            s->field_values[i] = eval_expr(interp, node->as.struct_lit.field_values[i]);
            if (ago_error_occurred(interp->ctx)) return val_nil();
        }
        AgoVal v;
        v.kind = VAL_STRUCT;
        v.as.strct = s;
        return v;
    }

    case AGO_NODE_RESULT_OK:
    case AGO_NODE_RESULT_ERR: {
        AgoVal inner = eval_expr(interp, node->as.result_val.value);
        if (ago_error_occurred(interp->ctx)) return val_nil();
        AgoResultVal *rv = ago_gc_alloc(interp->gc, sizeof(AgoResultVal), NULL);
        if (!rv) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column), "out of memory");
            return val_nil();
        }
        rv->is_ok = (node->kind == AGO_NODE_RESULT_OK);
        rv->value = inner;
        AgoVal v;
        v.kind = VAL_RESULT;
        v.as.result = rv;
        return v;
    }

    case AGO_NODE_MATCH_EXPR: {
        AgoVal subject = eval_expr(interp, node->as.match_expr.subject);
        if (ago_error_occurred(interp->ctx)) return val_nil();
        if (subject.kind != VAL_RESULT) {
            ago_error_set(interp->ctx, AGO_ERR_TYPE,
                          ago_loc(NULL, node->line, node->column),
                          "match requires a result value");
            return val_nil();
        }
        AgoResultVal *rv = subject.as.result;
        int saved_count = interp->env.count;
        AgoVal result;
        const char *bind_name;
        int bind_len;
        AgoNode *body;
        if (rv->is_ok) {
            bind_name = node->as.match_expr.ok_name;
            bind_len = node->as.match_expr.ok_name_length;
            body = node->as.match_expr.ok_body;
        } else {
            bind_name = node->as.match_expr.err_name;
            bind_len = node->as.match_expr.err_name_length;
            body = node->as.match_expr.err_body;
        }
        if (!env_define(&interp->env, bind_name, bind_len, rv->value, true)) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column),
                          "too many variables (max %d)", MAX_VARS);
            return val_nil();
        }
        result = eval_expr(interp, body);
        interp->env.count = saved_count;
        return result;
    }

    case AGO_NODE_LAMBDA: {
        AgoFnVal *fn = ago_gc_alloc(interp->gc, sizeof(AgoFnVal), fn_cleanup);
        if (!fn) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column), "out of memory");
            return val_nil();
        }
        fn->decl = node;
        fn->captured_names = NULL;
        fn->captured_name_lengths = NULL;
        fn->captured_values = NULL;
        fn->captured_immutable = NULL;
        fn->captured_count = interp->env.count;
        if (fn->captured_count > 0) {
            size_t n_cap = (size_t)fn->captured_count;
            fn->captured_names = malloc(sizeof(char *) * n_cap);
            fn->captured_name_lengths = malloc(sizeof(int) * n_cap);
            fn->captured_values = malloc(sizeof(AgoVal) * n_cap);
            fn->captured_immutable = malloc(sizeof(bool) * n_cap);
            if (!fn->captured_names || !fn->captured_name_lengths ||
                !fn->captured_values || !fn->captured_immutable) {
                ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                              ago_loc(NULL, node->line, node->column), "out of memory");
                return val_nil();
            }
            memcpy(fn->captured_names, interp->env.names, sizeof(char *) * n_cap);
            memcpy(fn->captured_name_lengths, interp->env.name_lengths, sizeof(int) * n_cap);
            memcpy(fn->captured_values, interp->env.values, sizeof(AgoVal) * n_cap);
            memcpy(fn->captured_immutable, interp->env.immutable, sizeof(bool) * n_cap);
        } else {
            fn->captured_names = NULL;
            fn->captured_name_lengths = NULL;
            fn->captured_values = NULL;
            fn->captured_immutable = NULL;
        }
        AgoVal fn_val;
        fn_val.kind = VAL_FN;
        fn_val.as.fn = fn;
        return fn_val;
    }

    case AGO_NODE_CALL: {
        /* Check built-in functions by name first */
        if (node->as.call.callee->kind == AGO_NODE_IDENT) {
            const char *name = node->as.call.callee->as.ident.name;
            int len = node->as.call.callee->as.ident.length;

            /* Built-in print */
            if (ago_str_eq(name, len, "print", 5)) {
                for (int i = 0; i < node->as.call.arg_count; i++) {
                    AgoVal arg = eval_expr(interp, node->as.call.args[i]);
                    if (ago_error_occurred(interp->ctx)) return val_nil();
                    builtin_print(arg);
                }
                return val_nil();
            }

            /* Built-in len */
            if (ago_str_eq(name, len, "len", 3)) {
                if (node->as.call.arg_count != 1) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                                  ago_loc(NULL, node->line, node->column),
                                  "len() takes exactly 1 argument");
                    return val_nil();
                }
                AgoVal arg = eval_expr(interp, node->as.call.args[0]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                if (arg.kind == VAL_ARRAY) return val_int(arg.as.array->count);
                if (arg.kind == VAL_STRING) {
                    int slen; str_content(arg, &slen);
                    return val_int(slen);
                }
                ago_error_set(interp->ctx, AGO_ERR_TYPE,
                              ago_loc(NULL, node->line, node->column),
                              "len() requires an array or string");
                return val_nil();
            }

            /* Built-in type(val) -> string */
            if (ago_str_eq(name, len, "type", 4)) {
                if (node->as.call.arg_count != 1) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "type() takes exactly 1 argument");
                    return val_nil();
                }
                AgoVal arg = eval_expr(interp, node->as.call.args[0]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                const char *tname;
                switch (arg.kind) {
                case VAL_INT:    tname = "int"; break;
                case VAL_FLOAT:  tname = "float"; break;
                case VAL_BOOL:   tname = "bool"; break;
                case VAL_STRING: tname = "string"; break;
                case VAL_FN:     tname = "fn"; break;
                case VAL_ARRAY:  tname = "array"; break;
                case VAL_STRUCT: tname = "struct"; break;
                case VAL_RESULT: tname = "result"; break;
                case VAL_NIL:    tname = "nil"; break;
                default:         tname = "unknown"; break;
                }
                return val_string(tname, (int)strlen(tname));
            }

            /* Built-in str(val) -> string */
            if (ago_str_eq(name, len, "str", 3)) {
                if (node->as.call.arg_count != 1) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "str() takes exactly 1 argument");
                    return val_nil();
                }
                AgoVal arg = eval_expr(interp, node->as.call.args[0]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                char buf[256];
                int n = 0;
                switch (arg.kind) {
                case VAL_INT:    n = snprintf(buf, sizeof(buf), "%lld", (long long)arg.as.integer); break;
                case VAL_FLOAT:  n = snprintf(buf, sizeof(buf), "%g", arg.as.floating); break;
                case VAL_BOOL:   n = snprintf(buf, sizeof(buf), "%s", arg.as.boolean ? "true" : "false"); break;
                case VAL_NIL:    n = snprintf(buf, sizeof(buf), "nil"); break;
                case VAL_FN:     n = snprintf(buf, sizeof(buf), "<fn>"); break;
                case VAL_STRUCT: n = snprintf(buf, sizeof(buf), "<struct %.*s>", arg.as.strct->type_name_length, arg.as.strct->type_name); break;
                case VAL_RESULT: n = snprintf(buf, sizeof(buf), "%s(...)", arg.as.result->is_ok ? "ok" : "err"); break;
                case VAL_ARRAY:  n = snprintf(buf, sizeof(buf), "<array[%d]>", arg.as.array->count); break;
                case VAL_STRING: {
                    int slen; const char *sd = str_content(arg, &slen);
                    char *copy = ago_arena_alloc(interp->arena, (size_t)slen);
                    if (copy) memcpy(copy, sd, (size_t)slen);
                    return val_string(copy ? copy : "", copy ? slen : 0);
                }
                }
                if (n < 0) n = 0;
                if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
                char *s = ago_arena_alloc(interp->arena, (size_t)n);
                if (s) memcpy(s, buf, (size_t)n);
                return val_string(s ? s : "", s ? n : 0);
            }

            /* Built-in int(string) -> int */
            if (ago_str_eq(name, len, "int", 3)) {
                if (node->as.call.arg_count != 1) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "int() takes exactly 1 argument");
                    return val_nil();
                }
                AgoVal arg = eval_expr(interp, node->as.call.args[0]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                if (arg.kind == VAL_STRING) {
                    int slen; const char *sd = str_content(arg, &slen);
                    char tmp[64];
                    if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
                    memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
                    char *end;
                    int64_t val = strtoll(tmp, &end, 10);
                    if (end == tmp) {
                        ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "int() invalid integer string");
                        return val_nil();
                    }
                    return val_int(val);
                }
                if (arg.kind == VAL_FLOAT) return val_int((int64_t)arg.as.floating);
                if (arg.kind == VAL_INT) return arg;
                ago_error_set(interp->ctx, AGO_ERR_TYPE, ago_loc(NULL, node->line, node->column), "int() cannot convert this type");
                return val_nil();
            }

            /* Built-in float(string) -> float */
            if (ago_str_eq(name, len, "float", 5)) {
                if (node->as.call.arg_count != 1) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "float() takes exactly 1 argument");
                    return val_nil();
                }
                AgoVal arg = eval_expr(interp, node->as.call.args[0]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                if (arg.kind == VAL_STRING) {
                    int slen; const char *sd = str_content(arg, &slen);
                    char tmp[64];
                    if (slen >= (int)sizeof(tmp)) slen = (int)sizeof(tmp) - 1;
                    memcpy(tmp, sd, (size_t)slen); tmp[slen] = '\0';
                    char *end;
                    double val = strtod(tmp, &end);
                    if (end == tmp) {
                        ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "float() invalid number string");
                        return val_nil();
                    }
                    return val_float(val);
                }
                if (arg.kind == VAL_INT) return val_float((double)arg.as.integer);
                if (arg.kind == VAL_FLOAT) return arg;
                ago_error_set(interp->ctx, AGO_ERR_TYPE, ago_loc(NULL, node->line, node->column), "float() cannot convert this type");
                return val_nil();
            }

            /* Built-in push(arr, val) -> new array */
            if (ago_str_eq(name, len, "push", 4)) {
                if (node->as.call.arg_count != 2) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "push() takes exactly 2 arguments");
                    return val_nil();
                }
                AgoVal arr_val = eval_expr(interp, node->as.call.args[0]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                AgoVal elem = eval_expr(interp, node->as.call.args[1]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                if (arr_val.kind != VAL_ARRAY) {
                    ago_error_set(interp->ctx, AGO_ERR_TYPE, ago_loc(NULL, node->line, node->column), "push() first argument must be an array");
                    return val_nil();
                }
                AgoArrayVal *old = arr_val.as.array;
                if (old->count >= MAX_ARRAY_SIZE) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "array size limit exceeded (max %d)", MAX_ARRAY_SIZE);
                    return val_nil();
                }
                int new_count = old->count + 1;
                AgoArrayVal *arr = ago_gc_alloc(interp->gc, sizeof(AgoArrayVal), array_cleanup);
                if (!arr) { ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "out of memory"); return val_nil(); }
                arr->count = new_count;
                arr->elements = malloc(sizeof(AgoVal) * (size_t)new_count);
                if (!arr->elements) { ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "out of memory"); return val_nil(); }
                memcpy(arr->elements, old->elements, sizeof(AgoVal) * (size_t)old->count);
                arr->elements[old->count] = elem;
                AgoVal v; v.kind = VAL_ARRAY; v.as.array = arr;
                return v;
            }

            /* Built-in map(arr, fn) -> new array */
            if (ago_str_eq(name, len, "map", 3) && node->as.call.arg_count == 2) {
                AgoVal arr_val = eval_expr(interp, node->as.call.args[0]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                AgoVal fn_val = eval_expr(interp, node->as.call.args[1]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                if (arr_val.kind != VAL_ARRAY || fn_val.kind != VAL_FN) {
                    ago_error_set(interp->ctx, AGO_ERR_TYPE, ago_loc(NULL, node->line, node->column), "map() requires (array, fn)");
                    return val_nil();
                }
                AgoArrayVal *src = arr_val.as.array;
                AgoArrayVal *dst = ago_gc_alloc(interp->gc, sizeof(AgoArrayVal), array_cleanup);
                if (!dst) { ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "out of memory"); return val_nil(); }
                dst->count = src->count;
                dst->elements = src->count > 0 ? malloc(sizeof(AgoVal) * (size_t)src->count) : NULL;
                for (int i = 0; i < src->count; i++) {
                    dst->elements[i] = call_fn_direct(interp, fn_val.as.fn,
                                                      &src->elements[i], 1,
                                                      node->line, node->column);
                    if (ago_error_occurred(interp->ctx)) return val_nil();
                }
                AgoVal v; v.kind = VAL_ARRAY; v.as.array = dst;
                return v;
            }

            /* Built-in filter(arr, fn) -> new array */
            if (ago_str_eq(name, len, "filter", 6) && node->as.call.arg_count == 2) {
                AgoVal arr_val = eval_expr(interp, node->as.call.args[0]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                AgoVal fn_val = eval_expr(interp, node->as.call.args[1]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                if (arr_val.kind != VAL_ARRAY || fn_val.kind != VAL_FN) {
                    ago_error_set(interp->ctx, AGO_ERR_TYPE, ago_loc(NULL, node->line, node->column), "filter() requires (array, fn)");
                    return val_nil();
                }
                AgoArrayVal *src = arr_val.as.array;
                /* Heap-allocate temp buffer to avoid stack overflow */
                AgoVal *temp = src->count > 0 ? malloc(sizeof(AgoVal) * (size_t)src->count) : NULL;
                int kept = 0;
                for (int i = 0; i < src->count; i++) {
                    AgoVal pred = call_fn_direct(interp, fn_val.as.fn,
                                                 &src->elements[i], 1,
                                                 node->line, node->column);
                    if (ago_error_occurred(interp->ctx)) { free(temp); return val_nil(); }
                    if (is_truthy(pred)) temp[kept++] = src->elements[i];
                }
                AgoArrayVal *dst = ago_gc_alloc(interp->gc, sizeof(AgoArrayVal), array_cleanup);
                if (!dst) { free(temp); ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "out of memory"); return val_nil(); }
                dst->count = kept;
                dst->elements = kept > 0 ? malloc(sizeof(AgoVal) * (size_t)kept) : NULL;
                if (kept > 0) memcpy(dst->elements, temp, sizeof(AgoVal) * (size_t)kept);
                free(temp);
                AgoVal v; v.kind = VAL_ARRAY; v.as.array = dst;
                return v;
            }

            /* Built-in abs(n) */
            if (ago_str_eq(name, len, "abs", 3)) {
                if (node->as.call.arg_count != 1) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "abs() takes exactly 1 argument");
                    return val_nil();
                }
                AgoVal arg = eval_expr(interp, node->as.call.args[0]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                if (arg.kind == VAL_INT) return val_int(arg.as.integer < 0 ? -arg.as.integer : arg.as.integer);
                if (arg.kind == VAL_FLOAT) return val_float(arg.as.floating < 0 ? -arg.as.floating : arg.as.floating);
                ago_error_set(interp->ctx, AGO_ERR_TYPE, ago_loc(NULL, node->line, node->column), "abs() requires a number");
                return val_nil();
            }

            /* Built-in min(a, b) */
            if (ago_str_eq(name, len, "min", 3)) {
                if (node->as.call.arg_count != 2) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "min() takes exactly 2 arguments");
                    return val_nil();
                }
                AgoVal a = eval_expr(interp, node->as.call.args[0]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                AgoVal b = eval_expr(interp, node->as.call.args[1]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                if (a.kind == VAL_INT && b.kind == VAL_INT) return a.as.integer <= b.as.integer ? a : b;
                if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) return a.as.floating <= b.as.floating ? a : b;
                ago_error_set(interp->ctx, AGO_ERR_TYPE, ago_loc(NULL, node->line, node->column), "min() requires two numbers of the same type");
                return val_nil();
            }

            /* Built-in max(a, b) */
            if (ago_str_eq(name, len, "max", 3)) {
                if (node->as.call.arg_count != 2) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "max() takes exactly 2 arguments");
                    return val_nil();
                }
                AgoVal a = eval_expr(interp, node->as.call.args[0]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                AgoVal b = eval_expr(interp, node->as.call.args[1]);
                if (ago_error_occurred(interp->ctx)) return val_nil();
                if (a.kind == VAL_INT && b.kind == VAL_INT) return a.as.integer >= b.as.integer ? a : b;
                if (a.kind == VAL_FLOAT && b.kind == VAL_FLOAT) return a.as.floating >= b.as.floating ? a : b;
                ago_error_set(interp->ctx, AGO_ERR_TYPE, ago_loc(NULL, node->line, node->column), "max() requires two numbers of the same type");
                return val_nil();
            }
        }

        /* Evaluate callee as expression (handles variables, lambdas, etc.) */
        AgoVal callee = eval_expr(interp, node->as.call.callee);
        if (ago_error_occurred(interp->ctx)) return val_nil();

        if (callee.kind == VAL_FN) {
            return call_user_fn(interp, callee.as.fn, node);
        }

        /* Not callable */
        if (node->as.call.callee->kind == AGO_NODE_IDENT) {
            ago_error_set(interp->ctx, AGO_ERR_NAME,
                          ago_loc(NULL, node->line, node->column),
                          "unknown function '%.*s'",
                          node->as.call.callee->as.ident.length,
                          node->as.call.callee->as.ident.name);
        } else {
            ago_error_set(interp->ctx, AGO_ERR_TYPE,
                          ago_loc(NULL, node->line, node->column),
                          "expression is not callable");
        }
        return val_nil();
    }

    default:
        ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                      ago_loc(NULL, node->line, node->column),
                      "unsupported expression type");
        return val_nil();
    }
}

/* ---- Statement execution ---- */

static void exec_stmt(AgoInterp *interp, AgoNode *node) {
    if (!node || ago_error_occurred(interp->ctx)) return;

    /* GC: collect at statement boundaries when threshold exceeded */
    if (ago_gc_should_collect(interp->gc)) {
        gc_collect(interp);
    }

    switch (node->kind) {
    case AGO_NODE_EXPR_STMT:
        eval_expr(interp, node->as.expr_stmt.expr);
        break;

    case AGO_NODE_ASSIGN_STMT: {
        AgoVal val = eval_expr(interp, node->as.assign_stmt.value);
        if (ago_error_occurred(interp->ctx)) return;
        int rc = env_assign(&interp->env, node->as.assign_stmt.name,
                            node->as.assign_stmt.name_length, val);
        if (rc == 1) {
            ago_error_set(interp->ctx, AGO_ERR_NAME,
                          ago_loc(NULL, node->line, node->column),
                          "undefined variable '%.*s'",
                          node->as.assign_stmt.name_length,
                          node->as.assign_stmt.name);
        } else if (rc == 2) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column),
                          "cannot assign to immutable variable '%.*s'",
                          node->as.assign_stmt.name_length,
                          node->as.assign_stmt.name);
        }
        break;
    }

    case AGO_NODE_LET_STMT:
    case AGO_NODE_VAR_STMT: {
        AgoVal val = val_nil();
        if (node->as.var_decl.initializer) {
            val = eval_expr(interp, node->as.var_decl.initializer);
        }
        if (ago_error_occurred(interp->ctx)) return;
        bool immut = (node->kind == AGO_NODE_LET_STMT);
        if (!env_define(&interp->env, node->as.var_decl.name,
                        node->as.var_decl.name_length, val, immut)) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column),
                          "too many variables (max %d)", MAX_VARS);
        }
        break;
    }

    case AGO_NODE_RETURN_STMT: {
        if (node->as.return_stmt.value) {
            interp->return_value = eval_expr(interp, node->as.return_stmt.value);
        } else {
            interp->return_value = val_nil();
        }
        interp->has_return = true;
        if (interp->return_jmp_set) {
            longjmp(interp->return_jmp, 1);
        }
        break;
    }

    case AGO_NODE_IF_STMT: {
        AgoVal cond = eval_expr(interp, node->as.if_stmt.condition);
        if (ago_error_occurred(interp->ctx)) return;
        if (is_truthy(cond)) {
            exec_stmt(interp, node->as.if_stmt.then_block);
        } else if (node->as.if_stmt.else_block) {
            exec_stmt(interp, node->as.if_stmt.else_block);
        }
        break;
    }

    case AGO_NODE_WHILE_STMT: {
        for (;;) {
            AgoVal cond = eval_expr(interp, node->as.while_stmt.condition);
            if (ago_error_occurred(interp->ctx)) return;
            if (!is_truthy(cond)) break;
            exec_stmt(interp, node->as.while_stmt.body);
            if (ago_error_occurred(interp->ctx)) return;
            if (interp->has_return) return;
        }
        break;
    }

    case AGO_NODE_FOR_STMT: {
        AgoVal iterable = eval_expr(interp, node->as.for_stmt.iterable);
        if (ago_error_occurred(interp->ctx)) return;
        if (iterable.kind != VAL_ARRAY) {
            ago_error_set(interp->ctx, AGO_ERR_TYPE,
                          ago_loc(NULL, node->line, node->column),
                          "for-in requires an array");
            return;
        }
        int saved = interp->env.count;
        /* Define loop variable */
        if (!env_define(&interp->env, node->as.for_stmt.var_name,
                        node->as.for_stmt.var_name_length, val_nil(), false)) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column),
                          "too many variables (max %d)", MAX_VARS);
            return;
        }
        int var_idx = interp->env.count - 1;
        for (int i = 0; i < iterable.as.array->count; i++) {
            interp->env.values[var_idx] = iterable.as.array->elements[i];
            exec_stmt(interp, node->as.for_stmt.body);
            if (ago_error_occurred(interp->ctx)) return;
            if (interp->has_return) return;
        }
        interp->env.count = saved;
        break;
    }

    case AGO_NODE_STRUCT_DECL:
        break;

    case AGO_NODE_IMPORT: {
        /* Resolve path relative to current file — rejects path traversal */
        char resolved[512];
        if (!resolve_import(interp->file,
                            node->as.import_stmt.path,
                            node->as.import_stmt.path_length,
                            resolved, sizeof(resolved))) {
            ago_error_set(interp->ctx, AGO_ERR_IO,
                          ago_loc(NULL, node->line, node->column),
                          "invalid import path '%.*s'",
                          node->as.import_stmt.path_length,
                          node->as.import_stmt.path);
            return;
        }

        /* Skip if already loaded */
        if (module_loaded(interp, resolved)) break;

        /* Read module file */
        char *mod_source = read_file(resolved);
        if (!mod_source) {
            ago_error_set(interp->ctx, AGO_ERR_IO,
                          ago_loc(NULL, node->line, node->column),
                          "cannot open module '%.*s'",
                          node->as.import_stmt.path_length,
                          node->as.import_stmt.path);
            return;
        }

        /* Parse module */
        AgoArena *mod_arena = ago_arena_new();
        if (!mod_arena) { free(mod_source); return; }

        AgoParser mod_parser;
        ago_parser_init(&mod_parser, mod_source, resolved, mod_arena, interp->ctx);
        AgoNode *mod_program = ago_parser_parse(&mod_parser);

        if (!mod_program || ago_error_occurred(interp->ctx)) {
            ago_arena_free(mod_arena);
            free(mod_source);
            return;
        }

        /* Run sema on module (skip if it has imports — same reason as main) */
        bool mod_has_imports = false;
        for (int i = 0; i < mod_program->as.program.decl_count; i++) {
            if (mod_program->as.program.decls[i] &&
                mod_program->as.program.decls[i]->kind == AGO_NODE_IMPORT) {
                mod_has_imports = true;
                break;
            }
        }
        if (!mod_has_imports) {
            AgoSema *mod_sema = ago_sema_new(interp->ctx, mod_arena);
            if (mod_sema) {
                ago_sema_check(mod_sema, mod_program);
                ago_sema_free(mod_sema);
            }
            if (ago_error_occurred(interp->ctx)) {
                ago_arena_free(mod_arena);
                free(mod_source);
                return;
            }
        }

        /* Register module — takes ownership of source and arena.
         * Must register before execution to prevent circular imports. */
        if (!module_register(interp, resolved, mod_source, mod_arena)) {
            ago_arena_free(mod_arena);
            free(mod_source);
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column),
                          "too many modules (max %d)", MAX_MODULES);
            return;
        }

        /* Execute module in current interpreter (shares env, gc) */
        const char *saved_file = interp->file;
        interp->file = resolved;
        for (int i = 0; i < mod_program->as.program.decl_count; i++) {
            exec_stmt(interp, mod_program->as.program.decls[i]);
            if (ago_error_occurred(interp->ctx)) break;
        }
        interp->file = saved_file;
        break;
    }

    case AGO_NODE_FN_DECL: {
        /* Register function in environment */
        AgoFnVal *fn = ago_gc_alloc(interp->gc, sizeof(AgoFnVal), fn_cleanup);
        if (!fn) { ago_error_set(interp->ctx, AGO_ERR_RUNTIME, ago_loc(NULL, node->line, node->column), "out of memory"); return; }
        fn->decl = node;
        fn->captured_count = 0;
        fn->captured_names = NULL;
        fn->captured_name_lengths = NULL;
        fn->captured_values = NULL;
        fn->captured_immutable = NULL;
        AgoVal fn_val;
        fn_val.kind = VAL_FN;
        fn_val.as.fn = fn;
        if (!env_define(&interp->env, node->as.fn_decl.name,
                        node->as.fn_decl.name_length, fn_val, true)) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column),
                          "too many variables (max %d)", MAX_VARS);
        }
        break;
    }

    case AGO_NODE_BLOCK:
        for (int i = 0; i < node->as.block.stmt_count; i++) {
            exec_stmt(interp, node->as.block.stmts[i]);
            if (ago_error_occurred(interp->ctx)) return;
            if (interp->has_return) return;
        }
        break;

    default:
        ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                      ago_loc(NULL, node->line, node->column),
                      "unsupported statement type");
        break;
    }
}

/* ---- Public API ---- */

int ago_interpret(AgoNode *program, const char *filename, AgoCtx *ctx) {
    if (!program || program->kind != AGO_NODE_PROGRAM) return -1;

    AgoArena *runtime_arena = ago_arena_new();
    if (!runtime_arena) return -1;

    AgoGc *gc = ago_gc_new();
    if (!gc) { ago_arena_free(runtime_arena); return -1; }

    AgoInterp interp;
    env_init(&interp.env);
    interp.ctx = ctx;
    interp.arena = runtime_arena;
    interp.gc = gc;
    interp.file = filename ? filename : "<stdin>";
    interp.module_count = 0;
    interp.has_return = false;
    interp.return_value = val_nil();
    interp.return_jmp_set = false;
    interp.call_depth = 0;

    for (int i = 0; i < program->as.program.decl_count; i++) {
        exec_stmt(&interp, program->as.program.decls[i]);
        if (ago_error_occurred(ctx)) {
            module_cache_free(&interp);
            ago_gc_free(gc);
            ago_arena_free(runtime_arena);
            return -1;
        }
    }

    module_cache_free(&interp);
    ago_gc_free(gc);
    ago_arena_free(runtime_arena);
    return 0;
}

int ago_run(const char *source, const char *filename, AgoCtx *ctx) {
    AgoArena *arena = ago_arena_new();
    if (!arena) return -1;

    AgoParser parser;
    ago_parser_init(&parser, source, filename, arena, ctx);
    AgoNode *program = ago_parser_parse(&parser);

    int result = -1;
    if (program && !ago_error_occurred(ctx)) {
        /* Semantic analysis — skip for files with imports since imported
         * names aren't available at sema time. Each module gets sema
         * individually when loaded. */
        bool has_imports = false;
        for (int i = 0; i < program->as.program.decl_count; i++) {
            if (program->as.program.decls[i] &&
                program->as.program.decls[i]->kind == AGO_NODE_IMPORT) {
                has_imports = true;
                break;
            }
        }
        if (!has_imports) {
            AgoSema *sema = ago_sema_new(ctx, arena);
            if (sema) {
                ago_sema_check(sema, program);
                ago_sema_free(sema);
            }
        }
        /* Interpret only if sema passed */
        if (!ago_error_occurred(ctx)) {
            result = ago_interpret(program, filename, ctx);
        }
    }

    ago_arena_free(arena);
    return result;
}
