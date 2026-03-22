#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#define AGO_VERSION "0.1.0"

/* Internal assertion — aborts on failure (for invariant violations, not user errors) */
#define AGO_INTERNAL_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ago: internal error: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        abort(); \
    } \
} while(0)

#define AGO_UNREACHABLE() do { \
    fprintf(stderr, "ago: unreachable code reached (%s:%d)\n", __FILE__, __LINE__); \
    abort(); \
} while(0)

/* Compare two non-null-terminated string slices */
static inline bool ago_str_eq(const char *a, int alen, const char *b, int blen) {
    return alen == blen && memcmp(a, b, (size_t)alen) == 0;
}

/* Forward declarations of core types */
typedef struct AgoCtx AgoCtx;
typedef struct AgoArena AgoArena;
typedef struct AgoError AgoError;
