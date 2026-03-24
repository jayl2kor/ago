#include "test_harness.h"
#include "../src/lexer.h"

/* Helper: lex all tokens from source into a static buffer */
#define MAX_TOKENS 128
static AglToken tokens[MAX_TOKENS];
static int token_count;

static void lex_all(const char *source, AglCtx *ctx) {
    AglLexer lexer;
    agl_lexer_init(&lexer, source, "test.ago", ctx);
    token_count = 0;
    for (;;) {
        tokens[token_count] = agl_lexer_next_token(&lexer);
        if (tokens[token_count].kind == AGL_TOKEN_EOF) {
            token_count++;
            break;
        }
        if (tokens[token_count].kind == AGL_TOKEN_ERROR) {
            token_count++;
            break;
        }
        token_count++;
        if (token_count >= MAX_TOKENS - 1) break;
    }
}

/* ---- Basic token tests ---- */

AGL_TEST(test_empty_input) {
    AglCtx *c = agl_ctx_new();
    lex_all("", c);
    AGL_ASSERT_INT_EQ(ctx, token_count, 1);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_EOF);
    agl_ctx_free(c);
}

AGL_TEST(test_whitespace_only) {
    AglCtx *c = agl_ctx_new();
    lex_all("   \t  \r  ", c);
    AGL_ASSERT_INT_EQ(ctx, token_count, 1);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_EOF);
    agl_ctx_free(c);
}

AGL_TEST(test_integer_literal) {
    AglCtx *c = agl_ctx_new();
    lex_all("42", c);
    AGL_ASSERT(ctx, token_count >= 2);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_INT);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].length, 2);
    AGL_ASSERT(ctx, memcmp(tokens[0].start, "42", 2) == 0);
    agl_ctx_free(c);
}

AGL_TEST(test_float_literal) {
    AglCtx *c = agl_ctx_new();
    lex_all("3.14", c);
    AGL_ASSERT(ctx, token_count >= 2);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_FLOAT);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].length, 4);
    agl_ctx_free(c);
}

AGL_TEST(test_string_literal) {
    AglCtx *c = agl_ctx_new();
    lex_all("\"hello world\"", c);
    AGL_ASSERT(ctx, token_count >= 2);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_STRING);
    /* includes quotes */
    AGL_ASSERT_INT_EQ(ctx, tokens[0].length, 13);
    agl_ctx_free(c);
}

AGL_TEST(test_string_with_escape) {
    AglCtx *c = agl_ctx_new();
    lex_all("\"hello\\nworld\"", c);
    AGL_ASSERT(ctx, token_count >= 2);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_STRING);
    agl_ctx_free(c);
}

AGL_TEST(test_unterminated_string) {
    AglCtx *c = agl_ctx_new();
    lex_all("\"hello", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_ERROR);
    AGL_ASSERT(ctx, agl_error_occurred(c));
    agl_ctx_free(c);
}

AGL_TEST(test_identifier) {
    AglCtx *c = agl_ctx_new();
    lex_all("foo_bar", c);
    AGL_ASSERT(ctx, token_count >= 2);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_IDENT);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].length, 7);
    agl_ctx_free(c);
}

/* ---- Keywords ---- */

AGL_TEST(test_keywords) {
    AglCtx *c = agl_ctx_new();

    struct { const char *src; AglTokenKind expected; } cases[] = {
        {"let",      AGL_TOKEN_LET},
        {"var",      AGL_TOKEN_VAR},
        {"fn",       AGL_TOKEN_FN},
        {"return",   AGL_TOKEN_RETURN},
        {"if",       AGL_TOKEN_IF},
        {"else",     AGL_TOKEN_ELSE},
        {"while",    AGL_TOKEN_WHILE},
        {"for",      AGL_TOKEN_FOR},
        {"in",       AGL_TOKEN_IN},
        {"struct",   AGL_TOKEN_STRUCT},
        {"match",    AGL_TOKEN_MATCH},
        {"ok",       AGL_TOKEN_OK},
        {"err",      AGL_TOKEN_ERR},
        {"true",     AGL_TOKEN_TRUE},
        {"false",    AGL_TOKEN_FALSE},
        {"break",    AGL_TOKEN_BREAK},
        {"continue", AGL_TOKEN_CONTINUE},
    };
    int n = (int)(sizeof(cases) / sizeof(cases[0]));

    for (int i = 0; i < n; i++) {
        lex_all(cases[i].src, c);
        AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, cases[i].expected);
        agl_error_clear(c);
    }

    /* "letter" should NOT be a keyword */
    lex_all("letter", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_IDENT);

    agl_ctx_free(c);
}

