#pragma once

#include "chunk.h"
#include "ast.h"
#include "error.h"

/* Run a compiled bytecode chunk (creates/destroys VM internally).
 * Returns 0 on success, -1 on error. */
int agl_vm_run(AglChunk *chunk, const char *filename, AglCtx *ctx);

/* Compile AST and execute via VM.
 * Drop-in replacement for agl_interpret. */
int agl_vm_interpret(AglNode *program, const char *filename, AglCtx *ctx);

/* VM-based REPL with persistent state */
typedef struct AglVmRepl AglVmRepl;

AglVmRepl *agl_vm_repl_new(void);
void agl_vm_repl_free(AglVmRepl *repl);
int agl_vm_repl_exec(AglVmRepl *repl, const char *source);
