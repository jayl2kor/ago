#pragma once

#include "common.h"
#include "error.h"

/* ---------------- Token types ---------------- */

typedef enum {
    /* Literals */
    AGO_TOKEN_INT,          /* 42 */
    AGO_TOKEN_FLOAT,        /* 3.14 */
    AGO_TOKEN_STRING,       /* "hello" */
    AGO_TOKEN_TRUE,         /* true */
    AGO_TOKEN_FALSE,        /* false */

    /* Identifier */
    AGO_TOKEN_IDENT,        /* foo, bar, print */

    /* Keywords */
    AGO_TOKEN_LET,          /* let */
    AGO_TOKEN_VAR,          /* var */
    AGO_TOKEN_FN,           /* fn */
    AGO_TOKEN_RETURN,       /* return */
    AGO_TOKEN_IF,           /* if */
    AGO_TOKEN_ELSE,         /* else */
    AGO_TOKEN_WHILE,        /* while */
    AGO_TOKEN_FOR,          /* for */
    AGO_TOKEN_IN,           /* in */
    AGO_TOKEN_STRUCT,       /* struct */
    AGO_TOKEN_MATCH,        /* match */
    AGO_TOKEN_OK,           /* ok */
    AGO_TOKEN_ERR,          /* err */
    AGO_TOKEN_BREAK,        /* break */
    AGO_TOKEN_CONTINUE,     /* continue */

    /* Operators */
    AGO_TOKEN_PLUS,         /* + */
    AGO_TOKEN_MINUS,        /* - */
    AGO_TOKEN_STAR,         /* * */
    AGO_TOKEN_SLASH,        /* / */
    AGO_TOKEN_PERCENT,      /* % */
    AGO_TOKEN_ASSIGN,       /* = */
    AGO_TOKEN_EQ,           /* == */
    AGO_TOKEN_NEQ,          /* != */
    AGO_TOKEN_LT,           /* < */
    AGO_TOKEN_GT,           /* > */
    AGO_TOKEN_LE,           /* <= */
    AGO_TOKEN_GE,           /* >= */
    AGO_TOKEN_AND,          /* && */
    AGO_TOKEN_OR,           /* || */
    AGO_TOKEN_NOT,          /* ! */
    AGO_TOKEN_ARROW,        /* -> */
    AGO_TOKEN_DOT,          /* . */

    /* Delimiters */
    AGO_TOKEN_LPAREN,       /* ( */
    AGO_TOKEN_RPAREN,       /* ) */
    AGO_TOKEN_LBRACE,       /* { */
    AGO_TOKEN_RBRACE,       /* } */
    AGO_TOKEN_LBRACKET,     /* [ */
    AGO_TOKEN_RBRACKET,     /* ] */
    AGO_TOKEN_COMMA,        /* , */
    AGO_TOKEN_COLON,        /* : */

    /* Special */
    AGO_TOKEN_NEWLINE,      /* auto-inserted semicolon (statement terminator) */
    AGO_TOKEN_EOF,
    AGO_TOKEN_ERROR,        /* lexer error */
} AgoTokenKind;

/* ---------------- Token ---------------- */

typedef struct {
    AgoTokenKind kind;
    const char *start;      /* pointer into source (not owned) */
    int length;
    int line;
    int column;
} AgoToken;

/* ---------------- Lexer ---------------- */

typedef struct {
    const char *source;     /* full source text (not owned) */
    const char *current;    /* current position */
    const char *file;       /* filename for error reporting */
    int line;
    int column;
    int paren_depth;        /* ( [ { depth — suppresses newline insertion */
    bool insert_newline;    /* whether next newline should become a token */
    AgoCtx *ctx;            /* error context (not owned) */
} AgoLexer;

/* Initialize a lexer for the given source text */
void ago_lexer_init(AgoLexer *lexer, const char *source, const char *file,
                    AgoCtx *ctx);

/* Get the next token. Returns AGO_TOKEN_EOF at end. */
AgoToken ago_lexer_next_token(AgoLexer *lexer);

/* Get a human-readable name for a token kind */
const char *ago_token_kind_name(AgoTokenKind kind);
