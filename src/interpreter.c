#include "interpreter.h"
#include "parser.h"
#include "arena.h"
#include <setjmp.h>

/* ---- Runtime Value ---- */

typedef enum {
    VAL_INT,
    VAL_FLOAT,
    VAL_BOOL,
    VAL_STRING,
    VAL_FN,
    VAL_ARRAY,
    VAL_STRUCT,
    VAL_NIL,
} AgoValKind;

typedef struct AgoFnVal AgoFnVal;
typedef struct AgoArrayVal AgoArrayVal;
typedef struct AgoStructVal AgoStructVal;

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
    } as;
} AgoVal;

struct AgoFnVal {
    AgoNode *decl;
};

#define MAX_ARRAY_SIZE 1024

struct AgoArrayVal {
    AgoVal *elements;
    int count;
};

#define MAX_STRUCT_FIELDS 64

struct AgoStructVal {
    const char *type_name;
    int type_name_length;
    const char *field_names[MAX_STRUCT_FIELDS];
    int field_name_lengths[MAX_STRUCT_FIELDS];
    AgoVal field_values[MAX_STRUCT_FIELDS];
    int field_count;
};

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

typedef struct {
    AgoEnv env;
    AgoCtx *ctx;
    /* Return mechanism */
    bool has_return;
    AgoVal return_value;
    jmp_buf return_jmp;
    bool return_jmp_set;
} AgoInterp;

/* Forward declarations */
static AgoVal eval_expr(AgoInterp *interp, AgoNode *node);
static void exec_stmt(AgoInterp *interp, AgoNode *node);

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
    }
    return false;
}

/* ---- Built-in: print ---- */

static void builtin_print(AgoVal val) {
    switch (val.kind) {
    case VAL_INT:
        printf("%lld\n", (long long)val.as.integer);
        break;
    case VAL_FLOAT:
        printf("%g\n", val.as.floating);
        break;
    case VAL_BOOL:
        printf("%s\n", val.as.boolean ? "true" : "false");
        break;
    case VAL_STRING:
        if (val.as.string.length >= 2 && val.as.string.data[0] == '"') {
            printf("%.*s\n", val.as.string.length - 2, val.as.string.data + 1);
        } else {
            printf("%.*s\n", val.as.string.length, val.as.string.data);
        }
        break;
    case VAL_NIL:
        printf("nil\n");
        break;
    case VAL_FN:
        printf("<fn>\n");
        break;
    case VAL_ARRAY: {
        printf("[");
        for (int i = 0; i < val.as.array->count; i++) {
            if (i > 0) printf(", ");
            AgoVal elem = val.as.array->elements[i];
            switch (elem.kind) {
            case VAL_INT:    printf("%lld", (long long)elem.as.integer); break;
            case VAL_FLOAT:  printf("%g", elem.as.floating); break;
            case VAL_BOOL:   printf("%s", elem.as.boolean ? "true" : "false"); break;
            case VAL_STRING:
                if (elem.as.string.length >= 2 && elem.as.string.data[0] == '"')
                    printf("\"%.*s\"", elem.as.string.length - 2, elem.as.string.data + 1);
                break;
            default: printf("..."); break;
            }
        }
        printf("]\n");
        break;
    }
    case VAL_STRUCT:
        printf("<struct %.*s>\n", val.as.strct->type_name_length, val.as.strct->type_name);
        break;
    }
}

/* ---- Call user function ---- */

