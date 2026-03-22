#include "test_harness.h"
#include "../src/lexer.h"

/* Helper: lex all tokens from source into a static buffer */
#define MAX_TOKENS 128
static AgoToken tokens[MAX_TOKENS];
static int token_count;

static void lex_all(const char *source, AgoCtx *ctx) {
    AgoLexer lexer;
    ago_lexer_init(&lexer, source, "test.ago", ctx);
    token_count = 0;
    for (;;) {
        tokens[token_count] = ago_lexer_next_token(&lexer);
        if (tokens[token_count].kind == AGO_TOKEN_EOF) {
            token_count++;
            break;
        }
        if (tokens[token_count].kind == AGO_TOKEN_ERROR) {
            token_count++;
            break;
        }
        token_count++;
        if (token_count >= MAX_TOKENS - 1) break;
    }
}

/* ---- Basic token tests ---- */

AGO_TEST(test_empty_input) {
    AgoCtx *c = ago_ctx_new();
    lex_all("", c);
    AGO_ASSERT_INT_EQ(ctx, token_count, 1);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_EOF);
    ago_ctx_free(c);
}

AGO_TEST(test_whitespace_only) {
    AgoCtx *c = ago_ctx_new();
    lex_all("   \t  \r  ", c);
    AGO_ASSERT_INT_EQ(ctx, token_count, 1);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_EOF);
    ago_ctx_free(c);
}

AGO_TEST(test_integer_literal) {
    AgoCtx *c = ago_ctx_new();
    lex_all("42", c);
    AGO_ASSERT(ctx, token_count >= 2);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_INT);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].length, 2);
    AGO_ASSERT(ctx, memcmp(tokens[0].start, "42", 2) == 0);
    ago_ctx_free(c);
}

AGO_TEST(test_float_literal) {
    AgoCtx *c = ago_ctx_new();
    lex_all("3.14", c);
    AGO_ASSERT(ctx, token_count >= 2);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_FLOAT);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].length, 4);
    ago_ctx_free(c);
}

AGO_TEST(test_string_literal) {
    AgoCtx *c = ago_ctx_new();
    lex_all("\"hello world\"", c);
    AGO_ASSERT(ctx, token_count >= 2);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_STRING);
    /* includes quotes */
    AGO_ASSERT_INT_EQ(ctx, tokens[0].length, 13);
    ago_ctx_free(c);
}

AGO_TEST(test_string_with_escape) {
    AgoCtx *c = ago_ctx_new();
    lex_all("\"hello\\nworld\"", c);
    AGO_ASSERT(ctx, token_count >= 2);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_STRING);
    ago_ctx_free(c);
}

AGO_TEST(test_unterminated_string) {
    AgoCtx *c = ago_ctx_new();
    lex_all("\"hello", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_ERROR);
    AGO_ASSERT(ctx, ago_error_occurred(c));
    ago_ctx_free(c);
}

AGO_TEST(test_identifier) {
    AgoCtx *c = ago_ctx_new();
    lex_all("foo_bar", c);
    AGO_ASSERT(ctx, token_count >= 2);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_IDENT);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].length, 7);
    ago_ctx_free(c);
}

/* ---- Keywords ---- */

AGO_TEST(test_keywords) {
    AgoCtx *c = ago_ctx_new();

    struct { const char *src; AgoTokenKind expected; } cases[] = {
        {"let",      AGO_TOKEN_LET},
        {"var",      AGO_TOKEN_VAR},
        {"fn",       AGO_TOKEN_FN},
        {"return",   AGO_TOKEN_RETURN},
        {"if",       AGO_TOKEN_IF},
        {"else",     AGO_TOKEN_ELSE},
        {"while",    AGO_TOKEN_WHILE},
        {"for",      AGO_TOKEN_FOR},
        {"in",       AGO_TOKEN_IN},
        {"struct",   AGO_TOKEN_STRUCT},
        {"match",    AGO_TOKEN_MATCH},
        {"ok",       AGO_TOKEN_OK},
        {"err",      AGO_TOKEN_ERR},
        {"true",     AGO_TOKEN_TRUE},
        {"false",    AGO_TOKEN_FALSE},
        {"break",    AGO_TOKEN_BREAK},
        {"continue", AGO_TOKEN_CONTINUE},
    };
    int n = (int)(sizeof(cases) / sizeof(cases[0]));

    for (int i = 0; i < n; i++) {
        lex_all(cases[i].src, c);
        AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, cases[i].expected);
        ago_error_clear(c);
    }

    /* "letter" should NOT be a keyword */
    lex_all("letter", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_IDENT);

    ago_ctx_free(c);
}

