#pragma once

#include "common.h"
#include "error.h"

/* ---------------- Token types ---------------- */

typedef enum {
    /* Literals */
    AGL_TOKEN_INT,          /* 42 */
    AGL_TOKEN_FLOAT,        /* 3.14 */
    AGL_TOKEN_STRING,       /* "hello" */
    AGL_TOKEN_TRUE,         /* true */
    AGL_TOKEN_FALSE,        /* false */

    /* Identifier */
    AGL_TOKEN_IDENT,        /* foo, bar, print */

    /* Keywords */
    AGL_TOKEN_LET,          /* let */
    AGL_TOKEN_VAR,          /* var */
    AGL_TOKEN_FN,           /* fn */
    AGL_TOKEN_RETURN,       /* return */
    AGL_TOKEN_IF,           /* if */
    AGL_TOKEN_ELSE,         /* else */
    AGL_TOKEN_WHILE,        /* while */
    AGL_TOKEN_FOR,          /* for */
    AGL_TOKEN_IN,           /* in */
    AGL_TOKEN_STRUCT,       /* struct */
    AGL_TOKEN_MATCH,        /* match */
    AGL_TOKEN_OK,           /* ok */
    AGL_TOKEN_ERR,          /* err */
    AGL_TOKEN_IMPORT,       /* import */
    AGL_TOKEN_BREAK,        /* break */
    AGL_TOKEN_CONTINUE,     /* continue */

    /* Operators */
    AGL_TOKEN_PLUS,         /* + */
    AGL_TOKEN_MINUS,        /* - */
    AGL_TOKEN_STAR,         /* * */
    AGL_TOKEN_SLASH,        /* / */
    AGL_TOKEN_PERCENT,      /* % */
    AGL_TOKEN_ASSIGN,       /* = */
    AGL_TOKEN_EQ,           /* == */
    AGL_TOKEN_NEQ,          /* != */
    AGL_TOKEN_LT,           /* < */
    AGL_TOKEN_GT,           /* > */
    AGL_TOKEN_LE,           /* <= */
    AGL_TOKEN_GE,           /* >= */
    AGL_TOKEN_AND,          /* && */
    AGL_TOKEN_OR,           /* || */
    AGL_TOKEN_NOT,          /* ! */
    AGL_TOKEN_ARROW,        /* -> */
    AGL_TOKEN_DOT,          /* . */
    AGL_TOKEN_QUESTION,     /* ? */

    /* Delimiters */
    AGL_TOKEN_LPAREN,       /* ( */
    AGL_TOKEN_RPAREN,       /* ) */
    AGL_TOKEN_LBRACE,       /* { */
    AGL_TOKEN_RBRACE,       /* } */
    AGL_TOKEN_LBRACKET,     /* [ */
    AGL_TOKEN_RBRACKET,     /* ] */
    AGL_TOKEN_COMMA,        /* , */
    AGL_TOKEN_COLON,        /* : */

    /* Special */
    AGL_TOKEN_NEWLINE,      /* auto-inserted semicolon (statement terminator) */
    AGL_TOKEN_EOF,
    AGL_TOKEN_ERROR,        /* lexer error */
} AglTokenKind;

/* ---------------- Token ---------------- */

typedef struct {
    AglTokenKind kind;
    const char *start;      /* pointer into source (not owned) */
    int length;
    int line;
    int column;
} AglToken;

/* ---------------- Lexer ---------------- */

typedef struct {
    const char *source;     /* full source text (not owned) */
    const char *current;    /* current position */
    const char *file;       /* filename for error reporting */
    int line;
    int column;
    int paren_depth;        /* ( [ { depth — suppresses newline insertion */
    bool insert_newline;    /* whether next newline should become a token */
    AglCtx *ctx;            /* error context (not owned) */
} AglLexer;

/* Initialize a lexer for the given source text */
void agl_lexer_init(AglLexer *lexer, const char *source, const char *file,
                    AglCtx *ctx);

/* Get the next token. Returns AGL_TOKEN_EOF at end. */
AglToken agl_lexer_next_token(AglLexer *lexer);

/* Get a human-readable name for a token kind */
const char *agl_token_kind_name(AglTokenKind kind);