static AgoVal call_user_fn(AgoInterp *interp, AgoFnVal *fn, AgoNode *call_node) {
    AgoNode *decl = fn->decl;
    int param_count = decl->as.fn_decl.param_count;
    int arg_count = call_node->as.call.arg_count;

    if (arg_count != param_count) {
        ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                      ago_loc(NULL, call_node->line, call_node->column),
                      "expected %d arguments, got %d", param_count, arg_count);
        return val_nil();
    }

    /* Evaluate arguments before modifying env */
    AgoVal args[64];
    for (int i = 0; i < arg_count && i < 64; i++) {
        args[i] = eval_expr(interp, call_node->as.call.args[i]);
        if (ago_error_occurred(interp->ctx)) return val_nil();
    }

    /* Save env state for restoration after call */
    int saved_count = interp->env.count;

    /* Bind parameters */
    for (int i = 0; i < param_count; i++) {
        env_define(&interp->env,
                   decl->as.fn_decl.param_names[i],
                   decl->as.fn_decl.param_name_lengths[i],
                   args[i], true);
    }

    /* Execute body with return support */
    interp->has_return = false;
    interp->return_value = val_nil();

    bool prev_jmp_set = interp->return_jmp_set;
    jmp_buf prev_jmp;
    if (prev_jmp_set) memcpy(prev_jmp, interp->return_jmp, sizeof(jmp_buf));

    interp->return_jmp_set = true;
    if (setjmp(interp->return_jmp) == 0) {
        /* Normal execution */
        if (decl->as.fn_decl.body) {
            exec_stmt(interp, decl->as.fn_decl.body);
        }
    }
    /* If we get here via longjmp, return_value is already set */

    AgoVal result = interp->return_value;
    interp->has_return = false;

    /* Restore previous return jmp */
    interp->return_jmp_set = prev_jmp_set;
    if (prev_jmp_set) memcpy(interp->return_jmp, prev_jmp, sizeof(jmp_buf));

    /* Restore env */
    interp->env.count = saved_count;

    return result;
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

        ago_error_set(interp->ctx, AGO_ERR_TYPE,
                      ago_loc(NULL, node->line, node->column),
                      "invalid binary operation");
        return val_nil();
    }

    case AGO_NODE_ARRAY_LIT: {
        static AgoArrayVal arr_storage[256];
        static AgoVal elem_storage[4096];
        static int arr_idx = 0;
        static int elem_idx = 0;
        if (arr_idx >= 256) { arr_idx = 0; } /* wrap for simplicity */
        AgoArrayVal *arr = &arr_storage[arr_idx++];
        arr->count = node->as.array_lit.count;
        arr->elements = &elem_storage[elem_idx];
        for (int i = 0; i < arr->count; i++) {
            elem_storage[elem_idx++] = eval_expr(interp, node->as.array_lit.elements[i]);
            if (ago_error_occurred(interp->ctx)) return val_nil();
            if (elem_idx >= 4096) elem_idx = 0;
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
        static AgoStructVal struct_storage[128];
        static int struct_idx = 0;
        if (struct_idx >= 128) struct_idx = 0;
        AgoStructVal *s = &struct_storage[struct_idx++];
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

    case AGO_NODE_CALL: {
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
                if (arg.kind == VAL_STRING) return val_int(arg.as.string.length - 2);
                ago_error_set(interp->ctx, AGO_ERR_TYPE,
                              ago_loc(NULL, node->line, node->column),
                              "len() requires an array or string");
                return val_nil();
            }

            /* User-defined function */
            AgoVal *fn_val = env_get(&interp->env, name, len);
            if (fn_val && fn_val->kind == VAL_FN) {
                return call_user_fn(interp, fn_val->as.fn, node);
            }
        }
        ago_error_set(interp->ctx, AGO_ERR_NAME,
                      ago_loc(NULL, node->line, node->column),
                      "unknown function '%.*s'",
                      node->as.call.callee->as.ident.length,
                      node->as.call.callee->as.ident.name);
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
        env_define(&interp->env, node->as.for_stmt.var_name,
                   node->as.for_stmt.var_name_length, val_nil(), false);
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
        /* Struct declarations are type definitions — no runtime action needed yet */
        break;

    case AGO_NODE_FN_DECL: {
        /* Register function in environment */
        static AgoFnVal fn_storage[64];
        static int fn_count = 0;
        if (fn_count >= 64) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column),
                          "too many function definitions");
            return;
        }
        fn_storage[fn_count].decl = node;
        AgoVal fn_val;
        fn_val.kind = VAL_FN;
        fn_val.as.fn = &fn_storage[fn_count];
        fn_count++;
        env_define(&interp->env, node->as.fn_decl.name,
                   node->as.fn_decl.name_length, fn_val, true);
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

int ago_interpret(AgoNode *program, AgoCtx *ctx) {
    if (!program || program->kind != AGO_NODE_PROGRAM) return -1;

    AgoInterp interp;
    env_init(&interp.env);
    interp.ctx = ctx;
    interp.has_return = false;
    interp.return_value = val_nil();
    interp.return_jmp_set = false;

    for (int i = 0; i < program->as.program.decl_count; i++) {
        exec_stmt(&interp, program->as.program.decls[i]);
        if (ago_error_occurred(ctx)) return -1;
    }

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
        result = ago_interpret(program, ctx);
    }

    ago_arena_free(arena);
    return result;
}
