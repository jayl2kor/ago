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
} AglSemaVar;

typedef struct AglScope {
    struct AglScope *parent;
    AglSemaVar vars[MAX_SCOPE_VARS];
    int var_count;
} AglScope;

struct AglSema {
    AglCtx *ctx;
    AglArena *arena;
    AglScope *scope;
};

/* ---- Scope helpers ---- */

static AglScope *scope_new(AglSema *sema, AglScope *parent) {
    AglScope *s = agl_arena_alloc(sema->arena, sizeof(AglScope));
    if (!s) return NULL;
    s->parent = parent;
    s->var_count = 0;
    return s;
}

static bool scope_push(AglSema *sema) {
    AglScope *s = scope_new(sema, sema->scope);
    if (!s) {
        agl_error_set(sema->ctx, AGL_ERR_RUNTIME,
                      agl_loc(NULL, 0, 0), "out of memory");
        return false;
    }
    sema->scope = s;
    return true;
}

static void scope_pop(AglSema *sema) {
    if (sema->scope && sema->scope->parent) {
        sema->scope = sema->scope->parent;
    }
}

static bool scope_define(AglScope *scope, const char *name, int length,
                         bool is_mutable, bool is_function, int arity) {
    if (scope->var_count >= MAX_SCOPE_VARS) return false;
    AglSemaVar *v = &scope->vars[scope->var_count++];
    v->name = name;
    v->name_length = length;
    v->is_mutable = is_mutable;
    v->is_function = is_function;
    v->arity = arity;
    return true;
}

/* Look up a variable through the scope chain. Returns NULL if not found. */
static AglSemaVar *scope_lookup(AglScope *scope, const char *name, int length) {
    for (AglScope *s = scope; s; s = s->parent) {
        for (int i = s->var_count - 1; i >= 0; i--) {
            if (agl_str_eq(s->vars[i].name, s->vars[i].name_length,
                           name, length)) {
                return &s->vars[i];
            }
        }
    }
    return NULL;
}

/* Look up a variable in the current scope only (no parent chain). */
static AglSemaVar *scope_lookup_local(AglScope *scope, const char *name, int length) {
    for (int i = scope->var_count - 1; i >= 0; i--) {
        if (agl_str_eq(scope->vars[i].name, scope->vars[i].name_length,
                       name, length)) {
            return &scope->vars[i];
        }
    }
    return NULL;
}

/* ---- AST walking ---- */

static void check_expr(AglSema *sema, AglNode *node);
static void check_stmt(AglSema *sema, AglNode *node);

