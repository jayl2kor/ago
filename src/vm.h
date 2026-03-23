#pragma once

#include "chunk.h"
#include "error.h"

/* Run a compiled bytecode chunk.
 * Returns 0 on success, -1 on error (error set on ctx). */
int ago_vm_run(AgoChunk *chunk, const char *filename, AgoCtx *ctx);
