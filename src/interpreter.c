#include "interpreter.h"
#include "runtime.h"
#include "parser.h"
#include "sema.h"
#include "vm.h"

/* ---- Call user function from AST call node (evaluates args) ---- */

static AglVal call_user_fn(AglInterp *interp, AglFnVal *fn, AglNode *call_node) {
    int arg_count = call_node->as.call.arg_count;
    if (arg_count > 64) {
        agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                      agl_loc(NULL, call_node->line, call_node->column),
                      "too many arguments (max 64)");
        return val_nil();
    }
    AglVal args[64];
    for (int i = 0; i < arg_count; i++) {
        args[i] = eval_expr(interp, call_node->as.call.args[i]);
        if (agl_error_occurred(interp->ctx)) return val_nil();
    }
    return call_fn_direct(interp, fn, args, arg_count,
                          call_node->line, call_node->column);
}

/* ---- Expression evaluation ---- */

AglVal eval_expr(AglInterp *interp, AglNode *node) {
    if (!node || agl_error_occurred(interp->ctx)) return val_nil();

    switch (node->kind) {
    case AGL_NODE_INT_LIT:
        return val_int(node->as.int_lit.value);

    case AGL_NODE_FLOAT_LIT:
        return val_float(node->as.float_lit.value);

    case AGL_NODE_STRING_LIT:
        return val_string(node->as.string_lit.value, node->as.string_lit.length);

    case AGL_NODE_BOOL_LIT:
        return val_bool(node->as.bool_lit.value);

    case AGL_NODE_IDENT: {
        AglVal *v = env_get(&interp->env, node->as.ident.name, node->as.ident.length);
        if (!v) {
            agl_error_set(interp->ctx, AGL_ERR_NAME,
                          agl_loc(NULL, node->line, node->column),
                          "undefined variable '%.*s'",
                          node->as.ident.length, node->as.ident.name);
            return val_nil();
        }
        return *v;
    }

    case AGL_NODE_UNARY: {
        AglVal operand = eval_expr(interp, node->as.unary.operand);
        if (agl_error_occurred(interp->ctx)) return val_nil();

        if (node->as.unary.op == AGL_TOKEN_MINUS) {
            if (operand.kind == VAL_INT) return val_int(-operand.as.integer);
            if (operand.kind == VAL_FLOAT) return val_float(-operand.as.floating);
        }
        if (node->as.unary.op == AGL_TOKEN_NOT) {
            if (operand.kind == VAL_BOOL) return val_bool(!operand.as.boolean);
        }
        agl_error_set(interp->ctx, AGL_ERR_TYPE,
                      agl_loc(NULL, node->line, node->column),
                      "invalid unary operator");
        return val_nil();
    }

    case AGL_NODE_BINARY: {
        AglTokenKind op = node->as.binary.op;

        /* Field access: don't evaluate right side */
        if (op == AGL_TOKEN_DOT) {
            AglVal left = eval_expr(interp, node->as.binary.left);
            if (agl_error_occurred(interp->ctx)) return val_nil();
            AglNode *field_node = node->as.binary.right;
            if (left.kind == VAL_STRUCT && field_node->kind == AGL_NODE_IDENT) {
                AglStructVal *s = left.as.strct;
                for (int i = 0; i < s->field_count; i++) {
                    if (agl_str_eq(s->field_names[i], s->field_name_lengths[i],
                                   field_node->as.ident.name, field_node->as.ident.length)) {
                        return s->field_values[i];
                    }
                }
                agl_error_set(interp->ctx, AGL_ERR_NAME,
                              agl_loc(NULL, node->line, node->column),
                              "no field '%.*s'", field_node->as.ident.length,
                              field_node->as.ident.name);
            } else {
                agl_error_set(interp->ctx, AGL_ERR_TYPE,
                              agl_loc(NULL, node->line, node->column),
                              "cannot access field on non-struct value");
            }
            return val_nil();
        }

        AglVal left = eval_expr(interp, node->as.binary.left);
        if (agl_error_occurred(interp->ctx)) return val_nil();
        AglVal right = eval_expr(interp, node->as.binary.right);
        if (agl_error_occurred(interp->ctx)) return val_nil();

        if (left.kind == VAL_INT && right.kind == VAL_INT) {
            int64_t l = left.as.integer, r = right.as.integer;
            switch (op) {
            case AGL_TOKEN_PLUS:    return val_int(l + r);
            case AGL_TOKEN_MINUS:   return val_int(l - r);
            case AGL_TOKEN_STAR:    return val_int(l * r);
            case AGL_TOKEN_SLASH:
                if (r == 0) {
                    agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                                  agl_loc(NULL, node->line, node->column),
                                  "division by zero");
                    return val_nil();
                }
                return val_int(l / r);
            case AGL_TOKEN_PERCENT:
                if (r == 0) {
                    agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                                  agl_loc(NULL, node->line, node->column),
                                  "division by zero");
                    return val_nil();
                }
                return val_int(l % r);
            case AGL_TOKEN_EQ:      return val_bool(l == r);
            case AGL_TOKEN_NEQ:     return val_bool(l != r);
            case AGL_TOKEN_LT:      return val_bool(l < r);
            case AGL_TOKEN_GT:      return val_bool(l > r);
            case AGL_TOKEN_LE:      return val_bool(l <= r);
            case AGL_TOKEN_GE:      return val_bool(l >= r);
            default: break;
            }
        }

        /* Float arithmetic */
        if (left.kind == VAL_FLOAT && right.kind == VAL_FLOAT) {
            double l = left.as.floating, r = right.as.floating;
            switch (op) {
            case AGL_TOKEN_PLUS:    return val_float(l + r);
            case AGL_TOKEN_MINUS:   return val_float(l - r);
            case AGL_TOKEN_STAR:    return val_float(l * r);
            case AGL_TOKEN_SLASH:   return val_float(l / r);
            case AGL_TOKEN_EQ:      return val_bool(l == r);
            case AGL_TOKEN_NEQ:     return val_bool(l != r);
            case AGL_TOKEN_LT:      return val_bool(l < r);
            case AGL_TOKEN_GT:      return val_bool(l > r);
            case AGL_TOKEN_LE:      return val_bool(l <= r);
            case AGL_TOKEN_GE:      return val_bool(l >= r);
            default: break;
            }
        }

        /* Int-float promotion */
        if ((left.kind == VAL_INT && right.kind == VAL_FLOAT) ||
            (left.kind == VAL_FLOAT && right.kind == VAL_INT)) {
            double l = left.kind == VAL_FLOAT ? left.as.floating : (double)left.as.integer;
            double r = right.kind == VAL_FLOAT ? right.as.floating : (double)right.as.integer;
            switch (op) {
            case AGL_TOKEN_PLUS:    return val_float(l + r);
            case AGL_TOKEN_MINUS:   return val_float(l - r);
            case AGL_TOKEN_STAR:    return val_float(l * r);
            case AGL_TOKEN_SLASH:   return val_float(l / r);
            case AGL_TOKEN_EQ:      return val_bool(l == r);
            case AGL_TOKEN_NEQ:     return val_bool(l != r);
            case AGL_TOKEN_LT:      return val_bool(l < r);
            case AGL_TOKEN_GT:      return val_bool(l > r);
            case AGL_TOKEN_LE:      return val_bool(l <= r);
            case AGL_TOKEN_GE:      return val_bool(l >= r);
            default: break;
            }
        }

        if (left.kind == VAL_BOOL && right.kind == VAL_BOOL) {
            bool l = left.as.boolean, r = right.as.boolean;
            switch (op) {
            case AGL_TOKEN_AND:  return val_bool(l && r);
            case AGL_TOKEN_OR:   return val_bool(l || r);
            case AGL_TOKEN_EQ:   return val_bool(l == r);
            case AGL_TOKEN_NEQ:  return val_bool(l != r);
            default: break;
            }
        }

        /* String operations: ==, !=, <, >, <=, >=, + */
        if (left.kind == VAL_STRING && right.kind == VAL_STRING) {
            int llen, rlen;
            const char *ldata = str_content(left, &llen);
            const char *rdata = str_content(right, &rlen);
            switch (op) {
            case AGL_TOKEN_EQ:
                return val_bool(llen == rlen && memcmp(ldata, rdata, (size_t)llen) == 0);
            case AGL_TOKEN_NEQ:
                return val_bool(llen != rlen || memcmp(ldata, rdata, (size_t)llen) != 0);
            case AGL_TOKEN_LT: case AGL_TOKEN_GT:
            case AGL_TOKEN_LE: case AGL_TOKEN_GE: {
                int minlen = llen < rlen ? llen : rlen;
                int cmp = memcmp(ldata, rdata, (size_t)minlen);
                if (cmp == 0) cmp = (llen > rlen) - (llen < rlen);
                if (op == AGL_TOKEN_LT) return val_bool(cmp < 0);
                if (op == AGL_TOKEN_GT) return val_bool(cmp > 0);
                if (op == AGL_TOKEN_LE) return val_bool(cmp <= 0);
                return val_bool(cmp >= 0);
            }
            case AGL_TOKEN_PLUS: {
                if (llen > INT_MAX - rlen) {
                    agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                                  agl_loc(NULL, node->line, node->column),
                                  "string too large");
                    return val_nil();
                }
                int total = llen + rlen;
                char *buf = agl_arena_alloc(interp->arena, (size_t)total);
                if (!buf) {
                    agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                                  agl_loc(NULL, node->line, node->column),
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

        agl_error_set(interp->ctx, AGL_ERR_TYPE,
                      agl_loc(NULL, node->line, node->column),
                      "invalid binary operation");
        return val_nil();
    }

    case AGL_NODE_ARRAY_LIT: {
        AglArrayVal *arr = agl_gc_alloc(interp->gc, sizeof(AglArrayVal), array_cleanup);
        if (!arr) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column), "out of memory");
            return val_nil();
        }
        arr->count = node->as.array_lit.count;
        arr->elements = NULL;
        if (arr->count > 0) {
            arr->elements = malloc(sizeof(AglVal) * (size_t)arr->count);
            if (!arr->elements) {
                agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                              agl_loc(NULL, node->line, node->column), "out of memory");
                return val_nil();
            }
        }
        for (int i = 0; i < arr->count; i++) {
            arr->elements[i] = eval_expr(interp, node->as.array_lit.elements[i]);
            if (agl_error_occurred(interp->ctx)) return val_nil();
        }
        AglVal v;
        v.kind = VAL_ARRAY;
        v.as.array = arr;
        return v;
    }

    case AGL_NODE_MAP_LIT: {
        AglMapVal *m = agl_gc_alloc(interp->gc, sizeof(AglMapVal), map_cleanup);
        if (!m) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column), "out of memory");
            return val_nil();
        }
        int count = node->as.map_lit.count;
        m->count = count;
        m->capacity = count;
        m->keys = count > 0 ? malloc(sizeof(char *) * (size_t)count) : NULL;
        m->key_lengths = count > 0 ? malloc(sizeof(int) * (size_t)count) : NULL;
        m->values = count > 0 ? malloc(sizeof(AglVal) * (size_t)count) : NULL;
        for (int i = 0; i < count; i++) {
            m->keys[i] = node->as.map_lit.keys[i];
            m->key_lengths[i] = node->as.map_lit.key_lengths[i];
            m->values[i] = eval_expr(interp, node->as.map_lit.values[i]);
            if (agl_error_occurred(interp->ctx)) return val_nil();
        }
        AglVal v;
        v.kind = VAL_MAP;
        v.as.map = m;
        return v;
    }

    case AGL_NODE_INDEX: {
        AglVal obj = eval_expr(interp, node->as.index_expr.object);
        if (agl_error_occurred(interp->ctx)) return val_nil();
        AglVal idx = eval_expr(interp, node->as.index_expr.index);
        if (agl_error_occurred(interp->ctx)) return val_nil();
        if (obj.kind == VAL_MAP) {
            if (idx.kind != VAL_STRING) {
                agl_error_set(interp->ctx, AGL_ERR_TYPE,
                              agl_loc(NULL, node->line, node->column),
                              "map key must be a string");
                return val_nil();
            }
            int klen; const char *kdata = str_content(idx, &klen);
            AglMapVal *mp = obj.as.map;
            for (int i = 0; i < mp->count; i++) {
                if (mp->key_lengths[i] == klen && memcmp(mp->keys[i], kdata, (size_t)klen) == 0) {
                    return mp->values[i];
                }
            }
            return val_nil();
        }
        if (obj.kind != VAL_ARRAY) {
            agl_error_set(interp->ctx, AGL_ERR_TYPE,
                          agl_loc(NULL, node->line, node->column),
                          "cannot index non-array value");
            return val_nil();
        }
        if (idx.kind != VAL_INT) {
            agl_error_set(interp->ctx, AGL_ERR_TYPE,
                          agl_loc(NULL, node->line, node->column),
                          "array index must be an integer");
            return val_nil();
        }
        int i = (int)idx.as.integer;
        if (i < 0 || i >= obj.as.array->count) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column),
                          "index %d out of bounds (length %d)", i, obj.as.array->count);
            return val_nil();
        }
        return obj.as.array->elements[i];
    }

    case AGL_NODE_STRUCT_LIT: {
        AglStructVal *s = agl_gc_alloc(interp->gc, sizeof(AglStructVal), NULL);
        if (!s) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column), "out of memory");
            return val_nil();
        }
        s->type_name = node->as.struct_lit.name;
        s->type_name_length = node->as.struct_lit.name_length;
        s->field_count = node->as.struct_lit.field_count;
        for (int i = 0; i < s->field_count; i++) {
            s->field_names[i] = node->as.struct_lit.field_names[i];
            s->field_name_lengths[i] = node->as.struct_lit.field_name_lengths[i];
            s->field_values[i] = eval_expr(interp, node->as.struct_lit.field_values[i]);
            if (agl_error_occurred(interp->ctx)) return val_nil();
        }
        AglVal v;
        v.kind = VAL_STRUCT;
        v.as.strct = s;
        return v;
    }

    case AGL_NODE_RESULT_OK:
    case AGL_NODE_RESULT_ERR: {
        AglVal inner = eval_expr(interp, node->as.result_val.value);
        if (agl_error_occurred(interp->ctx)) return val_nil();
        AglResultVal *rv = agl_gc_alloc(interp->gc, sizeof(AglResultVal), NULL);
        if (!rv) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column), "out of memory");
            return val_nil();
        }
        rv->is_ok = (node->kind == AGL_NODE_RESULT_OK);
        rv->value = inner;
        AglVal v;
        v.kind = VAL_RESULT;
        v.as.result = rv;
        return v;
    }

    case AGL_NODE_MATCH_EXPR: {
        AglVal subject = eval_expr(interp, node->as.match_expr.subject);
        if (agl_error_occurred(interp->ctx)) return val_nil();
        if (subject.kind != VAL_RESULT) {
            agl_error_set(interp->ctx, AGL_ERR_TYPE,
                          agl_loc(NULL, node->line, node->column),
                          "match requires a result value");
            return val_nil();
        }
        AglResultVal *rv = subject.as.result;
        int saved_count = interp->env.count;
        AglVal result;
        const char *bind_name;
        int bind_len;
        AglNode *body;
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
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column),
                          "too many variables (max %d)", MAX_VARS);
            return val_nil();
        }
        result = eval_expr(interp, body);
        interp->env.count = saved_count;
        return result;
    }

    case AGL_NODE_LAMBDA: {
        AglFnVal *fn = agl_gc_alloc(interp->gc, sizeof(AglFnVal), fn_cleanup);
        if (!fn) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column), "out of memory");
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
            fn->captured_values = malloc(sizeof(AglVal) * n_cap);
            fn->captured_immutable = malloc(sizeof(bool) * n_cap);
            if (!fn->captured_names || !fn->captured_name_lengths ||
                !fn->captured_values || !fn->captured_immutable) {
                agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                              agl_loc(NULL, node->line, node->column), "out of memory");
                return val_nil();
            }
            memcpy(fn->captured_names, interp->env.names, sizeof(char *) * n_cap);
            memcpy(fn->captured_name_lengths, interp->env.name_lengths, sizeof(int) * n_cap);
            memcpy(fn->captured_values, interp->env.values, sizeof(AglVal) * n_cap);
            memcpy(fn->captured_immutable, interp->env.immutable, sizeof(bool) * n_cap);
        }
        AglVal fn_val;
        fn_val.kind = VAL_FN;
        fn_val.as.fn = fn;
        return fn_val;
    }

    case AGL_NODE_CALL: {
        /* Try built-in functions first */
        if (node->as.call.callee->kind == AGL_NODE_IDENT) {
            const char *name = node->as.call.callee->as.ident.name;
            int len = node->as.call.callee->as.ident.length;
            AglVal result;
            if (try_builtin_call(interp, name, len, node, &result)) {
                return result;
            }
        }

        /* Evaluate callee as expression (handles variables, lambdas, etc.) */
        AglVal callee = eval_expr(interp, node->as.call.callee);
        if (agl_error_occurred(interp->ctx)) return val_nil();

        if (callee.kind == VAL_FN) {
            return call_user_fn(interp, callee.as.fn, node);
        }

        /* Not callable */
        if (node->as.call.callee->kind == AGL_NODE_IDENT) {
            agl_error_set(interp->ctx, AGL_ERR_NAME,
                          agl_loc(NULL, node->line, node->column),
                          "unknown function '%.*s'",
                          node->as.call.callee->as.ident.length,
                          node->as.call.callee->as.ident.name);
        } else {
            agl_error_set(interp->ctx, AGL_ERR_TYPE,
                          agl_loc(NULL, node->line, node->column),
                          "expression is not callable");
        }
        return val_nil();
    }

    default:
        agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                      agl_loc(NULL, node->line, node->column),
                      "unsupported expression type");
        return val_nil();
    }
}

