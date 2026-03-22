#include "interpreter.h"
#include "parser.h"
#include "arena.h"

/* ---- Runtime Value ---- */

typedef enum {
    VAL_INT,
    VAL_FLOAT,
    VAL_BOOL,
    VAL_STRING,
    VAL_NIL,
} AgoValKind;

typedef struct {
    AgoValKind kind;
    union {
        int64_t integer;
        double floating;
        bool boolean;
        struct { const char *data; int length; } string;
    } as;
} AgoVal;

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

/* ---- Environment (variable bindings) ---- */

#define MAX_VARS 256

typedef struct {
    const char *names[MAX_VARS];
    int name_lengths[MAX_VARS];
    AgoVal values[MAX_VARS];
    int count;
} AgoEnv;

static void env_init(AgoEnv *env) {
    env->count = 0;
}

static bool env_set(AgoEnv *env, const char *name, int length, AgoVal val) {
    if (env->count >= MAX_VARS) return false;
    env->names[env->count] = name;
    env->name_lengths[env->count] = length;
    env->values[env->count] = val;
    env->count++;
    return true;
}

static AgoVal *env_get(AgoEnv *env, const char *name, int length) {
    /* Search backward for most recent binding */
    for (int i = env->count - 1; i >= 0; i--) {
        if (ago_str_eq(env->names[i], env->name_lengths[i], name, length)) {
            return &env->values[i];
        }
    }
    return NULL;
}

/* ---- Interpreter state ---- */

typedef struct {
    AgoEnv env;
    AgoCtx *ctx;
} AgoInterp;

/* Forward declarations */
static AgoVal eval_expr(AgoInterp *interp, AgoNode *node);
static void exec_stmt(AgoInterp *interp, AgoNode *node);

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
        /* String value includes quotes from token — print without quotes */
        if (val.as.string.length >= 2 && val.as.string.data[0] == '"') {
            printf("%.*s\n", val.as.string.length - 2, val.as.string.data + 1);
        } else {
            printf("%.*s\n", val.as.string.length, val.as.string.data);
        }
        break;
    case VAL_NIL:
        printf("nil\n");
        break;
    }
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
        AgoVal left = eval_expr(interp, node->as.binary.left);
        if (ago_error_occurred(interp->ctx)) return val_nil();
        AgoVal right = eval_expr(interp, node->as.binary.right);
        if (ago_error_occurred(interp->ctx)) return val_nil();

        AgoTokenKind op = node->as.binary.op;

        /* Integer arithmetic */
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

        /* Boolean logic */
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

    case AGO_NODE_CALL: {
        /* Currently only built-in print */
        if (node->as.call.callee->kind == AGO_NODE_IDENT) {
            const char *name = node->as.call.callee->as.ident.name;
            int len = node->as.call.callee->as.ident.length;

            if (ago_str_eq(name, len, "print", 5)) {
                for (int i = 0; i < node->as.call.arg_count; i++) {
                    AgoVal arg = eval_expr(interp, node->as.call.args[i]);
                    if (ago_error_occurred(interp->ctx)) return val_nil();
                    builtin_print(arg);
                }
                return val_nil();
            }
        }
        ago_error_set(interp->ctx, AGO_ERR_NAME,
                      ago_loc(NULL, node->line, node->column),
                      "unknown function");
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

    case AGO_NODE_LET_STMT:
    case AGO_NODE_VAR_STMT: {
        AgoVal val = val_nil();
        if (node->as.var_decl.initializer) {
            val = eval_expr(interp, node->as.var_decl.initializer);
        }
        if (ago_error_occurred(interp->ctx)) return;
        if (!env_set(&interp->env, node->as.var_decl.name,
                     node->as.var_decl.name_length, val)) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column),
                          "too many variables (max %d)", MAX_VARS);
            return;
        }
        break;
    }

    case AGO_NODE_BLOCK:
        for (int i = 0; i < node->as.block.stmt_count; i++) {
            exec_stmt(interp, node->as.block.stmts[i]);
            if (ago_error_occurred(interp->ctx)) return;
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
