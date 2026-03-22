#pragma once

#include "common.h"
#include "lexer.h"
#include "ast.h"
#include "arena.h"
#include "error.h"

/* ---- Parser ---- */

typedef struct {
    AgoLexer lexer;
    AgoToken current;
    AgoToken previous;
    AgoArena *arena;    /* owns all AST nodes */
    AgoCtx *ctx;        /* error context (not owned) */
} AgoParser;

/* Initialize a parser */
void ago_parser_init(AgoParser *parser, const char *source, const char *file,
                     AgoArena *arena, AgoCtx *ctx);

/* Parse the entire source into a program node */
AgoNode *ago_parser_parse(AgoParser *parser);

/* Parse a single expression (useful for testing) */
AgoNode *ago_parser_parse_expression(AgoParser *parser);
