#pragma once

#include "common.h"

/*
 * Arena allocator for compiler phases.
 * Allocate many objects, free them all at once when done.
 * No individual free — the entire arena is released together.
 */

#define AGL_ARENA_BLOCK_SIZE (8 * 1024)  /* 8 KB per block */

typedef struct AglArenaBlock {
    struct AglArenaBlock *next;
    size_t size;
    size_t used;
    char data[];  /* flexible array member */
} AglArenaBlock;

struct AglArena {
    AglArenaBlock *head;
};

/* Create a new arena */
AglArena *agl_arena_new(void);

/* Allocate `size` bytes from the arena (8-byte aligned) */
void *agl_arena_alloc(AglArena *arena, size_t size);

/* Free the entire arena and all its allocations */
void agl_arena_free(AglArena *arena);
