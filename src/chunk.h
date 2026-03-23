#pragma once

#include "common.h"

/* ---- Bytecode opcodes ---- */

typedef enum {
    /* Constants */
    AGO_OP_CONST,          /* 2B idx -> push constants[idx] */
    AGO_OP_NIL,            /* push nil */
    AGO_OP_TRUE,           /* push true */
    AGO_OP_FALSE,          /* push false */

    /* Arithmetic (pop 2, push 1) */
    AGO_OP_ADD,
    AGO_OP_SUB,
    AGO_OP_MUL,
    AGO_OP_DIV,
    AGO_OP_MOD,
    AGO_OP_NEGATE,         /* unary - (pop 1, push 1) */

    /* Comparison (pop 2, push bool) */
    AGO_OP_EQ,
    AGO_OP_NEQ,
    AGO_OP_LT,
    AGO_OP_GT,
    AGO_OP_LE,
    AGO_OP_GE,

    /* Logic */
    AGO_OP_NOT,            /* unary ! (pop 1, push 1) */

    /* Variables (name-based) */
    AGO_OP_DEFINE_LET,     /* 2B name_idx -> define immutable from TOS */
    AGO_OP_DEFINE_VAR,     /* 2B name_idx -> define mutable from TOS */
    AGO_OP_GET_VAR,        /* 2B name_idx -> push env lookup */
    AGO_OP_SET_VAR,        /* 2B name_idx -> assign TOS to existing var */

    /* Stack */
    AGO_OP_POP,            /* discard TOS */
    AGO_OP_POP_SCOPE,      /* 1B count -> env.count -= count */

    /* Control flow */
    AGO_OP_JUMP,           /* 2B offset -> unconditional forward jump */
    AGO_OP_JUMP_BACK,      /* 2B offset -> unconditional backward jump */
    AGO_OP_JUMP_IF_FALSE,  /* 2B offset -> pop, jump if falsy */
    AGO_OP_JUMP_IF_TRUE,   /* 2B offset -> pop, jump if truthy */

    /* Functions */
    AGO_OP_CLOSURE,        /* 2B func_idx -> create closure from constant */
    AGO_OP_CALL,           /* 1B argc -> call function on stack */
    AGO_OP_RETURN,         /* return TOS */
    AGO_OP_RETURN_NIL,     /* return nil */

    /* Builtins */
    AGO_OP_CALL_BUILTIN,   /* 2B builtin_id, 1B argc */

    /* Compound types */
    AGO_OP_ARRAY,          /* 2B count -> pop elements, push array */
    AGO_OP_INDEX,          /* pop index + array, push element */
    AGO_OP_STRUCT,         /* 2B type_name_idx, 1B field_count */
    AGO_OP_GET_FIELD,      /* 2B field_name_idx -> pop struct, push field */

    /* Result types */
    AGO_OP_RESULT_OK,      /* pop value, push ok(value) */
    AGO_OP_RESULT_ERR,     /* pop value, push err(value) */
    AGO_OP_MATCH,          /* 2B ok_name, 2B err_name, 2B ok_offset, 2B err_offset */

    /* For-in loop */
    AGO_OP_ITER_SETUP,     /* pop array -> push (array, len, 0) */
    AGO_OP_ITER_NEXT,      /* 2B end_offset -> push element or jump */
    AGO_OP_ITER_CLEANUP,   /* pop iteration state (3 values) */

    /* Module */
    AGO_OP_IMPORT,         /* 2B path_idx */

    /* Debug */
    AGO_OP_LINE,           /* 2B line_number */
} AgoOpCode;

/* ---- Bytecode chunk ---- */

/* Forward declaration — AgoVal defined in runtime.h */
struct AgoVal;

typedef struct AgoChunk {
    uint8_t *code;
    int code_count;
    int code_capacity;

    struct AgoVal *constants;
    int const_count;
    int const_capacity;
} AgoChunk;

AgoChunk *ago_chunk_new(void);
void ago_chunk_free(AgoChunk *chunk);

/* Write a single byte to the chunk */
void ago_chunk_write(AgoChunk *chunk, uint8_t byte);

/* Write a 2-byte little-endian uint16 */
void ago_chunk_write_u16(AgoChunk *chunk, uint16_t value);

/* Add a constant to the pool, return its index */
int ago_chunk_add_const(AgoChunk *chunk, struct AgoVal val);

/* Emit a jump instruction, return the offset to patch later */
int ago_chunk_emit_jump(AgoChunk *chunk, uint8_t op);

/* Patch a previously emitted jump to target current position */
void ago_chunk_patch_jump(AgoChunk *chunk, int offset);

/* Emit a backward jump to the given target position */
void ago_chunk_emit_loop(AgoChunk *chunk, int loop_start);
