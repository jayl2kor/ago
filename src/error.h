#pragma once

#include "common.h"

/* Error codes */
typedef enum {
    AGL_ERR_NONE = 0,
    AGL_ERR_SYNTAX,       /* Lexer/parser errors */
    AGL_ERR_TYPE,         /* Type checking errors */
    AGL_ERR_NAME,         /* Undefined name, duplicate definition */
    AGL_ERR_RUNTIME,      /* Runtime errors */
    AGL_ERR_IO,           /* File I/O errors */
} AglErrorCode;

/* Source location */
typedef struct {
    const char *file;
    int line;
    int column;
} AglSourceLoc;

/* Stack trace frame */
#define AGL_MAX_TRACE 16

typedef struct {
    char name[64];  /* function name (copied, not a pointer) */
    int line;
    int column;
} AglTraceFrame;

/* Error object */
struct AglError {
    AglErrorCode code;
    AglSourceLoc loc;
    char message[256];
    AglTraceFrame trace[AGL_MAX_TRACE];
    int trace_count;
};

/* Context that threads through all compiler/runtime phases */
struct AglCtx {
    AglError error;
    bool has_error;
    /* Trace capture: set by interpreter, called by agl_error_set */
    void (*trace_cb)(void *data, AglError *err);
    void *trace_data;
};

/* Create and destroy context */
AglCtx *agl_ctx_new(void);
void    agl_ctx_free(AglCtx *ctx);

/* Set an error on the context */
void agl_error_set(AglCtx *ctx, AglErrorCode code, AglSourceLoc loc,
                   const char *fmt, ...);

/* Check if an error has occurred */
bool agl_error_occurred(const AglCtx *ctx);

/* Get the current error (NULL if none) */
const AglError *agl_error_get(const AglCtx *ctx);

/* Clear the error state */
void agl_error_clear(AglCtx *ctx);

/* Print the error to stderr */
void agl_error_print(const AglError *err);

/* Helper to create a source location */
AglSourceLoc agl_loc(const char *file, int line, int column);
