#include "sema.h"
#include "arena.h"
#include <stdlib.h>
#include <string.h>

/* ---- Scope ---- */

#define MAX_SCOPE_VARS 256

typedef struct {
    const char *name;
    int name_length;
    bool is_mutable;    /* true for var, false for let/param */
    bool is_function;
    int arity;          /* valid only if is_function */
} AgoSemaVar;

typedef struct AgoScope {
    struct AgoScope *parent;
    AgoSemaVar vars[MAX_SCOPE_VARS];
    int var_count;
} AgoScope;

struct AgoSema {
    AgoCtx *ctx;
    AgoArena *arena;
    AgoScope *scope;
};

/* ---- Scope helpers ---- */

static AgoScope *scope_new(AgoSema *sema, AgoScope *parent) {
    AgoScope *s = ago_arena_alloc(sema->arena, sizeof(AgoScope));
    if (!s) return NULL;
    s->parent = parent;
    s->var_count = 0;
    return s;
}

static bool scope_push(AgoSema *sema) {
    AgoScope *s = scope_new(sema, sema->scope);
    if (!s) {
        ago_error_set(sema->ctx, AGO_ERR_RUNTIME,
                      ago_loc(NULL, 0, 0), "out of memory");
        return false;
    }
    sema->scope = s;
    return true;
}

static void scope_pop(AgoSema *sema) {
    if (sema->scope && sema->scope->parent) {
        sema->scope = sema->scope->parent;
    }
}

static bool scope_define(AgoScope *scope, const char *name, int length,
                         bool is_mutable, bool is_function, int arity) {
    if (scope->var_count >= MAX_SCOPE_VARS) return false;
    AgoSemaVar *v = &scope->vars[scope->var_count++];
    v->name = name;
    v->name_length = length;
    v->is_mutable = is_mutable;
    v->is_function = is_function;
    v->arity = arity;
    return true;
}

/* Look up a variable through the scope chain. Returns NULL if not found. */
static AgoSemaVar *scope_lookup(AgoScope *scope, const char *name, int length) {
    for (AgoScope *s = scope; s; s = s->parent) {
        for (int i = s->var_count - 1; i >= 0; i--) {
            if (ago_str_eq(s->vars[i].name, s->vars[i].name_length,
                           name, length)) {
                return &s->vars[i];
            }
        }
    }
    return NULL;
}

/* ---- AST walking ---- */

static void check_expr(AgoSema *sema, AgoNode *node);
static void check_stmt(AgoSema *sema, AgoNode *node);

