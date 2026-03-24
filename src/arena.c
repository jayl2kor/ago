#include "arena.h"

static AglArenaBlock *arena_block_new(size_t min_size) {
    size_t size = min_size > AGL_ARENA_BLOCK_SIZE ? min_size : AGL_ARENA_BLOCK_SIZE;
    AglArenaBlock *block = malloc(sizeof(AglArenaBlock) + size);
    if (!block) return NULL;
    block->next = NULL;
    block->size = size;
    block->used = 0;
    return block;
}

AglArena *agl_arena_new(void) {
    AglArena *arena = malloc(sizeof(AglArena));
    if (!arena) return NULL;
    arena->head = arena_block_new(AGL_ARENA_BLOCK_SIZE);
    if (!arena->head) {
        free(arena);
        return NULL;
    }
    return arena;
}

void *agl_arena_alloc(AglArena *arena, size_t size) {
    if (!arena || size == 0) return NULL;

    /* Align to 8 bytes (guard against overflow near SIZE_MAX) */
    if (size > SIZE_MAX - 7) return NULL;
    size = (size + 7) & ~(size_t)7;

    AglArenaBlock *block = arena->head;

    /* If current block has space, use it */
    if (block->used + size <= block->size) {
        void *ptr = block->data + block->used;
        block->used += size;
        return ptr;
    }

    /* Otherwise allocate a new block */
    AglArenaBlock *new_block = arena_block_new(size);
    if (!new_block) return NULL;
    new_block->next = arena->head;
    arena->head = new_block;

    void *ptr = new_block->data;
    new_block->used = size;
    return ptr;
}

void agl_arena_free(AglArena *arena) {
    if (!arena) return;
    AglArenaBlock *block = arena->head;
    while (block) {
        AglArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    free(arena);
}
