#pragma once

#include "chunk.h"
#include "ast.h"
#include "error.h"

/* Run a compiled bytecode chunk (creates/destroys VM internally).
 * Returns 0 on success, -1 on error. */
int ago_vm_run(AgoChunk *chunk, const char *filename, AgoCtx *ctx);

/* Compile AST and execute via VM.
 * Drop-in replacement for ago_interpret. */
int ago_vm_interpret(AgoNode *program, const char *filename, AgoCtx *ctx);

/* VM-based REPL with persistent state */
typedef struct AgoVmRepl AgoVmRepl;

AgoVmRepl *ago_vm_repl_new(void);
void ago_vm_repl_free(AgoVmRepl *repl);
int ago_vm_repl_exec(AgoVmRepl *repl, const char *source);