/* ---- Operators ---- */

AGO_TEST(test_single_char_operators) {
    AgoCtx *c = ago_ctx_new();
    lex_all("+ - * / % . ! = < >", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_PLUS);
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_MINUS);
    AGO_ASSERT_INT_EQ(ctx, tokens[2].kind, AGO_TOKEN_STAR);
    AGO_ASSERT_INT_EQ(ctx, tokens[3].kind, AGO_TOKEN_SLASH);
    AGO_ASSERT_INT_EQ(ctx, tokens[4].kind, AGO_TOKEN_PERCENT);
    AGO_ASSERT_INT_EQ(ctx, tokens[5].kind, AGO_TOKEN_DOT);
    AGO_ASSERT_INT_EQ(ctx, tokens[6].kind, AGO_TOKEN_NOT);
    AGO_ASSERT_INT_EQ(ctx, tokens[7].kind, AGO_TOKEN_ASSIGN);
    AGO_ASSERT_INT_EQ(ctx, tokens[8].kind, AGO_TOKEN_LT);
    AGO_ASSERT_INT_EQ(ctx, tokens[9].kind, AGO_TOKEN_GT);
    ago_ctx_free(c);
}

AGO_TEST(test_two_char_operators) {
    AgoCtx *c = ago_ctx_new();
    lex_all("== != <= >= && || ->", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_EQ);
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_NEQ);
    AGO_ASSERT_INT_EQ(ctx, tokens[2].kind, AGO_TOKEN_LE);
    AGO_ASSERT_INT_EQ(ctx, tokens[3].kind, AGO_TOKEN_GE);
    AGO_ASSERT_INT_EQ(ctx, tokens[4].kind, AGO_TOKEN_AND);
    AGO_ASSERT_INT_EQ(ctx, tokens[5].kind, AGO_TOKEN_OR);
    AGO_ASSERT_INT_EQ(ctx, tokens[6].kind, AGO_TOKEN_ARROW);
    ago_ctx_free(c);
}

/* ---- Delimiters ---- */

AGO_TEST(test_delimiters) {
    AgoCtx *c = ago_ctx_new();
    lex_all("( ) { } [ ] , :", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_LPAREN);
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_RPAREN);
    AGO_ASSERT_INT_EQ(ctx, tokens[2].kind, AGO_TOKEN_LBRACE);
    AGO_ASSERT_INT_EQ(ctx, tokens[3].kind, AGO_TOKEN_RBRACE);
    AGO_ASSERT_INT_EQ(ctx, tokens[4].kind, AGO_TOKEN_LBRACKET);
    AGO_ASSERT_INT_EQ(ctx, tokens[5].kind, AGO_TOKEN_RBRACKET);
    AGO_ASSERT_INT_EQ(ctx, tokens[6].kind, AGO_TOKEN_COMMA);
    AGO_ASSERT_INT_EQ(ctx, tokens[7].kind, AGO_TOKEN_COLON);
    ago_ctx_free(c);
}

/* ---- Line/column tracking ---- */

AGO_TEST(test_source_location) {
    AgoCtx *c = ago_ctx_new();
    lex_all("let x", c);
    /* "let" is at line 1, col 1 */
    AGO_ASSERT_INT_EQ(ctx, tokens[0].line, 1);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].column, 1);
    /* "x" is at line 1, col 5 */
    AGO_ASSERT_INT_EQ(ctx, tokens[1].line, 1);
    AGO_ASSERT_INT_EQ(ctx, tokens[1].column, 5);
    ago_ctx_free(c);
}

