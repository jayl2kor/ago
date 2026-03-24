#pragma once

#include "common.h"
#include "ast.h"
#include "error.h"

/* Run an Agl program (AST) and capture stdout output.
 * Returns 0 on success, non-zero on error.
 * If output is non-NULL, stdout is captured into the buffer. */
int agl_interpret(AglNode *program, const char *filename, AglCtx *ctx);

/* Convenience: parse and run source code directly.
 * Returns 0 on success. */
int agl_run(const char *source, const char *filename, AglCtx *ctx);

/* ---- REPL (persistent interpreter state) ---- */

typedef struct AglRepl AglRepl;

AglRepl *agl_repl_new(void);
void agl_repl_free(AglRepl *repl);

/* Execute one chunk of source in the persistent REPL state.
 * Variables and functions persist across calls.
 * Returns 0 on success, -1 on error. Errors are printed to stderr.
 * The AglCtx is reset between calls (errors don't persist). */
int agl_repl_exec(AglRepl *repl, const char *source);
