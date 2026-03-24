#include "runtime.h"
#include "builtins_core.h"

/* ---- Tree-walk callback for map/filter ---- */

static AglVal interp_call_fn(void *caller_data, AglFnVal *fn,
                              AglVal *args, int argc, int line, int col) {
    AglInterp *interp = caller_data;
    return call_fn_direct(interp, fn, args, argc, line, col);
}

/* ---- Built-in function dispatch (tree-walk interpreter) ---- */

bool try_builtin_call(AglInterp *interp, const char *name, int name_len,
                      AglNode *call_node, AglVal *out) {
    /* Resolve name to builtin ID */
    AglBuiltinId bid = agl_builtin_resolve(name, name_len);
    if (bid == AGL_BUILTIN_NONE) return false;

    int argc = call_node->as.call.arg_count;
    int line = call_node->line;
    int col  = call_node->column;

    /* Evaluate all arguments from AST nodes */
    AglVal args[32];
    int eval_count = argc < 32 ? argc : 32;
    for (int i = 0; i < eval_count; i++) {
        args[i] = eval_expr(interp, call_node->as.call.args[i]);
        if (agl_error_occurred(interp->ctx)) {
            *out = val_nil();
            return true;
        }
    }

    /* Build shared context */
    AglBuiltinCtx bctx;
    bctx.arena = interp->arena;
    bctx.gc = interp->gc;
    bctx.ctx = interp->ctx;
    bctx.line = line;
    bctx.col = col;
    bctx.call_fn = interp_call_fn;
    bctx.caller_data = interp;

    *out = agl_builtin_dispatch(bid, args, argc, &bctx);
    return true;
}
