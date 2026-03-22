#include "arena.h"

static AgoArenaBlock *arena_block_new(size_t min_size) {
    size_t size = min_size > AGO_ARENA_BLOCK_SIZE ? min_size : AGO_ARENA_BLOCK_SIZE;
    AgoArenaBlock *block = malloc(sizeof(AgoArenaBlock) + size);
    if (!block) return NULL;
    block->next = NULL;
    block->size = size;
    block->used = 0;
    return block;
}

AgoArena *ago_arena_new(void) {
    AgoArena *arena = malloc(sizeof(AgoArena));
    if (!arena) return NULL;
    arena->head = arena_block_new(AGO_ARENA_BLOCK_SIZE);
    if (!arena->head) {
        free(arena);
        return NULL;
    }
    return arena;
}

void *ago_arena_alloc(AgoArena *arena, size_t size) {
    if (!arena || size == 0) return NULL;

    /* Align to 8 bytes */
    size = (size + 7) & ~(size_t)7;

    AgoArenaBlock *block = arena->head;

    /* If current block has space, use it */
    if (block->used + size <= block->size) {
        void *ptr = block->data + block->used;
        block->used += size;
        return ptr;
    }

    /* Otherwise allocate a new block */
    AgoArenaBlock *new_block = arena_block_new(size);
    if (!new_block) return NULL;
    new_block->next = arena->head;
    arena->head = new_block;

    void *ptr = new_block->data;
    new_block->used = size;
    return ptr;
}

void ago_arena_free(AgoArena *arena) {
    if (!arena) return;
    AgoArenaBlock *block = arena->head;
    while (block) {
        AgoArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    free(arena);
}