AGO_TEST(test_multiline_location) {
    AgoCtx *c = ago_ctx_new();
    /* "let" does NOT trigger newline insertion (not in the insert set).
     * So "let\nx" produces: LET, IDENT(x), NEWLINE, EOF */
    lex_all("let\nx", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_LET);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].line, 1);
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_IDENT);
    AGO_ASSERT_INT_EQ(ctx, tokens[1].line, 2);
    ago_ctx_free(c);

    /* "x\ny" - identifier DOES trigger newline insertion */
    c = ago_ctx_new();
    lex_all("x\ny", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_IDENT);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].line, 1);
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_NEWLINE);
    AGO_ASSERT_INT_EQ(ctx, tokens[2].kind, AGO_TOKEN_IDENT);
    AGO_ASSERT_INT_EQ(ctx, tokens[2].line, 2);
    ago_ctx_free(c);
}

/* ---- Comments ---- */

AGO_TEST(test_line_comment) {
    AgoCtx *c = ago_ctx_new();
    lex_all("42 // this is a comment", c);
    /* Should get: INT(42), NEWLINE, EOF */
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_INT);
    /* Last token is EOF (newline + EOF or just EOF with newline) */
    AGO_ASSERT_INT_EQ(ctx, tokens[token_count - 1].kind, AGO_TOKEN_EOF);
    ago_ctx_free(c);
}

AGO_TEST(test_comment_entire_line) {
    AgoCtx *c = ago_ctx_new();
    lex_all("// just a comment\n42", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_INT);
    ago_ctx_free(c);
}

/* ---- Auto-semicolon (newline insertion) ---- */

AGO_TEST(test_newline_after_ident) {
    AgoCtx *c = ago_ctx_new();
    lex_all("x\ny", c);
    /* x NEWLINE y NEWLINE EOF */
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_IDENT);
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_NEWLINE);
    AGO_ASSERT_INT_EQ(ctx, tokens[2].kind, AGO_TOKEN_IDENT);
    ago_ctx_free(c);
}

AGO_TEST(test_newline_after_rparen) {
    AgoCtx *c = ago_ctx_new();
    lex_all("foo()\nbar()", c);
    /* foo ( ) NEWLINE bar ( ) NEWLINE EOF */
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_IDENT);
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_LPAREN);
    AGO_ASSERT_INT_EQ(ctx, tokens[2].kind, AGO_TOKEN_RPAREN);
    AGO_ASSERT_INT_EQ(ctx, tokens[3].kind, AGO_TOKEN_NEWLINE);
    AGO_ASSERT_INT_EQ(ctx, tokens[4].kind, AGO_TOKEN_IDENT);
    ago_ctx_free(c);
}

AGO_TEST(test_no_newline_after_operator) {
    AgoCtx *c = ago_ctx_new();
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
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_IDENT);  /* x */
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_PLUS);   /* + */
    AGO_ASSERT_INT_EQ(ctx, tokens[2].kind, AGO_TOKEN_IDENT);  /* y */
    ago_ctx_free(c);
}

AGO_TEST(test_no_newline_inside_parens) {
    AgoCtx *c = ago_ctx_new();
    lex_all("foo(\nx\n)", c);
    /* Inside parens, newlines are suppressed */
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_IDENT);    /* foo */
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_LPAREN);   /* ( */
    AGO_ASSERT_INT_EQ(ctx, tokens[2].kind, AGO_TOKEN_IDENT);    /* x */
    AGO_ASSERT_INT_EQ(ctx, tokens[3].kind, AGO_TOKEN_RPAREN);   /* ) */
    /* No NEWLINE tokens between ( and ) */
    ago_ctx_free(c);
}

AGO_TEST(test_newline_after_return) {
    AgoCtx *c = ago_ctx_new();
    lex_all("return\nx", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_RETURN);
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_NEWLINE);
    AGO_ASSERT_INT_EQ(ctx, tokens[2].kind, AGO_TOKEN_IDENT);
    ago_ctx_free(c);
}

