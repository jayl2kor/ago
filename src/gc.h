#pragma once

#include <stdbool.h>
#include <stddef.h>

/* GC object header — must be first field of every GC-tracked object.
 * The interpreter casts between AglObj* and the concrete type. */
typedef struct AglObj {
    struct AglObj *next;            /* intrusive linked list of all objects */
    size_t size;                    /* allocation size for GC accounting */
    void (*cleanup)(void *obj);     /* free internal buffers before sweep */
    bool marked;
} AglObj;

/* GC state */
typedef struct {
    AglObj *objects;        /* head of all-objects linked list */
    int object_count;
    size_t bytes_allocated;
    size_t next_gc;         /* collection threshold in bytes */
} AglGc;

/* Create / destroy */
AglGc *agl_gc_new(void);
void agl_gc_free(AglGc *gc);

/* Allocate a GC-tracked object of `size` bytes (includes AglObj header).
 * `cleanup` is called before free (NULL if none needed).
 * Returns pointer to the object (first field is AglObj). */
void *agl_gc_alloc(AglGc *gc, size_t size, void (*cleanup)(void *));

/* Mark an object as reachable (no-op if NULL or already marked) */
static inline void agl_gc_mark(AglObj *obj) {
    if (obj && !obj->marked) obj->marked = true;
}

/* Sweep: free all unmarked objects, reset marks on survivors */
void agl_gc_sweep(AglGc *gc);

/* Number of live tracked objects */
int agl_gc_object_count(const AglGc *gc);

/* Check if collection should run (bytes_allocated > next_gc) */
static inline bool agl_gc_should_collect(const AglGc *gc) {
    return gc->bytes_allocated > gc->next_gc;
}
