#pragma once

#include "common.h"
#include "lexer.h"
#include "ast.h"
#include "arena.h"
#include "error.h"

/* ---- Parser ---- */

typedef struct {
    AglLexer lexer;
    AglToken current;
    AglToken previous;
    AglArena *arena;    /* owns all AST nodes */
    AglCtx *ctx;        /* error context (not owned) */
} AglParser;

/* Initialize a parser */
void agl_parser_init(AglParser *parser, const char *source, const char *file,
                     AglArena *arena, AglCtx *ctx);

/* Parse the entire source into a program node */
AglNode *agl_parser_parse(AglParser *parser);

/* Parse a single expression (useful for testing) */
AglNode *agl_parser_parse_expression(AglParser *parser);
