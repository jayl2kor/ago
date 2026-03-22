#pragma once

#include "common.h"

/*
 * Arena allocator for compiler phases.
 * Allocate many objects, free them all at once when done.
 * No individual free — the entire arena is released together.
 */

#define AGO_ARENA_BLOCK_SIZE (8 * 1024)  /* 8 KB per block */

typedef struct AgoArenaBlock {
    struct AgoArenaBlock *next;
    size_t size;
    size_t used;
    char data[];  /* flexible array member */
} AgoArenaBlock;

struct AgoArena {
    AgoArenaBlock *head;
};

/* Create a new arena */
AgoArena *ago_arena_new(void);

/* Allocate `size` bytes from the arena (8-byte aligned) */
void *ago_arena_alloc(AgoArena *arena, size_t size);

/* Free the entire arena and all its allocations */
void ago_arena_free(AgoArena *arena);
