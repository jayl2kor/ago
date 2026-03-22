#pragma once

#include "common.h"

/* Error codes */
typedef enum {
    AGO_ERR_NONE = 0,
    AGO_ERR_SYNTAX,       /* Lexer/parser errors */
    AGO_ERR_TYPE,         /* Type checking errors */
    AGO_ERR_NAME,         /* Undefined name, duplicate definition */
    AGO_ERR_RUNTIME,      /* Runtime errors */
    AGO_ERR_IO,           /* File I/O errors */
} AgoErrorCode;

/* Source location */
typedef struct {
    const char *file;
    int line;
    int column;
} AgoSourceLoc;

/* Error object */
struct AgoError {
    AgoErrorCode code;
    AgoSourceLoc loc;
    char message[256];
};

/* Context that threads through all compiler/runtime phases */
struct AgoCtx {
    AgoError error;
    bool has_error;
};

/* Create and destroy context */
AgoCtx *ago_ctx_new(void);
void    ago_ctx_free(AgoCtx *ctx);

/* Set an error on the context */
void ago_error_set(AgoCtx *ctx, AgoErrorCode code, AgoSourceLoc loc,
                   const char *fmt, ...);

/* Check if an error has occurred */
bool ago_error_occurred(const AgoCtx *ctx);

/* Get the current error (NULL if none) */
const AgoError *ago_error_get(const AgoCtx *ctx);

/* Clear the error state */
void ago_error_clear(AgoCtx *ctx);

/* Print the error to stderr */
void ago_error_print(const AgoError *err);

/* Helper to create a source location */
AgoSourceLoc ago_loc(const char *file, int line, int column);
