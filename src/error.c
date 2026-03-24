#include "error.h"

AglCtx *agl_ctx_new(void) {
    AglCtx *ctx = malloc(sizeof(AglCtx));
    if (!ctx) return NULL;
    ctx->has_error = false;
    ctx->error.code = AGL_ERR_NONE;
    ctx->error.message[0] = '\0';
    ctx->error.trace_count = 0;
    ctx->trace_cb = NULL;
    ctx->trace_data = NULL;
    return ctx;
}

void agl_ctx_free(AglCtx *ctx) {
    free(ctx);
}

void agl_error_set(AglCtx *ctx, AglErrorCode code, AglSourceLoc loc,
                   const char *fmt, ...) {
    if (!ctx) return;
    ctx->has_error = true;
    ctx->error.code = code;
    ctx->error.loc = loc;
    ctx->error.trace_count = 0;

    va_list args;
    va_start(args, fmt);
    vsnprintf(ctx->error.message, sizeof(ctx->error.message), fmt, args);
    va_end(args);

    /* Capture call stack trace if available */
    if (ctx->trace_cb) {
        ctx->trace_cb(ctx->trace_data, &ctx->error);
    }
}

bool agl_error_occurred(const AglCtx *ctx) {
    return ctx && ctx->has_error;
}

const AglError *agl_error_get(const AglCtx *ctx) {
    if (!ctx || !ctx->has_error) return NULL;
    return &ctx->error;
}

void agl_error_clear(AglCtx *ctx) {
    if (!ctx) return;
    ctx->has_error = false;
    ctx->error.code = AGL_ERR_NONE;
    ctx->error.message[0] = '\0';
    ctx->error.trace_count = 0;
}

void agl_error_print(const AglError *err) {
    if (!err) return;
    if (err->loc.file) {
        fprintf(stderr, "%s:%d:%d: error: %s\n",
                err->loc.file, err->loc.line, err->loc.column, err->message);
    } else {
        fprintf(stderr, "error: %s\n", err->message);
    }
    /* Print stack trace */
    for (int i = 0; i < err->trace_count; i++) {
        if (err->trace[i].name[0]) {
            fprintf(stderr, "  in %s() (line %d)\n",
                    err->trace[i].name, err->trace[i].line);
        } else {
            fprintf(stderr, "  in <lambda> (line %d)\n",
                    err->trace[i].line);
        }
    }
}

AglSourceLoc agl_loc(const char *file, int line, int column) {
    AglSourceLoc loc = { .file = file, .line = line, .column = column };
    return loc;
}
