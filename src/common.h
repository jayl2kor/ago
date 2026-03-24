#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#define AGL_VERSION "0.1.0"

/* Internal assertion — aborts on failure (for invariant violations, not user errors) */
#define AGL_INTERNAL_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "agl: internal error: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        abort(); \
    } \
} while(0)

#define AGL_UNREACHABLE() do { \
    fprintf(stderr, "agl: unreachable code reached (%s:%d)\n", __FILE__, __LINE__); \
    abort(); \
} while(0)

/* Compare two non-null-terminated string slices */
static inline bool agl_str_eq(const char *a, int alen, const char *b, int blen) {
    return alen == blen && memcmp(a, b, (size_t)alen) == 0;
}

/* Forward declarations of core types */
typedef struct AglCtx AglCtx;
typedef struct AglArena AglArena;
typedef struct AglError AglError;
