#include "gc.h"
#include <stdlib.h>
#include <string.h>

#define GC_INITIAL_THRESHOLD (1024 * 1024) /* 1 MB */
#define GC_GROWTH_FACTOR 2

AglGc *agl_gc_new(void) {
    AglGc *gc = calloc(1, sizeof(AglGc));
    if (!gc) return NULL;
    gc->next_gc = GC_INITIAL_THRESHOLD;
    return gc;
}

void agl_gc_free(AglGc *gc) {
    if (!gc) return;
    AglObj *obj = gc->objects;
    while (obj) {
        AglObj *next = obj->next;
        if (obj->cleanup) obj->cleanup(obj);
        free(obj);
        obj = next;
    }
    free(gc);
}

void *agl_gc_alloc(AglGc *gc, size_t size, void (*cleanup)(void *)) {
    void *mem = calloc(1, size);
    if (!mem) return NULL;
    AglObj *obj = mem;
    obj->next = gc->objects;
    obj->size = size;
    obj->marked = false;
    obj->cleanup = cleanup;
    gc->objects = obj;
    gc->object_count++;
    gc->bytes_allocated += size;
    return mem;
}

void agl_gc_sweep(AglGc *gc) {
    AglObj **p = &gc->objects;
    while (*p) {
        if (!(*p)->marked) {
            AglObj *unreached = *p;
            *p = unreached->next;
            gc->bytes_allocated -= unreached->size;
            if (unreached->cleanup) unreached->cleanup(unreached);
            free(unreached);
            gc->object_count--;
        } else {
            (*p)->marked = false;
            p = &(*p)->next;
        }
    }
    /* Grow threshold after collection */
    gc->next_gc = gc->bytes_allocated * GC_GROWTH_FACTOR;
    if (gc->next_gc < GC_INITIAL_THRESHOLD) gc->next_gc = GC_INITIAL_THRESHOLD;
}

int agl_gc_object_count(const AglGc *gc) {
    return gc->object_count;
}