/* ---- Statement execution ---- */

void exec_stmt(AglInterp *interp, AglNode *node) {
    if (!node || agl_error_occurred(interp->ctx)) return;

    /* GC: collect at statement boundaries when threshold exceeded */
    if (agl_gc_should_collect(interp->gc)) {
        gc_collect(interp);
    }

    switch (node->kind) {
    case AGL_NODE_EXPR_STMT:
        eval_expr(interp, node->as.expr_stmt.expr);
        break;

    case AGL_NODE_ASSIGN_STMT: {
        AglVal val = eval_expr(interp, node->as.assign_stmt.value);
        if (agl_error_occurred(interp->ctx)) return;
        int rc = env_assign(&interp->env, node->as.assign_stmt.name,
                            node->as.assign_stmt.name_length, val);
        if (rc == 1) {
            agl_error_set(interp->ctx, AGL_ERR_NAME,
                          agl_loc(NULL, node->line, node->column),
                          "undefined variable '%.*s'",
                          node->as.assign_stmt.name_length,
                          node->as.assign_stmt.name);
        } else if (rc == 2) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column),
                          "cannot assign to immutable variable '%.*s'",
                          node->as.assign_stmt.name_length,
                          node->as.assign_stmt.name);
        }
        break;
    }

    case AGL_NODE_LET_STMT:
    case AGL_NODE_VAR_STMT: {
        AglVal val = val_nil();
        if (node->as.var_decl.initializer) {
            val = eval_expr(interp, node->as.var_decl.initializer);
        }
        if (agl_error_occurred(interp->ctx)) return;
        bool immut = (node->kind == AGL_NODE_LET_STMT);
        if (!env_define(&interp->env, node->as.var_decl.name,
                        node->as.var_decl.name_length, val, immut)) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column),
                          "too many variables (max %d)", MAX_VARS);
        }
        break;
    }

    case AGL_NODE_RETURN_STMT: {
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

    case AGL_NODE_IF_STMT: {
        AglVal cond = eval_expr(interp, node->as.if_stmt.condition);
        if (agl_error_occurred(interp->ctx)) return;
        if (is_truthy(cond)) {
            exec_stmt(interp, node->as.if_stmt.then_block);
        } else if (node->as.if_stmt.else_block) {
            exec_stmt(interp, node->as.if_stmt.else_block);
        }
        break;
    }

    case AGL_NODE_WHILE_STMT: {
        for (;;) {
            AglVal cond = eval_expr(interp, node->as.while_stmt.condition);
            if (agl_error_occurred(interp->ctx)) return;
            if (!is_truthy(cond)) break;
            exec_stmt(interp, node->as.while_stmt.body);
            if (agl_error_occurred(interp->ctx)) return;
            if (interp->has_return) return;
        }
        break;
    }

    case AGL_NODE_FOR_STMT: {
        AglVal iterable = eval_expr(interp, node->as.for_stmt.iterable);
        if (agl_error_occurred(interp->ctx)) return;
        if (iterable.kind != VAL_ARRAY) {
            agl_error_set(interp->ctx, AGL_ERR_TYPE,
                          agl_loc(NULL, node->line, node->column),
                          "for-in requires an array");
            return;
        }
        int saved = interp->env.count;
        if (!env_define(&interp->env, node->as.for_stmt.var_name,
                        node->as.for_stmt.var_name_length, val_nil(), false)) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column),
                          "too many variables (max %d)", MAX_VARS);
            return;
        }
        int var_idx = interp->env.count - 1;
        for (int i = 0; i < iterable.as.array->count; i++) {
            interp->env.values[var_idx] = iterable.as.array->elements[i];
            exec_stmt(interp, node->as.for_stmt.body);
            if (agl_error_occurred(interp->ctx)) return;
            if (interp->has_return) return;
        }
        interp->env.count = saved;
        break;
    }

    case AGL_NODE_STRUCT_DECL:
        break;

    case AGL_NODE_IMPORT:
        exec_import(interp, node);
        break;

    case AGL_NODE_FN_DECL: {
        AglFnVal *fn = agl_gc_alloc(interp->gc, sizeof(AglFnVal), fn_cleanup);
        if (!fn) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column), "out of memory");
            return;
        }
        fn->decl = node;
        fn->captured_count = 0;
        fn->captured_names = NULL;
        fn->captured_name_lengths = NULL;
        fn->captured_values = NULL;
        fn->captured_immutable = NULL;
        AglVal fn_val;
        fn_val.kind = VAL_FN;
        fn_val.as.fn = fn;
        if (!env_define(&interp->env, node->as.fn_decl.name,
                        node->as.fn_decl.name_length, fn_val, true)) {
            agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                          agl_loc(NULL, node->line, node->column),
                          "too many variables (max %d)", MAX_VARS);
        }
        break;
    }

    case AGL_NODE_BLOCK:
        for (int i = 0; i < node->as.block.stmt_count; i++) {
            exec_stmt(interp, node->as.block.stmts[i]);
            if (agl_error_occurred(interp->ctx)) return;
            if (interp->has_return) return;
        }
        break;

    default:
        agl_error_set(interp->ctx, AGL_ERR_RUNTIME,
                      agl_loc(NULL, node->line, node->column),
                      "unsupported statement type");
        break;
    }
}