AGO_TEST(test_newline_before_eof) {
    AgoCtx *c = ago_ctx_new();
    lex_all("42", c);
    /* 42 NEWLINE EOF */
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_INT);
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_NEWLINE);
    AGO_ASSERT_INT_EQ(ctx, tokens[2].kind, AGO_TOKEN_EOF);
    ago_ctx_free(c);
}

/* ---- Hello world tokens ---- */

AGO_TEST(test_hello_world) {
    AgoCtx *c = ago_ctx_new();
    lex_all("print(\"hello world\")", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_IDENT);    /* print */
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_LPAREN);   /* ( */
    AGO_ASSERT_INT_EQ(ctx, tokens[2].kind, AGO_TOKEN_STRING);   /* "hello world" */
    AGO_ASSERT_INT_EQ(ctx, tokens[3].kind, AGO_TOKEN_RPAREN);   /* ) */
    AGO_ASSERT_INT_EQ(ctx, tokens[4].kind, AGO_TOKEN_NEWLINE);  /* auto */
    AGO_ASSERT_INT_EQ(ctx, tokens[5].kind, AGO_TOKEN_EOF);
    ago_ctx_free(c);
}

/* ---- Complex program ---- */

AGO_TEST(test_let_statement) {
    AgoCtx *c = ago_ctx_new();
    lex_all("let x = 42", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_LET);
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_IDENT);
    AGO_ASSERT_INT_EQ(ctx, tokens[2].kind, AGO_TOKEN_ASSIGN);
    AGO_ASSERT_INT_EQ(ctx, tokens[3].kind, AGO_TOKEN_INT);
    ago_ctx_free(c);
}

AGO_TEST(test_function_def) {
    AgoCtx *c = ago_ctx_new();
    lex_all("fn add(a: int, b: int) -> int {\n    return a + b\n}", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_FN);       /* fn */
    AGO_ASSERT_INT_EQ(ctx, tokens[1].kind, AGO_TOKEN_IDENT);    /* add */
    AGO_ASSERT_INT_EQ(ctx, tokens[2].kind, AGO_TOKEN_LPAREN);   /* ( */
    AGO_ASSERT_INT_EQ(ctx, tokens[3].kind, AGO_TOKEN_IDENT);    /* a */
    AGO_ASSERT_INT_EQ(ctx, tokens[4].kind, AGO_TOKEN_COLON);    /* : */
    AGO_ASSERT_INT_EQ(ctx, tokens[5].kind, AGO_TOKEN_IDENT);    /* int */
    AGO_ASSERT_INT_EQ(ctx, tokens[6].kind, AGO_TOKEN_COMMA);    /* , */
    AGO_ASSERT_INT_EQ(ctx, tokens[7].kind, AGO_TOKEN_IDENT);    /* b */
    AGO_ASSERT_INT_EQ(ctx, tokens[8].kind, AGO_TOKEN_COLON);    /* : */
    AGO_ASSERT_INT_EQ(ctx, tokens[9].kind, AGO_TOKEN_IDENT);    /* int */
    AGO_ASSERT_INT_EQ(ctx, tokens[10].kind, AGO_TOKEN_RPAREN);  /* ) */
    AGO_ASSERT_INT_EQ(ctx, tokens[11].kind, AGO_TOKEN_ARROW);   /* -> */
    AGO_ASSERT_INT_EQ(ctx, tokens[12].kind, AGO_TOKEN_IDENT);   /* int */
    AGO_ASSERT_INT_EQ(ctx, tokens[13].kind, AGO_TOKEN_LBRACE);  /* { */
    AGO_ASSERT_INT_EQ(ctx, tokens[14].kind, AGO_TOKEN_RETURN);  /* return */
    AGO_ASSERT_INT_EQ(ctx, tokens[15].kind, AGO_TOKEN_IDENT);   /* a */
    AGO_ASSERT_INT_EQ(ctx, tokens[16].kind, AGO_TOKEN_PLUS);    /* + */
    AGO_ASSERT_INT_EQ(ctx, tokens[17].kind, AGO_TOKEN_IDENT);   /* b */
    AGO_ASSERT_INT_EQ(ctx, tokens[18].kind, AGO_TOKEN_NEWLINE); /* auto after b */
    AGO_ASSERT_INT_EQ(ctx, tokens[19].kind, AGO_TOKEN_RBRACE);  /* } */
    AGO_ASSERT_INT_EQ(ctx, tokens[20].kind, AGO_TOKEN_NEWLINE); /* auto after } */
    AGO_ASSERT_INT_EQ(ctx, tokens[21].kind, AGO_TOKEN_EOF);
    ago_ctx_free(c);
}

