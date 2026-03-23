#include "runtime.h"
#include "chunk.h"

AgoChunk *ago_chunk_new(void) {
    AgoChunk *chunk = calloc(1, sizeof(AgoChunk));
    return chunk;
}

void ago_chunk_free(AgoChunk *chunk) {
    if (!chunk) return;
    free(chunk->code);
    free(chunk->constants);
    free(chunk);
}

void ago_chunk_write(AgoChunk *chunk, uint8_t byte) {
    if (chunk->code_count >= chunk->code_capacity) {
        int cap = chunk->code_capacity < 8 ? 8 : chunk->code_capacity * 2;
        chunk->code = realloc(chunk->code, (size_t)cap);
        chunk->code_capacity = cap;
    }
    chunk->code[chunk->code_count++] = byte;
}

void ago_chunk_write_u16(AgoChunk *chunk, uint16_t value) {
    ago_chunk_write(chunk, (uint8_t)(value & 0xff));
    ago_chunk_write(chunk, (uint8_t)(value >> 8));
}

int ago_chunk_add_const(AgoChunk *chunk, AgoVal val) {
    if (chunk->const_count >= chunk->const_capacity) {
        int cap = chunk->const_capacity < 8 ? 8 : chunk->const_capacity * 2;
        chunk->constants = realloc(chunk->constants, sizeof(AgoVal) * (size_t)cap);
        chunk->const_capacity = cap;
    }
    chunk->constants[chunk->const_count] = val;
    return chunk->const_count++;
}

int ago_chunk_emit_jump(AgoChunk *chunk, uint8_t op) {
    ago_chunk_write(chunk, op);
    /* Placeholder 2 bytes for the jump offset */
    int offset = chunk->code_count;
    ago_chunk_write(chunk, 0xff);
    ago_chunk_write(chunk, 0xff);
    return offset;
}

void ago_chunk_patch_jump(AgoChunk *chunk, int offset) {
    /* Calculate how far to jump from the instruction after the jump operand */
    int jump = chunk->code_count - offset - 2;
    chunk->code[offset] = (uint8_t)(jump & 0xff);
    chunk->code[offset + 1] = (uint8_t)(jump >> 8);
}

void ago_chunk_emit_loop(AgoChunk *chunk, int loop_start) {
    ago_chunk_write(chunk, AGO_OP_JUMP_BACK);
    int offset = chunk->code_count - loop_start + 2;
    ago_chunk_write_u16(chunk, (uint16_t)offset);
}
