#include "error.h"

AgoCtx *ago_ctx_new(void) {
    AgoCtx *ctx = malloc(sizeof(AgoCtx));
    if (!ctx) return NULL;
    ctx->has_error = false;
    ctx->error.code = AGO_ERR_NONE;
    ctx->error.message[0] = '\0';
    ctx->error.trace_count = 0;
    ctx->trace_cb = NULL;
    ctx->trace_data = NULL;
    return ctx;
}

void ago_ctx_free(AgoCtx *ctx) {
    free(ctx);
}

void ago_error_set(AgoCtx *ctx, AgoErrorCode code, AgoSourceLoc loc,
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

bool ago_error_occurred(const AgoCtx *ctx) {
    return ctx && ctx->has_error;
}

const AgoError *ago_error_get(const AgoCtx *ctx) {
    if (!ctx || !ctx->has_error) return NULL;
    return &ctx->error;
}

void ago_error_clear(AgoCtx *ctx) {
    if (!ctx) return;
    ctx->has_error = false;
    ctx->error.code = AGO_ERR_NONE;
    ctx->error.message[0] = '\0';
    ctx->error.trace_count = 0;
}

void ago_error_print(const AgoError *err) {
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

AgoSourceLoc ago_loc(const char *file, int line, int column) {
    AgoSourceLoc loc = { .file = file, .line = line, .column = column };
    return loc;
}
