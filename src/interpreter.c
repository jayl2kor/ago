#include "interpreter.h"
#include "runtime.h"
#include "parser.h"
#include "sema.h"
#include "vm.h"

/* ---- Call user function from AST call node (evaluates args) ---- */

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

AgoVal eval_expr(AgoInterp *interp, AgoNode *node) {
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

        /* Field access: don't evaluate right side */
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

        /* String operations: ==, !=, <, >, <=, >=, + */
        if (left.kind == VAL_STRING && right.kind == VAL_STRING) {
            int llen, rlen;
            const char *ldata = str_content(left, &llen);
            const char *rdata = str_content(right, &rlen);
            switch (op) {
            case AGO_TOKEN_EQ:
                return val_bool(llen == rlen && memcmp(ldata, rdata, (size_t)llen) == 0);
            case AGO_TOKEN_NEQ:
                return val_bool(llen != rlen || memcmp(ldata, rdata, (size_t)llen) != 0);
            case AGO_TOKEN_LT: case AGO_TOKEN_GT:
            case AGO_TOKEN_LE: case AGO_TOKEN_GE: {
                int minlen = llen < rlen ? llen : rlen;
                int cmp = memcmp(ldata, rdata, (size_t)minlen);
                if (cmp == 0) cmp = (llen > rlen) - (llen < rlen);
                if (op == AGO_TOKEN_LT) return val_bool(cmp < 0);
                if (op == AGO_TOKEN_GT) return val_bool(cmp > 0);
                if (op == AGO_TOKEN_LE) return val_bool(cmp <= 0);
                return val_bool(cmp >= 0);
            }
            case AGO_TOKEN_PLUS: {
                if (llen > INT_MAX - rlen) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                                  ago_loc(NULL, node->line, node->column),
                                  "string too large");
                    return val_nil();
                }
                int total = llen + rlen;
                char *buf = ago_arena_alloc(interp->arena, (size_t)total);
                if (!buf) {
                    ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                                  ago_loc(NULL, node->line, node->column),
                                  "out of memory");
                    return val_nil();
                }
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
        if (!arr) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column), "out of memory");
            return val_nil();
        }
        arr->count = node->as.array_lit.count;
        arr->elements = NULL;
        if (arr->count > 0) {
            arr->elements = malloc(sizeof(AgoVal) * (size_t)arr->count);
            if (!arr->elements) {
                ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                              ago_loc(NULL, node->line, node->column), "out of memory");
                return val_nil();
            }
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
        if (!s) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column), "out of memory");
            return val_nil();
        }
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
        }
        AgoVal fn_val;
        fn_val.kind = VAL_FN;
        fn_val.as.fn = fn;
        return fn_val;
    }

    case AGO_NODE_CALL: {
        /* Try built-in functions first */
        if (node->as.call.callee->kind == AGO_NODE_IDENT) {
            const char *name = node->as.call.callee->as.ident.name;
            int len = node->as.call.callee->as.ident.length;
            AgoVal result;
            if (try_builtin_call(interp, name, len, node, &result)) {
                return result;
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

void exec_stmt(AgoInterp *interp, AgoNode *node) {
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

    case AGO_NODE_IMPORT:
        exec_import(interp, node);
        break;

    case AGO_NODE_FN_DECL: {
        AgoFnVal *fn = ago_gc_alloc(interp->gc, sizeof(AgoFnVal), fn_cleanup);
        if (!fn) {
            ago_error_set(interp->ctx, AGO_ERR_RUNTIME,
                          ago_loc(NULL, node->line, node->column), "out of memory");
            return;
        }
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
    /* Delegate to bytecode VM */
    return ago_vm_interpret(program, filename, ctx);
}

int ago_run(const char *source, const char *filename, AgoCtx *ctx) {
    AgoArena *arena = ago_arena_new();
    if (!arena) return -1;

    AgoParser parser;
    ago_parser_init(&parser, source, filename, arena, ctx);
    AgoNode *program = ago_parser_parse(&parser);

    int result = -1;
    if (program && !ago_error_occurred(ctx)) {
        /* Skip sema for files with imports (imported names unavailable) */
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
        if (!ago_error_occurred(ctx)) {
            result = ago_interpret(program, filename, ctx);
        }
    }

    ago_arena_free(arena);
    return result;
}

/* ---- REPL (delegates to VM) ---- */

struct AgoRepl {
    AgoVmRepl *vm_repl;
};

AgoRepl *ago_repl_new(void) {
    AgoRepl *repl = calloc(1, sizeof(AgoRepl));
    if (!repl) return NULL;
    repl->vm_repl = ago_vm_repl_new();
    if (!repl->vm_repl) { free(repl); return NULL; }
    return repl;
}

void ago_repl_free(AgoRepl *repl) {
    if (!repl) return;
    ago_vm_repl_free(repl->vm_repl);
    free(repl);
}

int ago_repl_exec(AgoRepl *repl, const char *source) {
    if (!repl || !source) return -1;
    return ago_vm_repl_exec(repl->vm_repl, source);
}