/* ---- Public API ---- */

int agl_interpret(AglNode *program, const char *filename, AglCtx *ctx) {
    /* Delegate to bytecode VM */
    return agl_vm_interpret(program, filename, ctx);
}

int agl_run(const char *source, const char *filename, AglCtx *ctx) {
    AglArena *arena = agl_arena_new();
    if (!arena) return -1;

    AglParser parser;
    agl_parser_init(&parser, source, filename, arena, ctx);
    AglNode *program = agl_parser_parse(&parser);

    int result = -1;
    if (program && !agl_error_occurred(ctx)) {
        /* Skip sema for files with imports (imported names unavailable) */
        bool has_imports = false;
        for (int i = 0; i < program->as.program.decl_count; i++) {
            if (program->as.program.decls[i] &&
                program->as.program.decls[i]->kind == AGL_NODE_IMPORT) {
                has_imports = true;
                break;
            }
        }
        if (!has_imports) {
            AglSema *sema = agl_sema_new(ctx, arena);
            if (sema) {
                agl_sema_check(sema, program);
                agl_sema_free(sema);
            }
        }
        if (!agl_error_occurred(ctx)) {
            result = agl_interpret(program, filename, ctx);
        }
    }

    agl_arena_free(arena);
    return result;
}

/* ---- REPL (delegates to VM) ---- */

struct AglRepl {
    AglVmRepl *vm_repl;
};

AglRepl *agl_repl_new(void) {
    AglRepl *repl = calloc(1, sizeof(AglRepl));
    if (!repl) return NULL;
    repl->vm_repl = agl_vm_repl_new();
    if (!repl->vm_repl) { free(repl); return NULL; }
    return repl;
}

void agl_repl_free(AglRepl *repl) {
    if (!repl) return;
    agl_vm_repl_free(repl->vm_repl);
    free(repl);
}

int agl_repl_exec(AglRepl *repl, const char *source) {
    if (!repl || !source) return -1;
    return agl_vm_repl_exec(repl->vm_repl, source);
}