static void check_expr(AgoSema *sema, AgoNode *node) {
    if (!node || ago_error_occurred(sema->ctx)) return;

    switch (node->kind) {
    case AGO_NODE_INT_LIT:
    case AGO_NODE_FLOAT_LIT:
    case AGO_NODE_STRING_LIT:
    case AGO_NODE_BOOL_LIT:
        break;

    case AGO_NODE_IDENT: {
        AgoSemaVar *v = scope_lookup(sema->scope,
                                     node->as.ident.name,
                                     node->as.ident.length);
        if (!v) {
            ago_error_set(sema->ctx, AGO_ERR_NAME,
                          ago_loc(NULL, node->line, node->column),
                          "undefined variable '%.*s'",
                          node->as.ident.length, node->as.ident.name);
        }
        break;
    }

    case AGO_NODE_UNARY:
        check_expr(sema, node->as.unary.operand);
        break;

    case AGO_NODE_BINARY:
        check_expr(sema, node->as.binary.left);
        if (node->as.binary.op != AGO_TOKEN_DOT) {
            check_expr(sema, node->as.binary.right);
        }
        /* DOT right side is a field name, not a variable */
        break;

    case AGO_NODE_CALL: {
        check_expr(sema, node->as.call.callee);
        for (int i = 0; i < node->as.call.arg_count; i++) {
            check_expr(sema, node->as.call.args[i]);
        }
        /* Arity check for known functions */
        if (node->as.call.callee->kind == AGO_NODE_IDENT &&
            !ago_error_occurred(sema->ctx)) {
            AgoSemaVar *fn = scope_lookup(sema->scope,
                                          node->as.call.callee->as.ident.name,
                                          node->as.call.callee->as.ident.length);
            if (fn && fn->is_function &&
                node->as.call.arg_count != fn->arity) {
                ago_error_set(sema->ctx, AGO_ERR_TYPE,
                              ago_loc(NULL, node->line, node->column),
                              "expected %d arguments, got %d",
                              fn->arity, node->as.call.arg_count);
            }
        }
        break;
    }

    case AGO_NODE_INDEX:
        check_expr(sema, node->as.index_expr.object);
        check_expr(sema, node->as.index_expr.index);
        break;

    case AGO_NODE_ARRAY_LIT:
        for (int i = 0; i < node->as.array_lit.count; i++) {
            check_expr(sema, node->as.array_lit.elements[i]);
        }
        break;

    case AGO_NODE_STRUCT_LIT:
        for (int i = 0; i < node->as.struct_lit.field_count; i++) {
            check_expr(sema, node->as.struct_lit.field_values[i]);
        }
        break;

    case AGO_NODE_LAMBDA:
        scope_push(sema);
        for (int i = 0; i < node->as.fn_decl.param_count; i++) {
            scope_define(sema->scope,
                         node->as.fn_decl.param_names[i],
                         node->as.fn_decl.param_name_lengths[i],
                         false, false, 0);
        }
        if (node->as.fn_decl.body) {
            check_stmt(sema, node->as.fn_decl.body);
        }
        scope_pop(sema);
        break;

    case AGO_NODE_RESULT_OK:
    case AGO_NODE_RESULT_ERR:
        check_expr(sema, node->as.result_val.value);
        break;

    case AGO_NODE_MATCH_EXPR:
        check_expr(sema, node->as.match_expr.subject);
        /* ok arm: bind name in a temporary scope */
        scope_push(sema);
        scope_define(sema->scope,
                     node->as.match_expr.ok_name,
                     node->as.match_expr.ok_name_length,
                     false, false, 0);
        check_expr(sema, node->as.match_expr.ok_body);
        scope_pop(sema);
        /* err arm */
        scope_push(sema);
        scope_define(sema->scope,
                     node->as.match_expr.err_name,
                     node->as.match_expr.err_name_length,
                     false, false, 0);
        check_expr(sema, node->as.match_expr.err_body);
        scope_pop(sema);
        break;

    default:
        /* Statement nodes handled in check_stmt; future expression nodes
         * must be added here or sema will silently pass unchecked code. */
        break;
    }
}