static void check_expr(AglSema *sema, AglNode *node) {
    if (!node || agl_error_occurred(sema->ctx)) return;

    switch (node->kind) {
    case AGL_NODE_INT_LIT:
    case AGL_NODE_FLOAT_LIT:
    case AGL_NODE_STRING_LIT:
    case AGL_NODE_BOOL_LIT:
        break;

    case AGL_NODE_IDENT: {
        AglSemaVar *v = scope_lookup(sema->scope,
                                     node->as.ident.name,
                                     node->as.ident.length);
        if (!v) {
            agl_error_set(sema->ctx, AGL_ERR_NAME,
                          agl_loc(NULL, node->line, node->column),
                          "undefined variable '%.*s'",
                          node->as.ident.length, node->as.ident.name);
        }
        break;
    }

    case AGL_NODE_UNARY:
        check_expr(sema, node->as.unary.operand);
        break;

    case AGL_NODE_BINARY:
        check_expr(sema, node->as.binary.left);
        if (node->as.binary.op != AGL_TOKEN_DOT) {
            check_expr(sema, node->as.binary.right);
        }
        /* DOT right side is a field name, not a variable */
        break;

    case AGL_NODE_CALL: {
        check_expr(sema, node->as.call.callee);
        for (int i = 0; i < node->as.call.arg_count; i++) {
            check_expr(sema, node->as.call.args[i]);
        }
        /* Arity check for known functions */
        if (node->as.call.callee->kind == AGL_NODE_IDENT &&
            !agl_error_occurred(sema->ctx)) {
            AglSemaVar *fn = scope_lookup(sema->scope,
                                          node->as.call.callee->as.ident.name,
                                          node->as.call.callee->as.ident.length);
            if (fn && fn->is_function &&
                node->as.call.arg_count != fn->arity) {
                agl_error_set(sema->ctx, AGL_ERR_TYPE,
                              agl_loc(NULL, node->line, node->column),
                              "expected %d arguments, got %d",
                              fn->arity, node->as.call.arg_count);
            }
        }
        break;
    }

    case AGL_NODE_INDEX:
        check_expr(sema, node->as.index_expr.object);
        check_expr(sema, node->as.index_expr.index);
        break;

    case AGL_NODE_ARRAY_LIT:
        for (int i = 0; i < node->as.array_lit.count; i++) {
            check_expr(sema, node->as.array_lit.elements[i]);
        }
        break;

    case AGL_NODE_STRUCT_LIT:
        for (int i = 0; i < node->as.struct_lit.field_count; i++) {
            check_expr(sema, node->as.struct_lit.field_values[i]);
        }
        break;

    case AGL_NODE_MAP_LIT:
        for (int i = 0; i < node->as.map_lit.count; i++) {
            check_expr(sema, node->as.map_lit.values[i]);
        }
        break;

    case AGL_NODE_LAMBDA:
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

    case AGL_NODE_RESULT_OK:
    case AGL_NODE_RESULT_ERR:
        check_expr(sema, node->as.result_val.value);
        break;

    case AGL_NODE_MATCH_EXPR:
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

static void check_stmt(AglSema *sema, AglNode *node) {
    if (!node || agl_error_occurred(sema->ctx)) return;

    switch (node->kind) {
    case AGL_NODE_EXPR_STMT:
        check_expr(sema, node->as.expr_stmt.expr);
        break;

    case AGL_NODE_LET_STMT:
    case AGL_NODE_VAR_STMT:
        if (node->as.var_decl.initializer) {
            check_expr(sema, node->as.var_decl.initializer);
        }
        if (!agl_error_occurred(sema->ctx)) {
            scope_define(sema->scope,
                         node->as.var_decl.name,
                         node->as.var_decl.name_length,
                         node->kind == AGL_NODE_VAR_STMT,
                         false, 0);
        }
        break;

    case AGL_NODE_ASSIGN_STMT: {
        check_expr(sema, node->as.assign_stmt.value);
        if (agl_error_occurred(sema->ctx)) break;
        AglSemaVar *v = scope_lookup(sema->scope,
                                     node->as.assign_stmt.name,
                                     node->as.assign_stmt.name_length);
        if (!v) {
            agl_error_set(sema->ctx, AGL_ERR_NAME,
                          agl_loc(NULL, node->line, node->column),
                          "undefined variable '%.*s'",
                          node->as.assign_stmt.name_length,
                          node->as.assign_stmt.name);
        } else if (!v->is_mutable) {
            agl_error_set(sema->ctx, AGL_ERR_TYPE,
                          agl_loc(NULL, node->line, node->column),
                          "cannot assign to immutable variable '%.*s'",
                          node->as.assign_stmt.name_length,
                          node->as.assign_stmt.name);
        }
        break;
    }

    case AGL_NODE_RETURN_STMT:
        if (node->as.return_stmt.value) {
            check_expr(sema, node->as.return_stmt.value);
        }
        break;

    case AGL_NODE_IF_STMT:
        check_expr(sema, node->as.if_stmt.condition);
        if (node->as.if_stmt.then_block) {
            check_stmt(sema, node->as.if_stmt.then_block);
        }
        if (node->as.if_stmt.else_block) {
            check_stmt(sema, node->as.if_stmt.else_block);
        }
        break;

    case AGL_NODE_WHILE_STMT:
        check_expr(sema, node->as.while_stmt.condition);
        if (node->as.while_stmt.body) {
            check_stmt(sema, node->as.while_stmt.body);
        }
        break;

    case AGL_NODE_FOR_STMT:
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

    case AGL_NODE_BREAK_STMT:
    case AGL_NODE_CONTINUE_STMT:
        /* Valid only inside loops; we defer that check for now */
        break;

    case AGL_NODE_BLOCK:
        scope_push(sema);
        for (int i = 0; i < node->as.block.stmt_count; i++) {
            check_stmt(sema, node->as.block.stmts[i]);
            if (agl_error_occurred(sema->ctx)) break;
        }
        scope_pop(sema);
        break;

    case AGL_NODE_FN_DECL:
        /* Register function in current scope if not already pre-registered */
        if (!scope_lookup_local(sema->scope, node->as.fn_decl.name,
                                node->as.fn_decl.name_length)) {
            scope_define(sema->scope,
                         node->as.fn_decl.name,
                         node->as.fn_decl.name_length,
                         false, true, node->as.fn_decl.param_count);
        }
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

    case AGL_NODE_STRUCT_DECL:
        break;

    case AGL_NODE_IMPORT:
        /* Import paths validated at runtime (file existence); sema accepts */
        break;

    default:
        break;
    }
}

/* ---- Public API ---- */

AglSema *agl_sema_new(AglCtx *ctx, AglArena *arena) {
    AglSema *sema = calloc(1, sizeof(AglSema));
    if (!sema) return NULL;
    sema->ctx = ctx;
    sema->arena = arena;
    sema->scope = NULL;
    return sema;
}

void agl_sema_free(AglSema *sema) {
    free(sema);
}

bool agl_sema_check(AglSema *sema, AglNode *program) {
    if (!sema || !program || program->kind != AGL_NODE_PROGRAM) return false;

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
    /* Map builtins */
    scope_define(sema->scope, "map_get", 7, false, true, 2);
    scope_define(sema->scope, "map_set", 7, false, true, 3);
    scope_define(sema->scope, "map_keys", 8, false, true, 1);
    scope_define(sema->scope, "map_has", 7, false, true, 2);
    scope_define(sema->scope, "map_del", 7, false, true, 2);
    /* String builtins */
    scope_define(sema->scope, "split", 5, false, true, 2);
    scope_define(sema->scope, "trim", 4, false, true, 1);
    scope_define(sema->scope, "contains", 8, false, true, 2);
    scope_define(sema->scope, "replace", 7, false, true, 3);
    scope_define(sema->scope, "starts_with", 11, false, true, 2);
    scope_define(sema->scope, "ends_with", 9, false, true, 2);
    scope_define(sema->scope, "to_upper", 8, false, true, 1);
    scope_define(sema->scope, "to_lower", 8, false, true, 1);
    scope_define(sema->scope, "join", 4, false, true, 2);
    scope_define(sema->scope, "substr", 6, false, true, 3);
    /* JSON builtins */
    scope_define(sema->scope, "json_parse", 10, false, true, 1);
    scope_define(sema->scope, "json_stringify", 14, false, true, 1);
    /* Environment variable builtins */
    scope_define(sema->scope, "env", 3, false, true, 1);
    scope_define(sema->scope, "env_default", 11, false, true, 2);
    /* HTTP builtins */
    scope_define(sema->scope, "http_get", 8, false, true, 2);
    scope_define(sema->scope, "http_post", 9, false, true, 3);
    /* Process execution */
    scope_define(sema->scope, "exec", 4, false, true, 2);
    /* Time functions */
    scope_define(sema->scope, "now", 3, false, true, 0);  /* variadic-like: 0 args */
    scope_define(sema->scope, "sleep", 5, false, true, 1);

    /* Pass 1: Pre-register all top-level function names so that
     * forward references and mutual recursion work correctly. */
    for (int i = 0; i < program->as.program.decl_count; i++) {
        AglNode *decl = program->as.program.decls[i];
        if (decl && decl->kind == AGL_NODE_FN_DECL) {
            scope_define(sema->scope,
                         decl->as.fn_decl.name,
                         decl->as.fn_decl.name_length,
                         false, true, decl->as.fn_decl.param_count);
        }
    }

    /* Pass 2: Check all declarations (function bodies can now reference
     * any top-level function regardless of declaration order). */
    for (int i = 0; i < program->as.program.decl_count; i++) {
        check_stmt(sema, program->as.program.decls[i]);
        if (agl_error_occurred(sema->ctx)) return false;
    }

    return true;
}
