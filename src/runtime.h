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
    VAL_MAP,
    VAL_NIL,
} AglValKind;

typedef struct AglFnVal AglFnVal;
typedef struct AglArrayVal AglArrayVal;
typedef struct AglStructVal AglStructVal;
typedef struct AglResultVal AglResultVal;
typedef struct AglMapVal AglMapVal;

typedef struct AglVal {
    AglValKind kind;
    union {
        int64_t integer;
        double floating;
        bool boolean;
        struct { const char *data; int length; } string;
        AglFnVal *fn;
        AglArrayVal *array;
        AglStructVal *strct;
        AglResultVal *result;
        AglMapVal *map;
    } as;
} AglVal;

/* Forward declaration for bytecode chunk */
typedef struct AglChunk AglChunk;

struct AglFnVal {
    AglObj obj;     /* GC header */
    AglNode *decl;          /* AST node (tree-walk path, NULL for VM-only) */
    AglChunk *chunk;        /* bytecode (VM path, NULL for tree-walk) */
    int arity;              /* parameter count */
    int captured_count;
    const char **captured_names;        /* malloc'd */
    int *captured_name_lengths;         /* malloc'd */
    AglVal *captured_values;            /* malloc'd */
    bool *captured_immutable;           /* malloc'd */
};

#define MAX_ARRAY_SIZE 1024

struct AglArrayVal {
    AglObj obj;     /* GC header */
    AglVal *elements;   /* malloc'd */
    int count;
};

#define MAX_STRUCT_FIELDS 64

struct AglStructVal {
    AglObj obj;     /* GC header */
    const char *type_name;
    int type_name_length;
    const char *field_names[MAX_STRUCT_FIELDS];
    int field_name_lengths[MAX_STRUCT_FIELDS];
    AglVal field_values[MAX_STRUCT_FIELDS];
    int field_count;
};

struct AglResultVal {
    AglObj obj;     /* GC header */
    bool is_ok;
    AglVal value;
};

#define MAX_MAP_SIZE 256

struct AglMapVal {
    AglObj obj;             /* GC header */
    const char **keys;      /* malloc'd */
    int *key_lengths;       /* malloc'd */
    AglVal *values;         /* malloc'd */
    int count;
    int capacity;
};

/* ---- Value constructors (inline for performance) ---- */

static inline AglVal val_int(int64_t v) {
    return (AglVal){VAL_INT, {.integer = v}};
}
static inline AglVal val_float(double v) {
    return (AglVal){VAL_FLOAT, {.floating = v}};
}
static inline AglVal val_bool(bool v) {
    return (AglVal){VAL_BOOL, {.boolean = v}};
}
static inline AglVal val_nil(void) {
    return (AglVal){VAL_NIL, {.integer = 0}};
}
static inline AglVal val_string(const char *s, int len) {
    AglVal v;
    v.kind = VAL_STRING;
    v.as.string.data = s;
    v.as.string.length = len;
    return v;
}
static inline AglVal val_array(AglArrayVal *a) {
    AglVal v; v.kind = VAL_ARRAY; v.as.array = a; return v;
}
static inline AglVal val_map(AglMapVal *m) {
    AglVal v; v.kind = VAL_MAP; v.as.map = m; return v;
}
static inline AglVal val_result(AglResultVal *r) {
    AglVal v; v.kind = VAL_RESULT; v.as.result = r; return v;
}

/* ---- Environment (variable bindings) ---- */

#define MAX_VARS 256

typedef struct {
    const char *names[MAX_VARS];
    int name_lengths[MAX_VARS];
    AglVal values[MAX_VARS];
    bool immutable[MAX_VARS];   /* true for let, false for var */
    int count;
} AglEnv;

/* ---- Interpreter state ---- */

#define MAX_MODULES 64
#define MAX_CALL_DEPTH 512

typedef struct {
    const char *name;
    int name_len;
    int line;
    int column;
} AglCallFrame;

typedef struct {
    char *path;
    char *source;       /* malloc'd source text — AST tokens point into this */
    AglArena *arena;    /* AST nodes live here */
} AglModule;

typedef struct {
    AglEnv env;
    AglCtx *ctx;
    AglArena *arena;    /* temporary allocations */
    AglGc *gc;          /* GC-tracked runtime objects */
    const char *file;   /* current file path (for import resolution) */
    AglModule modules[MAX_MODULES];
    int module_count;
    bool has_return;
    AglVal return_value;
    jmp_buf return_jmp;
    bool return_jmp_set;
    int call_depth;
    AglCallFrame call_frames[MAX_CALL_DEPTH];
} AglInterp;

/* ---- runtime.c ---- */

const char *str_content(AglVal s, int *out_len);
void array_cleanup(void *p);
void fn_cleanup(void *p);
void map_cleanup(void *p);

void env_init(AglEnv *env);
bool env_define(AglEnv *env, const char *name, int length, AglVal val,
                bool is_immutable);
AglVal *env_get(AglEnv *env, const char *name, int length);
/* Returns: 0=ok, 1=not found, 2=immutable */
int env_assign(AglEnv *env, const char *name, int length, AglVal val);

void mark_val(AglVal val);
void gc_collect(AglInterp *interp);

bool is_truthy(AglVal val);
void print_val_inline(AglVal val);
void builtin_print(AglVal val);

/* Trace capture callback for agl_error_set */
void capture_trace(void *data, AglError *err);

AglVal call_fn_direct(AglInterp *interp, AglFnVal *fn, AglVal *args,
                      int arg_count, int line, int column);

/* ---- builtins.c ---- */

/* Try to dispatch a built-in function call.
 * Returns true if name matched a builtin (result stored in *out).
 * Returns false if not a builtin — caller should try user functions. */
bool try_builtin_call(AglInterp *interp, const char *name, int name_len,
                      AglNode *call_node, AglVal *out);

/* ---- modules.c ---- */

void path_dir(const char *filepath, char *buf, size_t bufsize);
bool resolve_import_path(const char *base_file, const char *import_path,
                         int import_len, char *buf, size_t bufsize);
char *agl_read_file(const char *path);
void module_cache_free(AglInterp *interp);
void exec_import(AglInterp *interp, AglNode *node);

/* ---- interpreter.c ---- */

AglVal eval_expr(AglInterp *interp, AglNode *node);
void exec_stmt(AglInterp *interp, AglNode *node);
