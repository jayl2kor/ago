#include "error.h"

AgoCtx *ago_ctx_new(void) {
    AgoCtx *ctx = malloc(sizeof(AgoCtx));
    if (!ctx) return NULL;
    ctx->has_error = false;
    ctx->error.code = AGO_ERR_NONE;
    ctx->error.message[0] = '\0';
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

    va_list args;
    va_start(args, fmt);
    vsnprintf(ctx->error.message, sizeof(ctx->error.message), fmt, args);
    va_end(args);
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
}

void ago_error_print(const AgoError *err) {
    if (!err) return;
    if (err->loc.file) {
        fprintf(stderr, "%s:%d:%d: error: %s\n",
                err->loc.file, err->loc.line, err->loc.column, err->message);
    } else {
        fprintf(stderr, "error: %s\n", err->message);
    }
}

AgoSourceLoc ago_loc(const char *file, int line, int column) {
    AgoSourceLoc loc = { .file = file, .line = line, .column = column };
    return loc;
}
