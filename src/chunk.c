#include "runtime.h"
#include "chunk.h"

AglChunk *agl_chunk_new(void) {
    AglChunk *chunk = calloc(1, sizeof(AglChunk));
    return chunk;
}

void agl_chunk_free(AglChunk *chunk) {
    if (!chunk) return;
    free(chunk->code);
    free(chunk->constants);
    free(chunk);
}

void agl_chunk_write(AglChunk *chunk, uint8_t byte) {
    if (chunk->code_count >= chunk->code_capacity) {
        int cap = chunk->code_capacity < 8 ? 8 : chunk->code_capacity * 2;
        chunk->code = realloc(chunk->code, (size_t)cap);
        chunk->code_capacity = cap;
    }
    chunk->code[chunk->code_count++] = byte;
}

void agl_chunk_write_u16(AglChunk *chunk, uint16_t value) {
    agl_chunk_write(chunk, (uint8_t)(value & 0xff));
    agl_chunk_write(chunk, (uint8_t)(value >> 8));
}

int agl_chunk_add_const(AglChunk *chunk, AglVal val) {
    if (chunk->const_count >= chunk->const_capacity) {
        int cap = chunk->const_capacity < 8 ? 8 : chunk->const_capacity * 2;
        chunk->constants = realloc(chunk->constants, sizeof(AglVal) * (size_t)cap);
        chunk->const_capacity = cap;
    }
    chunk->constants[chunk->const_count] = val;
    return chunk->const_count++;
}

int agl_chunk_emit_jump(AglChunk *chunk, uint8_t op) {
    agl_chunk_write(chunk, op);
    /* Placeholder 2 bytes for the jump offset */
    int offset = chunk->code_count;
    agl_chunk_write(chunk, 0xff);
    agl_chunk_write(chunk, 0xff);
    return offset;
}

void agl_chunk_patch_jump(AglChunk *chunk, int offset) {
    /* Calculate how far to jump from the instruction after the jump operand */
    int jump = chunk->code_count - offset - 2;
    chunk->code[offset] = (uint8_t)(jump & 0xff);
    chunk->code[offset + 1] = (uint8_t)(jump >> 8);
}

void agl_chunk_emit_loop(AglChunk *chunk, int loop_start) {
    agl_chunk_write(chunk, AGL_OP_JUMP_BACK);
    int offset = chunk->code_count - loop_start + 2;
    agl_chunk_write_u16(chunk, (uint16_t)offset);
}
