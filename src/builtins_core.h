#pragma once

#include "runtime.h"

/* ---- Builtin ID enum (shared between compiler and dispatchers) ---- */

typedef enum {
    AGL_BUILTIN_PRINT,
    AGL_BUILTIN_LEN,
    AGL_BUILTIN_TYPE,
    AGL_BUILTIN_STR,
    AGL_BUILTIN_INT,
    AGL_BUILTIN_FLOAT,
    AGL_BUILTIN_PUSH,
    AGL_BUILTIN_MAP,
    AGL_BUILTIN_FILTER,
    AGL_BUILTIN_ABS,
    AGL_BUILTIN_MIN,
    AGL_BUILTIN_MAX,
    AGL_BUILTIN_READ_FILE,
    AGL_BUILTIN_WRITE_FILE,
    AGL_BUILTIN_FILE_EXISTS,
    /* Map builtins */
    AGL_BUILTIN_MAP_GET,
    AGL_BUILTIN_MAP_SET,
    AGL_BUILTIN_MAP_KEYS,
    AGL_BUILTIN_MAP_HAS,
    AGL_BUILTIN_MAP_DEL,
    /* String builtins */
    AGL_BUILTIN_SPLIT,
    AGL_BUILTIN_TRIM,
    AGL_BUILTIN_CONTAINS,
    AGL_BUILTIN_REPLACE,
    AGL_BUILTIN_STARTS_WITH,
    AGL_BUILTIN_ENDS_WITH,
    AGL_BUILTIN_TO_UPPER,
    AGL_BUILTIN_TO_LOWER,
    AGL_BUILTIN_JOIN,
    AGL_BUILTIN_SUBSTR,
    AGL_BUILTIN_COUNT,
    /* JSON builtins */
    AGL_BUILTIN_JSON_PARSE,
    AGL_BUILTIN_JSON_STRINGIFY,
    /* Environment variable builtins */
    AGL_BUILTIN_ENV,
    AGL_BUILTIN_ENV_DEFAULT,
    /* HTTP builtins */
    AGL_BUILTIN_HTTP_GET,
    AGL_BUILTIN_HTTP_POST,
    /* Process execution */
    AGL_BUILTIN_EXEC,
    /* Time functions */
    AGL_BUILTIN_NOW,
    AGL_BUILTIN_SLEEP,
    AGL_BUILTIN_NONE = -1,
} AglBuiltinId;

/* ---- Context passed to shared builtin implementations ---- */

typedef struct {
    AglArena *arena;
    AglGc *gc;
    AglCtx *ctx;
    int line;
    int col;
    /* Callback for map/filter to invoke user functions */
    AglVal (*call_fn)(void *caller_data, AglFnVal *fn,
                      AglVal *args, int argc, int line, int col);
    void *caller_data;
} AglBuiltinCtx;

/* Resolve a builtin name to its ID.  Returns AGL_BUILTIN_NONE if not found. */
AglBuiltinId agl_builtin_resolve(const char *name, int len);

/* Dispatch a builtin by ID with pre-evaluated arguments.
 * Returns the result value.  On error, sets error on bctx->ctx and returns nil.
 * For print (variadic), pass all args in the array.
 * For map/filter, bctx->call_fn must be set. */
AglVal agl_builtin_dispatch(int builtin_id, AglVal *args, int argc,
                            AglBuiltinCtx *bctx);
