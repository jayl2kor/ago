#pragma once

#include "chunk.h"
#include "ast.h"
#include "error.h"
#include "arena.h"
#include "gc.h"

/* Compile an AST program into a bytecode chunk.
 * Returns NULL on compilation error (error set on ctx). */
AglChunk *agl_compile(AglNode *program, AglCtx *ctx, AglArena *arena, AglGc *gc);