/* ---- Operators ---- */

AGL_TEST(test_single_char_operators) {
    AglCtx *c = agl_ctx_new();
    lex_all("+ - * / % . ! = < >", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_PLUS);
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_MINUS);
    AGL_ASSERT_INT_EQ(ctx, tokens[2].kind, AGL_TOKEN_STAR);
    AGL_ASSERT_INT_EQ(ctx, tokens[3].kind, AGL_TOKEN_SLASH);
    AGL_ASSERT_INT_EQ(ctx, tokens[4].kind, AGL_TOKEN_PERCENT);
    AGL_ASSERT_INT_EQ(ctx, tokens[5].kind, AGL_TOKEN_DOT);
    AGL_ASSERT_INT_EQ(ctx, tokens[6].kind, AGL_TOKEN_NOT);
    AGL_ASSERT_INT_EQ(ctx, tokens[7].kind, AGL_TOKEN_ASSIGN);
    AGL_ASSERT_INT_EQ(ctx, tokens[8].kind, AGL_TOKEN_LT);
    AGL_ASSERT_INT_EQ(ctx, tokens[9].kind, AGL_TOKEN_GT);
    agl_ctx_free(c);
}

AGL_TEST(test_two_char_operators) {
    AglCtx *c = agl_ctx_new();
    lex_all("== != <= >= && || ->", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_EQ);
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_NEQ);
    AGL_ASSERT_INT_EQ(ctx, tokens[2].kind, AGL_TOKEN_LE);
    AGL_ASSERT_INT_EQ(ctx, tokens[3].kind, AGL_TOKEN_GE);
    AGL_ASSERT_INT_EQ(ctx, tokens[4].kind, AGL_TOKEN_AND);
    AGL_ASSERT_INT_EQ(ctx, tokens[5].kind, AGL_TOKEN_OR);
    AGL_ASSERT_INT_EQ(ctx, tokens[6].kind, AGL_TOKEN_ARROW);
    agl_ctx_free(c);
}

/* ---- Delimiters ---- */

AGL_TEST(test_delimiters) {
    AglCtx *c = agl_ctx_new();
    lex_all("( ) { } [ ] , :", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_LPAREN);
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_RPAREN);
    AGL_ASSERT_INT_EQ(ctx, tokens[2].kind, AGL_TOKEN_LBRACE);
    AGL_ASSERT_INT_EQ(ctx, tokens[3].kind, AGL_TOKEN_RBRACE);
    AGL_ASSERT_INT_EQ(ctx, tokens[4].kind, AGL_TOKEN_LBRACKET);
    AGL_ASSERT_INT_EQ(ctx, tokens[5].kind, AGL_TOKEN_RBRACKET);
    AGL_ASSERT_INT_EQ(ctx, tokens[6].kind, AGL_TOKEN_COMMA);
    AGL_ASSERT_INT_EQ(ctx, tokens[7].kind, AGL_TOKEN_COLON);
    agl_ctx_free(c);
}

/* ---- Line/column tracking ---- */

AGL_TEST(test_source_location) {
    AglCtx *c = agl_ctx_new();
    lex_all("let x", c);
    /* "let" is at line 1, col 1 */
    AGL_ASSERT_INT_EQ(ctx, tokens[0].line, 1);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].column, 1);
    /* "x" is at line 1, col 5 */
    AGL_ASSERT_INT_EQ(ctx, tokens[1].line, 1);
    AGL_ASSERT_INT_EQ(ctx, tokens[1].column, 5);
    agl_ctx_free(c);
}

AGL_TEST(test_multiline_location) {
    AglCtx *c = agl_ctx_new();
    /* "let" does NOT trigger newline insertion (not in the insert set).
     * So "let\nx" produces: LET, IDENT(x), NEWLINE, EOF */
    lex_all("let\nx", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_LET);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].line, 1);
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_IDENT);
    AGL_ASSERT_INT_EQ(ctx, tokens[1].line, 2);
    agl_ctx_free(c);

    /* "x\ny" - identifier DOES trigger newline insertion */
    c = agl_ctx_new();
    lex_all("x\ny", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_IDENT);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].line, 1);
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_NEWLINE);
    AGL_ASSERT_INT_EQ(ctx, tokens[2].kind, AGL_TOKEN_IDENT);
    AGL_ASSERT_INT_EQ(ctx, tokens[2].line, 2);
    agl_ctx_free(c);
}