static void check_stmt(AgoSema *sema, AgoNode *node) {
    if (!node || ago_error_occurred(sema->ctx)) return;

    switch (node->kind) {
    case AGO_NODE_EXPR_STMT:
        check_expr(sema, node->as.expr_stmt.expr);
        break;

    case AGO_NODE_LET_STMT:
    case AGO_NODE_VAR_STMT:
        if (node->as.var_decl.initializer) {
            check_expr(sema, node->as.var_decl.initializer);
        }
        if (!ago_error_occurred(sema->ctx)) {
            scope_define(sema->scope,
                         node->as.var_decl.name,
                         node->as.var_decl.name_length,
                         node->kind == AGO_NODE_VAR_STMT,
                         false, 0);
        }
        break;

    case AGO_NODE_ASSIGN_STMT: {
        check_expr(sema, node->as.assign_stmt.value);
        if (ago_error_occurred(sema->ctx)) break;
        AgoSemaVar *v = scope_lookup(sema->scope,
                                     node->as.assign_stmt.name,
                                     node->as.assign_stmt.name_length);
        if (!v) {
            ago_error_set(sema->ctx, AGO_ERR_NAME,
                          ago_loc(NULL, node->line, node->column),
                          "undefined variable '%.*s'",
                          node->as.assign_stmt.name_length,
                          node->as.assign_stmt.name);
        } else if (!v->is_mutable) {
            ago_error_set(sema->ctx, AGO_ERR_TYPE,
                          ago_loc(NULL, node->line, node->column),
                          "cannot assign to immutable variable '%.*s'",
                          node->as.assign_stmt.name_length,
                          node->as.assign_stmt.name);
        }
        break;
    }

    case AGO_NODE_RETURN_STMT:
        if (node->as.return_stmt.value) {
            check_expr(sema, node->as.return_stmt.value);
        }
        break;

    case AGO_NODE_IF_STMT:
        check_expr(sema, node->as.if_stmt.condition);
        if (node->as.if_stmt.then_block) {
            check_stmt(sema, node->as.if_stmt.then_block);
        }
        if (node->as.if_stmt.else_block) {
            check_stmt(sema, node->as.if_stmt.else_block);
        }
        break;

    case AGO_NODE_WHILE_STMT:
        check_expr(sema, node->as.while_stmt.condition);
        if (node->as.while_stmt.body) {
            check_stmt(sema, node->as.while_stmt.body);
        }
        break;

    case AGO_NODE_FOR_STMT:
        check_expr(sema, node->as.for_stmt.iterable);
        scope_push(sema);
        scope_define(sema->scope,
                     node->as.for_stmt.var_name,
                     node->as.for_stmt.var_name_length,
                     false, false, 0);
        if (node->as.for_stmt.body) {
            check_stmt(sema, node->as.for_stmt.body);
        }
        scope_pop(sema);
        break;

    case AGO_NODE_BLOCK:
        scope_push(sema);
        for (int i = 0; i < node->as.block.stmt_count; i++) {
            check_stmt(sema, node->as.block.stmts[i]);
            if (ago_error_occurred(sema->ctx)) break;
        }
        scope_pop(sema);
        break;

    case AGO_NODE_FN_DECL:
        /* Register function in current scope before checking body */
        scope_define(sema->scope,
                     node->as.fn_decl.name,
                     node->as.fn_decl.name_length,
                     false, true, node->as.fn_decl.param_count);
        /* Check body in a new scope with params */
        scope_push(sema);
        for (int i = 0; i < node->as.fn_decl.param_count; i++) {
            scope_define(sema->scope,
                         node->as.fn_decl.param_names[i],
                         node->as.fn_decl.param_name_lengths[i],
                         false, false, 0);
        }
        if (node->as.fn_decl.body) {
            check_stmt(sema, node->as.fn_decl.body);
        }
        scope_pop(sema);
        break;

    case AGO_NODE_STRUCT_DECL:
        break;

    case AGO_NODE_IMPORT:
        /* Import paths validated at runtime (file existence); sema accepts */
        break;

    default:
        break;
    }
}

/* ---- Public API ---- */

AgoSema *ago_sema_new(AgoCtx *ctx, AgoArena *arena) {
    AgoSema *sema = calloc(1, sizeof(AgoSema));
    if (!sema) return NULL;
    sema->ctx = ctx;
    sema->arena = arena;
    sema->scope = NULL;
    return sema;
}

void ago_sema_free(AgoSema *sema) {
    free(sema);
}

bool ago_sema_check(AgoSema *sema, AgoNode *program) {
    if (!sema || !program || program->kind != AGO_NODE_PROGRAM) return false;

    /* Create top-level scope with built-in functions */
    sema->scope = scope_new(sema, NULL);
    if (!sema->scope) return false;

    /* Built-in functions */
    scope_define(sema->scope, "print", 5, false, false, 0); /* variadic */
    scope_define(sema->scope, "len", 3, false, true, 1);
    scope_define(sema->scope, "type", 4, false, true, 1);
    scope_define(sema->scope, "str", 3, false, true, 1);
    scope_define(sema->scope, "int", 3, false, true, 1);
    scope_define(sema->scope, "float", 5, false, true, 1);
    scope_define(sema->scope, "push", 4, false, true, 2);
    scope_define(sema->scope, "map", 3, false, true, 2);
    scope_define(sema->scope, "filter", 6, false, true, 2);
    scope_define(sema->scope, "abs", 3, false, true, 1);
    scope_define(sema->scope, "min", 3, false, true, 2);
    scope_define(sema->scope, "max", 3, false, true, 2);
    scope_define(sema->scope, "read_file", 9, false, true, 1);
    scope_define(sema->scope, "write_file", 10, false, true, 2);
    scope_define(sema->scope, "file_exists", 11, false, true, 1);

    /* Check all top-level declarations/statements */
    for (int i = 0; i < program->as.program.decl_count; i++) {
        check_stmt(sema, program->as.program.decls[i]);
        if (ago_error_occurred(sema->ctx)) return false;
    }

    return true;
}
