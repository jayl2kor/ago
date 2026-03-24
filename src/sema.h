#pragma once

#include "common.h"
#include "ast.h"
#include "error.h"

/* Semantic analysis: name resolution, immutability, arity checks.
 * Run after parsing, before interpretation.
 * Errors are reported via AglCtx. */

typedef struct AglSema AglSema;

/* Create / destroy */
AglSema *agl_sema_new(AglCtx *ctx, AglArena *arena);
void agl_sema_free(AglSema *sema);

/* Analyze a program AST. Returns true if no errors found. */
bool agl_sema_check(AglSema *sema, AglNode *program);