/* ---- Comments ---- */

AGL_TEST(test_line_comment) {
    AglCtx *c = agl_ctx_new();
    lex_all("42 // this is a comment", c);
    /* Should get: INT(42), NEWLINE, EOF */
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_INT);
    /* Last token is EOF (newline + EOF or just EOF with newline) */
    AGL_ASSERT_INT_EQ(ctx, tokens[token_count - 1].kind, AGL_TOKEN_EOF);
    agl_ctx_free(c);
}

AGL_TEST(test_comment_entire_line) {
    AglCtx *c = agl_ctx_new();
    lex_all("// just a comment\n42", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_INT);
    agl_ctx_free(c);
}

/* ---- Auto-semicolon (newline insertion) ---- */

AGL_TEST(test_newline_after_ident) {
    AglCtx *c = agl_ctx_new();
    lex_all("x\ny", c);
    /* x NEWLINE y NEWLINE EOF */
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_IDENT);
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_NEWLINE);
    AGL_ASSERT_INT_EQ(ctx, tokens[2].kind, AGL_TOKEN_IDENT);
    agl_ctx_free(c);
}

AGL_TEST(test_newline_after_rparen) {
    AglCtx *c = agl_ctx_new();
    lex_all("foo()\nbar()", c);
    /* foo ( ) NEWLINE bar ( ) NEWLINE EOF */
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_IDENT);
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_LPAREN);
    AGL_ASSERT_INT_EQ(ctx, tokens[2].kind, AGL_TOKEN_RPAREN);
    AGL_ASSERT_INT_EQ(ctx, tokens[3].kind, AGL_TOKEN_NEWLINE);
    AGL_ASSERT_INT_EQ(ctx, tokens[4].kind, AGL_TOKEN_IDENT);
    agl_ctx_free(c);
}

AGL_TEST(test_no_newline_after_operator) {
    AglCtx *c = agl_ctx_new();
    lex_all("x +\ny", c);
    /* x NEWLINE + y NEWLINE EOF — wait, x gets newline? No! + is next token.
     * Actually: x should insert newline, but + follows on next line.
     * Let me think: "x" -> insert_newline=true, then "\ny" -> newline seen,
     * but there's a "+" first... Actually the source is "x +\ny"
     * x -> insert_newline=true, " " -> skip, "+" -> saw_newline=false -> no newline inserted.
     * + -> insert_newline=false, "\n" -> saw_newline=true but insert_newline=false -> no newline.
     * y -> IDENT. So: x + y NEWLINE EOF. But x should get newline? No:
     * After x, skip whitespace " ", no newline seen, so "+" is next. Good.
     *
     * But wait: "x +\ny" — after +, we hit \n, insert_newline for + is false, so no NEWLINE.
     * Result: IDENT(x) NEWLINE(!) PLUS IDENT(y) NEWLINE EOF
     * Because after x, insert_newline=true. Then we skip " ", no newline. Then +.
     * Hmm, the newline is between + and y, not between x and +.
     * x (insert_newline=true), then " +" (no newline in between), + (insert_newline=false),
     * then "\ny" — saw_newline=true but insert_newline=false — no NEWLINE token.
     * Result: IDENT(x) PLUS IDENT(y) NEWLINE EOF — that's a continuation!
     *
     * Wait, x is on same line as +. The \n is between + and y.
     * So result should be: IDENT(x) PLUS IDENT(y) NEWLINE EOF
     */
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_IDENT);  /* x */
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_PLUS);   /* + */
    AGL_ASSERT_INT_EQ(ctx, tokens[2].kind, AGL_TOKEN_IDENT);  /* y */
    agl_ctx_free(c);
}

