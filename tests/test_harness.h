#pragma once

#include <stdio.h>
#include <string.h>

typedef struct {
    int passes;
    int failures;
} AglTestCtx;

#define AGL_TEST(name) static void name(AglTestCtx *ctx)

#define AGL_ASSERT(ctx, cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        (ctx)->failures++; \
    } else { \
        (ctx)->passes++; \
    } \
} while(0)

/* Fatal assert — returns from test function on failure (use before dereference) */
#define AGL_ASSERT_FATAL(ctx, cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s (fatal)\n", __FILE__, __LINE__, #cond); \
        (ctx)->failures++; \
        return; \
    } else { \
        (ctx)->passes++; \
    } \
} while(0)

#define AGL_ASSERT_INT_EQ(ctx, actual, expected) do { \
    int _a = (actual), _e = (expected); \
    if (_a != _e) { \
        printf("[FAIL] %s:%d: expected %d, got %d\n", __FILE__, __LINE__, _e, _a); \
        (ctx)->failures++; \
    } else { \
        (ctx)->passes++; \
    } \
} while(0)

#define AGL_ASSERT_STR_EQ(ctx, actual, expected) do { \
    const char *_a = (actual), *_e = (expected); \
    if (strcmp(_a, _e) != 0) { \
        printf("[FAIL] %s:%d: expected \"%s\", got \"%s\"\n", __FILE__, __LINE__, _e, _a); \
        (ctx)->failures++; \
    } else { \
        (ctx)->passes++; \
    } \
} while(0)

#define AGL_RUN_TEST(ctx, name) do { \
    int _before = (ctx)->failures; \
    name(ctx); \
    if ((ctx)->failures == _before) printf("[PASS] %s\n", #name); \
} while(0)

#define AGL_SUMMARY(ctx) do { \
    printf("\n--- %d passed, %d failed ---\n", (ctx)->passes, (ctx)->failures); \
    return (ctx)->failures > 0 ? 1 : 0; \
} while(0)
