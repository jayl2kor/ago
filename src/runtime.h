#pragma once

/* Internal header — shared types and functions for the interpreter.
 * NOT part of the public API. Include interpreter.h for the public API. */

#include "common.h"
#include "ast.h"
#include "error.h"
#include "gc.h"
#include "arena.h"
#include <setjmp.h>
#include <limits.h>

/* ---- Runtime Value ---- */

typedef enum {
    VAL_INT,
    VAL_FLOAT,
    VAL_BOOL,
    VAL_STRING,
    VAL_FN,
    VAL_ARRAY,
    VAL_STRUCT,
    VAL_RESULT,
    VAL_NIL,
} AgoValKind;

typedef struct AgoFnVal AgoFnVal;
typedef struct AgoArrayVal AgoArrayVal;
typedef struct AgoStructVal AgoStructVal;
typedef struct AgoResultVal AgoResultVal;

typedef struct {
    AgoValKind kind;
    union {
        int64_t integer;
        double floating;
        bool boolean;
        struct { const char *data; int length; } string;
        AgoFnVal *fn;
        AgoArrayVal *array;
        AgoStructVal *strct;
        AgoResultVal *result;
    } as;
} AgoVal;

struct AgoFnVal {
    AgoObj obj;     /* GC header */
    AgoNode *decl;
    int captured_count;
    const char **captured_names;        /* malloc'd */
    int *captured_name_lengths;         /* malloc'd */
    AgoVal *captured_values;            /* malloc'd */
    bool *captured_immutable;           /* malloc'd */
};

#define MAX_ARRAY_SIZE 1024

struct AgoArrayVal {
    AgoObj obj;     /* GC header */
    AgoVal *elements;   /* malloc'd */
    int count;
};

#define MAX_STRUCT_FIELDS 64

struct AgoStructVal {
    AgoObj obj;     /* GC header */
    const char *type_name;
    int type_name_length;
    const char *field_names[MAX_STRUCT_FIELDS];
    int field_name_lengths[MAX_STRUCT_FIELDS];
    AgoVal field_values[MAX_STRUCT_FIELDS];
    int field_count;
};

struct AgoResultVal {
    AgoObj obj;     /* GC header */
    bool is_ok;
    AgoVal value;
};

/* ---- Value constructors (inline for performance) ---- */

static inline AgoVal val_int(int64_t v) {
    return (AgoVal){VAL_INT, {.integer = v}};
}
static inline AgoVal val_float(double v) {
    return (AgoVal){VAL_FLOAT, {.floating = v}};
}
static inline AgoVal val_bool(bool v) {
    return (AgoVal){VAL_BOOL, {.boolean = v}};
}
static inline AgoVal val_nil(void) {
    return (AgoVal){VAL_NIL, {.integer = 0}};
}
static inline AgoVal val_string(const char *s, int len) {
    AgoVal v;
    v.kind = VAL_STRING;
    v.as.string.data = s;
    v.as.string.length = len;
    return v;
}

/* ---- Environment (variable bindings) ---- */

#define MAX_VARS 256

typedef struct {
    const char *names[MAX_VARS];
    int name_lengths[MAX_VARS];
    AgoVal values[MAX_VARS];
    bool immutable[MAX_VARS];   /* true for let, false for var */
    int count;
} AgoEnv;

/* ---- Interpreter state ---- */

#define MAX_MODULES 64
#define MAX_CALL_DEPTH 512

typedef struct {
    const char *name;
    int name_len;
    int line;
    int column;
} AgoCallFrame;

typedef struct {
    char *path;
    char *source;       /* malloc'd source text — AST tokens point into this */
    AgoArena *arena;    /* AST nodes live here */
} AgoModule;

typedef struct {
    AgoEnv env;
    AgoCtx *ctx;
    AgoArena *arena;    /* temporary allocations */
    AgoGc *gc;          /* GC-tracked runtime objects */
    const char *file;   /* current file path (for import resolution) */
    AgoModule modules[MAX_MODULES];
    int module_count;
    bool has_return;
    AgoVal return_value;
    jmp_buf return_jmp;
    bool return_jmp_set;
    int call_depth;
    AgoCallFrame call_frames[MAX_CALL_DEPTH];
} AgoInterp;

/* ---- runtime.c ---- */

const char *str_content(AgoVal s, int *out_len);
void array_cleanup(void *p);
void fn_cleanup(void *p);

void env_init(AgoEnv *env);
bool env_define(AgoEnv *env, const char *name, int length, AgoVal val,
                bool is_immutable);
AgoVal *env_get(AgoEnv *env, const char *name, int length);
/* Returns: 0=ok, 1=not found, 2=immutable */
int env_assign(AgoEnv *env, const char *name, int length, AgoVal val);

void mark_val(AgoVal val);
void gc_collect(AgoInterp *interp);

bool is_truthy(AgoVal val);
void print_val_inline(AgoVal val);
void builtin_print(AgoVal val);

/* Trace capture callback for ago_error_set */
void capture_trace(void *data, AgoError *err);

AgoVal call_fn_direct(AgoInterp *interp, AgoFnVal *fn, AgoVal *args,
                      int arg_count, int line, int column);

/* ---- builtins.c ---- */

/* Try to dispatch a built-in function call.
 * Returns true if name matched a builtin (result stored in *out).
 * Returns false if not a builtin — caller should try user functions. */
bool try_builtin_call(AgoInterp *interp, const char *name, int name_len,
                      AgoNode *call_node, AgoVal *out);

/* ---- modules.c ---- */

void module_cache_free(AgoInterp *interp);
void exec_import(AgoInterp *interp, AgoNode *node);

/* ---- interpreter.c ---- */

AgoVal eval_expr(AgoInterp *interp, AgoNode *node);
void exec_stmt(AgoInterp *interp, AgoNode *node);