AGL_TEST(test_no_newline_inside_parens) {
    AglCtx *c = agl_ctx_new();
    lex_all("foo(\nx\n)", c);
    /* Inside parens, newlines are suppressed */
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_IDENT);    /* foo */
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_LPAREN);   /* ( */
    AGL_ASSERT_INT_EQ(ctx, tokens[2].kind, AGL_TOKEN_IDENT);    /* x */
    AGL_ASSERT_INT_EQ(ctx, tokens[3].kind, AGL_TOKEN_RPAREN);   /* ) */
    /* No NEWLINE tokens between ( and ) */
    agl_ctx_free(c);
}

AGL_TEST(test_newline_after_return) {
    AglCtx *c = agl_ctx_new();
    lex_all("return\nx", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_RETURN);
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_NEWLINE);
    AGL_ASSERT_INT_EQ(ctx, tokens[2].kind, AGL_TOKEN_IDENT);
    agl_ctx_free(c);
}

AGL_TEST(test_newline_before_eof) {
    AglCtx *c = agl_ctx_new();
    lex_all("42", c);
    /* 42 NEWLINE EOF */
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_INT);
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_NEWLINE);
    AGL_ASSERT_INT_EQ(ctx, tokens[2].kind, AGL_TOKEN_EOF);
    agl_ctx_free(c);
}

/* ---- Hello world tokens ---- */

AGL_TEST(test_hello_world) {
    AglCtx *c = agl_ctx_new();
    lex_all("print(\"hello world\")", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_IDENT);    /* print */
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_LPAREN);   /* ( */
    AGL_ASSERT_INT_EQ(ctx, tokens[2].kind, AGL_TOKEN_STRING);   /* "hello world" */
    AGL_ASSERT_INT_EQ(ctx, tokens[3].kind, AGL_TOKEN_RPAREN);   /* ) */
    AGL_ASSERT_INT_EQ(ctx, tokens[4].kind, AGL_TOKEN_NEWLINE);  /* auto */
    AGL_ASSERT_INT_EQ(ctx, tokens[5].kind, AGL_TOKEN_EOF);
    agl_ctx_free(c);
}

/* ---- Complex program ---- */

AGL_TEST(test_let_statement) {
    AglCtx *c = agl_ctx_new();
    lex_all("let x = 42", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_LET);
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_IDENT);
    AGL_ASSERT_INT_EQ(ctx, tokens[2].kind, AGL_TOKEN_ASSIGN);
    AGL_ASSERT_INT_EQ(ctx, tokens[3].kind, AGL_TOKEN_INT);
    agl_ctx_free(c);
}

AGL_TEST(test_function_def) {
    AglCtx *c = agl_ctx_new();
    lex_all("fn add(a: int, b: int) -> int {\n    return a + b\n}", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_FN);       /* fn */
    AGL_ASSERT_INT_EQ(ctx, tokens[1].kind, AGL_TOKEN_IDENT);    /* add */
    AGL_ASSERT_INT_EQ(ctx, tokens[2].kind, AGL_TOKEN_LPAREN);   /* ( */
    AGL_ASSERT_INT_EQ(ctx, tokens[3].kind, AGL_TOKEN_IDENT);    /* a */
    AGL_ASSERT_INT_EQ(ctx, tokens[4].kind, AGL_TOKEN_COLON);    /* : */
    AGL_ASSERT_INT_EQ(ctx, tokens[5].kind, AGL_TOKEN_IDENT);    /* int */
    AGL_ASSERT_INT_EQ(ctx, tokens[6].kind, AGL_TOKEN_COMMA);    /* , */
    AGL_ASSERT_INT_EQ(ctx, tokens[7].kind, AGL_TOKEN_IDENT);    /* b */
    AGL_ASSERT_INT_EQ(ctx, tokens[8].kind, AGL_TOKEN_COLON);    /* : */
    AGL_ASSERT_INT_EQ(ctx, tokens[9].kind, AGL_TOKEN_IDENT);    /* int */
    AGL_ASSERT_INT_EQ(ctx, tokens[10].kind, AGL_TOKEN_RPAREN);  /* ) */
    AGL_ASSERT_INT_EQ(ctx, tokens[11].kind, AGL_TOKEN_ARROW);   /* -> */
    AGL_ASSERT_INT_EQ(ctx, tokens[12].kind, AGL_TOKEN_IDENT);   /* int */
    AGL_ASSERT_INT_EQ(ctx, tokens[13].kind, AGL_TOKEN_LBRACE);  /* { */
    AGL_ASSERT_INT_EQ(ctx, tokens[14].kind, AGL_TOKEN_RETURN);  /* return */
    AGL_ASSERT_INT_EQ(ctx, tokens[15].kind, AGL_TOKEN_IDENT);   /* a */
    AGL_ASSERT_INT_EQ(ctx, tokens[16].kind, AGL_TOKEN_PLUS);    /* + */
    AGL_ASSERT_INT_EQ(ctx, tokens[17].kind, AGL_TOKEN_IDENT);   /* b */
    AGL_ASSERT_INT_EQ(ctx, tokens[18].kind, AGL_TOKEN_NEWLINE); /* auto after b */
    AGL_ASSERT_INT_EQ(ctx, tokens[19].kind, AGL_TOKEN_RBRACE);  /* } */
    AGL_ASSERT_INT_EQ(ctx, tokens[20].kind, AGL_TOKEN_NEWLINE); /* auto after } */
    AGL_ASSERT_INT_EQ(ctx, tokens[21].kind, AGL_TOKEN_EOF);
    agl_ctx_free(c);
}