/* ---- Error cases ---- */

AGO_TEST(test_unexpected_character) {
    AgoCtx *c = ago_ctx_new();
    lex_all("@", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_ERROR);
    AGO_ASSERT(ctx, ago_error_occurred(c));
    ago_ctx_free(c);
}

AGO_TEST(test_single_ampersand) {
    AgoCtx *c = ago_ctx_new();
    lex_all("& ", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_ERROR);
    AGO_ASSERT(ctx, ago_error_occurred(c));
    ago_ctx_free(c);
}

AGO_TEST(test_single_pipe) {
    AgoCtx *c = ago_ctx_new();
    lex_all("| ", c);
    AGO_ASSERT_INT_EQ(ctx, tokens[0].kind, AGO_TOKEN_ERROR);
    AGO_ASSERT(ctx, ago_error_occurred(c));
    ago_ctx_free(c);
}

/* ---- Main ---- */

int main(void) {
    AgoTestCtx ctx = {0, 0};

    printf("=== Lexer Tests ===\n");

    /* Basic tokens */
    AGO_RUN_TEST(&ctx, test_empty_input);
    AGO_RUN_TEST(&ctx, test_whitespace_only);
    AGO_RUN_TEST(&ctx, test_integer_literal);
    AGO_RUN_TEST(&ctx, test_float_literal);
    AGO_RUN_TEST(&ctx, test_string_literal);
    AGO_RUN_TEST(&ctx, test_string_with_escape);
    AGO_RUN_TEST(&ctx, test_unterminated_string);
    AGO_RUN_TEST(&ctx, test_identifier);

    /* Keywords */
    AGO_RUN_TEST(&ctx, test_keywords);

    /* Operators */
    AGO_RUN_TEST(&ctx, test_single_char_operators);
    AGO_RUN_TEST(&ctx, test_two_char_operators);

    /* Delimiters */
    AGO_RUN_TEST(&ctx, test_delimiters);

    /* Source location */
    AGO_RUN_TEST(&ctx, test_source_location);
    AGO_RUN_TEST(&ctx, test_multiline_location);

    /* Comments */
    AGO_RUN_TEST(&ctx, test_line_comment);
    AGO_RUN_TEST(&ctx, test_comment_entire_line);

    /* Auto-semicolon */
    AGO_RUN_TEST(&ctx, test_newline_after_ident);
    AGO_RUN_TEST(&ctx, test_newline_after_rparen);
    AGO_RUN_TEST(&ctx, test_no_newline_after_operator);
    AGO_RUN_TEST(&ctx, test_no_newline_inside_parens);
    AGO_RUN_TEST(&ctx, test_newline_after_return);
    AGO_RUN_TEST(&ctx, test_newline_before_eof);

    /* Integration */
    AGO_RUN_TEST(&ctx, test_hello_world);
    AGO_RUN_TEST(&ctx, test_let_statement);
    AGO_RUN_TEST(&ctx, test_function_def);

    /* Error cases */
    AGO_RUN_TEST(&ctx, test_unexpected_character);
    AGO_RUN_TEST(&ctx, test_single_ampersand);
    AGO_RUN_TEST(&ctx, test_single_pipe);

    AGO_SUMMARY(&ctx);
}
