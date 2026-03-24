#include "test_harness.h"
#include "../src/gc.h"

/* ---- Basic lifecycle ---- */

AGL_TEST(test_gc_new_free) {
    AglGc *gc = agl_gc_new();
    AGL_ASSERT(ctx, gc != NULL);
    AGL_ASSERT_INT_EQ(ctx, agl_gc_object_count(gc), 0);
    agl_gc_free(gc);
}

AGL_TEST(test_gc_alloc_one) {
    AglGc *gc = agl_gc_new();
    void *obj = agl_gc_alloc(gc, sizeof(AglObj) + 64, NULL);
    AGL_ASSERT(ctx, obj != NULL);
    AGL_ASSERT_INT_EQ(ctx, agl_gc_object_count(gc), 1);
    agl_gc_free(gc);
}

AGL_TEST(test_gc_alloc_multiple) {
    AglGc *gc = agl_gc_new();
    agl_gc_alloc(gc, sizeof(AglObj) + 32, NULL);
    agl_gc_alloc(gc, sizeof(AglObj) + 32, NULL);
    agl_gc_alloc(gc, sizeof(AglObj) + 32, NULL);
    AGL_ASSERT_INT_EQ(ctx, agl_gc_object_count(gc), 3);
    agl_gc_free(gc);
}

/* ---- Sweep ---- */

AGL_TEST(test_gc_sweep_all_unmarked) {
    AglGc *gc = agl_gc_new();
    agl_gc_alloc(gc, sizeof(AglObj) + 32, NULL);
    agl_gc_alloc(gc, sizeof(AglObj) + 32, NULL);
    AGL_ASSERT_INT_EQ(ctx, agl_gc_object_count(gc), 2);
    agl_gc_sweep(gc);
    AGL_ASSERT_INT_EQ(ctx, agl_gc_object_count(gc), 0);
    agl_gc_free(gc);
}

AGL_TEST(test_gc_sweep_keeps_marked) {
    AglGc *gc = agl_gc_new();
    AglObj *obj1 = agl_gc_alloc(gc, sizeof(AglObj) + 32, NULL);
    agl_gc_alloc(gc, sizeof(AglObj) + 32, NULL);
    AGL_ASSERT_INT_EQ(ctx, agl_gc_object_count(gc), 2);
    agl_gc_mark(obj1);
    agl_gc_sweep(gc);
    AGL_ASSERT_INT_EQ(ctx, agl_gc_object_count(gc), 1);
    agl_gc_free(gc);
}

AGL_TEST(test_gc_sweep_resets_marks) {
    AglGc *gc = agl_gc_new();
    AglObj *obj = agl_gc_alloc(gc, sizeof(AglObj) + 32, NULL);
    agl_gc_mark(obj);
    AGL_ASSERT(ctx, obj->marked);
    agl_gc_sweep(gc);
    AGL_ASSERT(ctx, !obj->marked);
    agl_gc_free(gc);
}

AGL_TEST(test_gc_sweep_partial) {
    AglGc *gc = agl_gc_new();
    AglObj *a = agl_gc_alloc(gc, sizeof(AglObj) + 32, NULL);
    agl_gc_alloc(gc, sizeof(AglObj) + 32, NULL);
    AglObj *c = agl_gc_alloc(gc, sizeof(AglObj) + 32, NULL);
    agl_gc_alloc(gc, sizeof(AglObj) + 32, NULL);
    AGL_ASSERT_INT_EQ(ctx, agl_gc_object_count(gc), 4);
    agl_gc_mark(a);
    agl_gc_mark(c);
    agl_gc_sweep(gc);
    AGL_ASSERT_INT_EQ(ctx, agl_gc_object_count(gc), 2);
    agl_gc_free(gc);
}

/* ---- Cleanup callback ---- */

static int cleanup_count = 0;
static void test_cleanup(void *obj) { (void)obj; cleanup_count++; }

AGL_TEST(test_gc_cleanup_on_sweep) {
    cleanup_count = 0;
    AglGc *gc = agl_gc_new();
    agl_gc_alloc(gc, sizeof(AglObj) + 32, test_cleanup);
    agl_gc_alloc(gc, sizeof(AglObj) + 32, test_cleanup);
    agl_gc_sweep(gc);
    AGL_ASSERT_INT_EQ(ctx, cleanup_count, 2);
    agl_gc_free(gc);
}

AGL_TEST(test_gc_cleanup_on_free) {
    cleanup_count = 0;
    AglGc *gc = agl_gc_new();
    agl_gc_alloc(gc, sizeof(AglObj) + 32, test_cleanup);
    agl_gc_alloc(gc, sizeof(AglObj) + 32, test_cleanup);
    agl_gc_free(gc);
    AGL_ASSERT_INT_EQ(ctx, cleanup_count, 2);
}

AGL_TEST(test_gc_cleanup_skipped_for_marked) {
    cleanup_count = 0;
    AglGc *gc = agl_gc_new();
    AglObj *obj = agl_gc_alloc(gc, sizeof(AglObj) + 32, test_cleanup);
    agl_gc_alloc(gc, sizeof(AglObj) + 32, test_cleanup);
    agl_gc_mark(obj);
    agl_gc_sweep(gc);
    AGL_ASSERT_INT_EQ(ctx, cleanup_count, 1);
    agl_gc_free(gc);
}

/* ---- Threshold ---- */

AGL_TEST(test_gc_should_collect) {
    AglGc *gc = agl_gc_new();
    AGL_ASSERT(ctx, !agl_gc_should_collect(gc));
    /* Allocate past threshold */
    while (!agl_gc_should_collect(gc)) {
        agl_gc_alloc(gc, 1024, NULL);
    }
    AGL_ASSERT(ctx, agl_gc_should_collect(gc));
    agl_gc_free(gc);
}

/* ---- Mark null safety ---- */

AGL_TEST(test_gc_mark_null) {
    /* Should not crash */
    agl_gc_mark(NULL);
    AGL_ASSERT(ctx, true);
}

/* ---- Main ---- */

int main(void) {
    AglTestCtx ctx = {0, 0};

    printf("=== GC Tests ===\n");

    AGL_RUN_TEST(&ctx, test_gc_new_free);
    AGL_RUN_TEST(&ctx, test_gc_alloc_one);
    AGL_RUN_TEST(&ctx, test_gc_alloc_multiple);

    AGL_RUN_TEST(&ctx, test_gc_sweep_all_unmarked);
    AGL_RUN_TEST(&ctx, test_gc_sweep_keeps_marked);
    AGL_RUN_TEST(&ctx, test_gc_sweep_resets_marks);
    AGL_RUN_TEST(&ctx, test_gc_sweep_partial);

    AGL_RUN_TEST(&ctx, test_gc_cleanup_on_sweep);
    AGL_RUN_TEST(&ctx, test_gc_cleanup_on_free);
    AGL_RUN_TEST(&ctx, test_gc_cleanup_skipped_for_marked);

    AGL_RUN_TEST(&ctx, test_gc_should_collect);
    AGL_RUN_TEST(&ctx, test_gc_mark_null);

    AGL_SUMMARY(&ctx);
}