/* ---- Error cases ---- */

AGL_TEST(test_unexpected_character) {
    AglCtx *c = agl_ctx_new();
    lex_all("@", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_ERROR);
    AGL_ASSERT(ctx, agl_error_occurred(c));
    agl_ctx_free(c);
}

AGL_TEST(test_single_ampersand) {
    AglCtx *c = agl_ctx_new();
    lex_all("& ", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_ERROR);
    AGL_ASSERT(ctx, agl_error_occurred(c));
    agl_ctx_free(c);
}

AGL_TEST(test_single_pipe) {
    AglCtx *c = agl_ctx_new();
    lex_all("| ", c);
    AGL_ASSERT_INT_EQ(ctx, tokens[0].kind, AGL_TOKEN_ERROR);
    AGL_ASSERT(ctx, agl_error_occurred(c));
    agl_ctx_free(c);
}

/* ---- Main ---- */

int main(void) {
    AglTestCtx ctx = {0, 0};

    printf("=== Lexer Tests ===\n");

    /* Basic tokens */
    AGL_RUN_TEST(&ctx, test_empty_input);
    AGL_RUN_TEST(&ctx, test_whitespace_only);
    AGL_RUN_TEST(&ctx, test_integer_literal);
    AGL_RUN_TEST(&ctx, test_float_literal);
    AGL_RUN_TEST(&ctx, test_string_literal);
    AGL_RUN_TEST(&ctx, test_string_with_escape);
    AGL_RUN_TEST(&ctx, test_unterminated_string);
    AGL_RUN_TEST(&ctx, test_identifier);

    /* Keywords */
    AGL_RUN_TEST(&ctx, test_keywords);

    /* Operators */
    AGL_RUN_TEST(&ctx, test_single_char_operators);
    AGL_RUN_TEST(&ctx, test_two_char_operators);

    /* Delimiters */
    AGL_RUN_TEST(&ctx, test_delimiters);

    /* Source location */
    AGL_RUN_TEST(&ctx, test_source_location);
    AGL_RUN_TEST(&ctx, test_multiline_location);

    /* Comments */
    AGL_RUN_TEST(&ctx, test_line_comment);
    AGL_RUN_TEST(&ctx, test_comment_entire_line);

    /* Auto-semicolon */
    AGL_RUN_TEST(&ctx, test_newline_after_ident);
    AGL_RUN_TEST(&ctx, test_newline_after_rparen);
    AGL_RUN_TEST(&ctx, test_no_newline_after_operator);
    AGL_RUN_TEST(&ctx, test_no_newline_inside_parens);
    AGL_RUN_TEST(&ctx, test_newline_after_return);
    AGL_RUN_TEST(&ctx, test_newline_before_eof);

    /* Integration */
    AGL_RUN_TEST(&ctx, test_hello_world);
    AGL_RUN_TEST(&ctx, test_let_statement);
    AGL_RUN_TEST(&ctx, test_function_def);

    /* Error cases */
    AGL_RUN_TEST(&ctx, test_unexpected_character);
    AGL_RUN_TEST(&ctx, test_single_ampersand);
    AGL_RUN_TEST(&ctx, test_single_pipe);

    AGL_SUMMARY(&ctx);
}
