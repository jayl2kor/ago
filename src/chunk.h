#pragma once

#include "common.h"

/* ---- Bytecode opcodes ---- */

typedef enum {
    /* Constants */
    AGL_OP_CONST,          /* 2B idx -> push constants[idx] */
    AGL_OP_NIL,            /* push nil */
    AGL_OP_TRUE,           /* push true */
    AGL_OP_FALSE,          /* push false */

    /* Arithmetic (pop 2, push 1) */
    AGL_OP_ADD,
    AGL_OP_SUB,
    AGL_OP_MUL,
    AGL_OP_DIV,
    AGL_OP_MOD,
    AGL_OP_NEGATE,         /* unary - (pop 1, push 1) */

    /* Comparison (pop 2, push bool) */
    AGL_OP_EQ,
    AGL_OP_NEQ,
    AGL_OP_LT,
    AGL_OP_GT,
    AGL_OP_LE,
    AGL_OP_GE,

    /* Logic */
    AGL_OP_NOT,            /* unary ! (pop 1, push 1) */

    /* Variables (name-based) */
    AGL_OP_DEFINE_LET,     /* 2B name_idx -> define immutable from TOS */
    AGL_OP_DEFINE_VAR,     /* 2B name_idx -> define mutable from TOS */
    AGL_OP_GET_VAR,        /* 2B name_idx -> push env lookup */
    AGL_OP_SET_VAR,        /* 2B name_idx -> assign TOS to existing var */

    /* Stack */
    AGL_OP_POP,            /* discard TOS */
    AGL_OP_POP_SCOPE,      /* 1B count -> env.count -= count */

    /* Control flow */
    AGL_OP_JUMP,           /* 2B offset -> unconditional forward jump */
    AGL_OP_JUMP_BACK,      /* 2B offset -> unconditional backward jump */
    AGL_OP_JUMP_IF_FALSE,  /* 2B offset -> pop, jump if falsy */
    AGL_OP_JUMP_IF_TRUE,   /* 2B offset -> pop, jump if truthy */

    /* Functions */
    AGL_OP_CLOSURE,        /* 2B func_idx -> create closure from constant */
    AGL_OP_CALL,           /* 1B argc -> call function on stack */
    AGL_OP_RETURN,         /* return TOS */
    AGL_OP_RETURN_NIL,     /* return nil */

    /* Builtins */
    AGL_OP_CALL_BUILTIN,   /* 2B builtin_id, 1B argc */

    /* Compound types */
    AGL_OP_ARRAY,          /* 2B count -> pop elements, push array */
    AGL_OP_INDEX,          /* pop index + array, push element */
    AGL_OP_STRUCT,         /* 2B type_name_idx, 1B field_count */
    AGL_OP_GET_FIELD,      /* 2B field_name_idx -> pop struct, push field */
    AGL_OP_MAP,            /* 2B count -> pop count key-value pairs, push map */

    /* Result types */
    AGL_OP_RESULT_OK,      /* pop value, push ok(value) */
    AGL_OP_RESULT_ERR,     /* pop value, push err(value) */
    AGL_OP_MATCH,          /* 2B ok_name, 2B err_name, 2B ok_offset, 2B err_offset */
    AGL_OP_TRY,            /* pop result: if ok push value, if err push err and return */

    /* For-in loop */
    AGL_OP_ITER_SETUP,     /* pop array -> push (array, len, 0) */
    AGL_OP_ITER_NEXT,      /* 2B end_offset -> push element or jump */
    AGL_OP_ITER_CLEANUP,   /* pop iteration state (3 values) */

    /* Module */
    AGL_OP_IMPORT,         /* 2B path_idx */

    /* Debug */
    AGL_OP_LINE,           /* 2B line_number */
} AglOpCode;

/* ---- Bytecode chunk ---- */

/* Forward declaration — AglVal defined in runtime.h */
struct AglVal;

typedef struct AglChunk {
    uint8_t *code;
    int code_count;
    int code_capacity;

    struct AglVal *constants;
    int const_count;
    int const_capacity;
} AglChunk;

AglChunk *agl_chunk_new(void);
void agl_chunk_free(AglChunk *chunk);

/* Write a single byte to the chunk */
void agl_chunk_write(AglChunk *chunk, uint8_t byte);

/* Write a 2-byte little-endian uint16 */
void agl_chunk_write_u16(AglChunk *chunk, uint16_t value);

/* Add a constant to the pool, return its index */
int agl_chunk_add_const(AglChunk *chunk, struct AglVal val);

/* Emit a jump instruction, return the offset to patch later */
int agl_chunk_emit_jump(AglChunk *chunk, uint8_t op);

/* Patch a previously emitted jump to target current position */
void agl_chunk_patch_jump(AglChunk *chunk, int offset);

/* Emit a backward jump to the given target position */
void agl_chunk_emit_loop(AglChunk *chunk, int loop_start);
