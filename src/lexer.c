#include "lexer.h"

/* ---- Helpers ---- */

static bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static bool is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

static char peek(const AgoLexer *l) {
    return *l->current;
}

static char peek_next(const AgoLexer *l) {
    if (*l->current == '\0') return '\0';
    return l->current[1];
}

static char advance(AgoLexer *l) {
    char c = *l->current++;
    l->column++;
    return c;
}

static bool match(AgoLexer *l, char expected) {
    if (*l->current != expected) return false;
    l->current++;
    l->column++;
    return true;
}

static AgoToken make_token(AgoTokenKind kind,
                           const char *start, int length, int line, int col) {
    AgoToken t;
    t.kind = kind;
    t.start = start;
    t.length = length;
    t.line = line;
    t.column = col;
    return t;
}

static AgoToken error_token(AgoLexer *l, const char *message,
                            const char *start, int line, int col) {
    ago_error_set(l->ctx, AGO_ERR_SYNTAX, ago_loc(l->file, line, col),
                  "%s", message);
    return make_token(AGO_TOKEN_ERROR, start, (int)(l->current - start),
                      line, col);
}

/* ---- Go-style auto-semicolon insertion ----
 * A newline becomes a statement terminator if the preceding token is:
 *   - identifier, literal (int, float, string, true, false)
 *   - break, continue, return
 *   - ) ] }
 */
static bool should_insert_newline(AgoTokenKind kind) {
    switch (kind) {
    case AGO_TOKEN_IDENT:
    case AGO_TOKEN_INT:
    case AGO_TOKEN_FLOAT:
    case AGO_TOKEN_STRING:
    case AGO_TOKEN_TRUE:
    case AGO_TOKEN_FALSE:
    case AGO_TOKEN_BREAK:
    case AGO_TOKEN_CONTINUE:
    case AGO_TOKEN_RETURN:
    case AGO_TOKEN_RPAREN:
    case AGO_TOKEN_RBRACKET:
    case AGO_TOKEN_RBRACE:
        return true;
    default:
        return false;
    }
}

/* ---- Skip whitespace and comments ---- */

static bool skip_whitespace_and_comments(AgoLexer *l, bool *saw_newline) {
    *saw_newline = false;
    for (;;) {
        char c = peek(l);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(l);
        } else if (c == '\n') {
            *saw_newline = true;
            l->current++;
            l->line++;
            l->column = 1;
        } else if (c == '/' && peek_next(l) == '/') {
            /* Line comment — skip until end of line */
            while (peek(l) != '\n' && peek(l) != '\0') {
                advance(l);
            }
        } else {
            break;
        }
    }
    return *saw_newline;
}

/* ---- Scan a number literal ---- */

static AgoToken scan_number(AgoLexer *l, const char *start, int line, int col) {
    AgoTokenKind kind = AGO_TOKEN_INT;

    while (is_digit(peek(l))) advance(l);

    /* Check for fractional part */
    if (peek(l) == '.' && is_digit(peek_next(l))) {
        kind = AGO_TOKEN_FLOAT;
        advance(l); /* consume '.' */
        while (is_digit(peek(l))) advance(l);
    }

    return make_token(kind, start, (int)(l->current - start), line, col);
}

/* ---- Scan a string literal ---- */

static AgoToken scan_string(AgoLexer *l, const char *start, int line, int col) {
    while (peek(l) != '"' && peek(l) != '\0') {
        if (peek(l) == '\n') {
            return error_token(l, "unterminated string literal", start, line, col);
        }
        if (peek(l) == '\\') {
            advance(l); /* skip backslash */
            if (peek(l) == '\0') break;
        }
        advance(l);
    }

    if (peek(l) == '\0') {
        return error_token(l, "unterminated string literal", start, line, col);
    }

    advance(l); /* closing quote */
    return make_token(AGO_TOKEN_STRING, start, (int)(l->current - start),
                      line, col);
}

/* ---- Keyword lookup ---- */

typedef struct {
    const char *name;
    int length;
    AgoTokenKind kind;
} AgoKeyword;

static const AgoKeyword keywords[] = {
    {"break",    5, AGO_TOKEN_BREAK},
    {"continue", 8, AGO_TOKEN_CONTINUE},
    {"else",     4, AGO_TOKEN_ELSE},
    {"err",      3, AGO_TOKEN_ERR},
    {"false",    5, AGO_TOKEN_FALSE},
    {"fn",       2, AGO_TOKEN_FN},
    {"for",      3, AGO_TOKEN_FOR},
    {"if",       2, AGO_TOKEN_IF},
    {"in",       2, AGO_TOKEN_IN},
    {"let",      3, AGO_TOKEN_LET},
    {"match",    5, AGO_TOKEN_MATCH},
    {"ok",       2, AGO_TOKEN_OK},
    {"return",   6, AGO_TOKEN_RETURN},
    {"struct",   6, AGO_TOKEN_STRUCT},
    {"true",     4, AGO_TOKEN_TRUE},
    {"var",      3, AGO_TOKEN_VAR},
    {"while",    5, AGO_TOKEN_WHILE},
};

#define KEYWORD_COUNT (sizeof(keywords) / sizeof(keywords[0]))

static AgoTokenKind lookup_keyword(const char *start, int length) {
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (keywords[i].length == length &&
            memcmp(keywords[i].name, start, (size_t)length) == 0) {
            return keywords[i].kind;
        }
    }
    return AGO_TOKEN_IDENT;
}

/* ---- Scan an identifier or keyword ---- */

static AgoToken scan_identifier(AgoLexer *l, const char *start, int line, int col) {
    while (is_alnum(peek(l))) advance(l);

    int length = (int)(l->current - start);
    AgoTokenKind kind = lookup_keyword(start, length);
    return make_token(kind, start, length, line, col);
}

/* ---- Public API ---- */

void ago_lexer_init(AgoLexer *lexer, const char *source, const char *file,
                    AgoCtx *ctx) {
    lexer->source = source;
    lexer->current = source;
    lexer->file = file;
    lexer->line = 1;
    lexer->column = 1;
    lexer->paren_depth = 0;
    lexer->insert_newline = false;
    lexer->ctx = ctx;
}

AgoToken ago_lexer_next_token(AgoLexer *lexer) {
    bool saw_newline = false;
    skip_whitespace_and_comments(lexer, &saw_newline);

    /* Auto-semicolon: insert newline token if needed */
    if (saw_newline && lexer->insert_newline && lexer->paren_depth == 0) {
        lexer->insert_newline = false;
        return make_token(AGO_TOKEN_NEWLINE, "\n", 1,
                          lexer->line - 1, 0);
    }

    if (peek(lexer) == '\0') {
        /* Insert final newline if needed before EOF */
        if (lexer->insert_newline) {
            lexer->insert_newline = false;
            return make_token(AGO_TOKEN_NEWLINE, "", 0,
                              lexer->line, lexer->column);
        }
        return make_token(AGO_TOKEN_EOF, lexer->current, 0,
                          lexer->line, lexer->column);
    }

    const char *start = lexer->current;
    int line = lexer->line;
    int col = lexer->column;
    char c = advance(lexer);

    AgoToken tok;

    /* Identifiers and keywords */
    if (is_alpha(c)) {
        tok = scan_identifier(lexer, start, line, col);
        lexer->insert_newline = should_insert_newline(tok.kind);
        return tok;
    }

    /* Number literals */
    if (is_digit(c)) {
        tok = scan_number(lexer, start, line, col);
        lexer->insert_newline = should_insert_newline(tok.kind);
        return tok;
    }

    /* String literals */
    if (c == '"') {
        tok = scan_string(lexer, start, line, col);
        lexer->insert_newline = should_insert_newline(tok.kind);
        return tok;
    }

    /* Operators and delimiters */
    AgoTokenKind kind;
    switch (c) {
    case '(': lexer->paren_depth++; kind = AGO_TOKEN_LPAREN;   break;
    case ')': lexer->paren_depth--; kind = AGO_TOKEN_RPAREN;   break;
    case '{': kind = AGO_TOKEN_LBRACE;   break;
    case '}': kind = AGO_TOKEN_RBRACE;   break;
    case '[': lexer->paren_depth++; kind = AGO_TOKEN_LBRACKET; break;
    case ']': lexer->paren_depth--; kind = AGO_TOKEN_RBRACKET; break;
    case ',': kind = AGO_TOKEN_COMMA;   break;
    case ':': kind = AGO_TOKEN_COLON;   break;
    case '.': kind = AGO_TOKEN_DOT;     break;
    case '+': kind = AGO_TOKEN_PLUS;    break;
    case '*': kind = AGO_TOKEN_STAR;    break;
    case '/': kind = AGO_TOKEN_SLASH;   break;
    case '%': kind = AGO_TOKEN_PERCENT; break;
    case '-':
        kind = match(lexer, '>') ? AGO_TOKEN_ARROW : AGO_TOKEN_MINUS;
        break;
    case '=':
        kind = match(lexer, '=') ? AGO_TOKEN_EQ : AGO_TOKEN_ASSIGN;
        break;
    case '!':
        kind = match(lexer, '=') ? AGO_TOKEN_NEQ : AGO_TOKEN_NOT;
        break;
    case '<':
        kind = match(lexer, '=') ? AGO_TOKEN_LE : AGO_TOKEN_LT;
        break;
    case '>':
        kind = match(lexer, '=') ? AGO_TOKEN_GE : AGO_TOKEN_GT;
        break;
    case '&':
        if (match(lexer, '&')) { kind = AGO_TOKEN_AND; }
        else {
            tok = error_token(lexer, "unexpected character '&', did you mean '&&'?",
                              start, line, col);
            lexer->insert_newline = false;
            return tok;
        }
        break;
    case '|':
        if (match(lexer, '|')) { kind = AGO_TOKEN_OR; }
        else {
            tok = error_token(lexer, "unexpected character '|', did you mean '||'?",
                              start, line, col);
            lexer->insert_newline = false;
            return tok;
        }
        break;
    default:
        tok = error_token(lexer, "unexpected character", start, line, col);
        lexer->insert_newline = false;
        return tok;
    }

    tok = make_token(kind, start, (int)(lexer->current - start), line, col);
    lexer->insert_newline = should_insert_newline(kind);
    return tok;
}

const char *ago_token_kind_name(AgoTokenKind kind) {
    switch (kind) {
    case AGO_TOKEN_INT:       return "INT";
    case AGO_TOKEN_FLOAT:     return "FLOAT";
    case AGO_TOKEN_STRING:    return "STRING";
    case AGO_TOKEN_TRUE:      return "true";
    case AGO_TOKEN_FALSE:     return "false";
    case AGO_TOKEN_IDENT:     return "IDENT";
    case AGO_TOKEN_LET:       return "let";
    case AGO_TOKEN_VAR:       return "var";
    case AGO_TOKEN_FN:        return "fn";
    case AGO_TOKEN_RETURN:    return "return";
    case AGO_TOKEN_IF:        return "if";
    case AGO_TOKEN_ELSE:      return "else";
    case AGO_TOKEN_WHILE:     return "while";
    case AGO_TOKEN_FOR:       return "for";
    case AGO_TOKEN_IN:        return "in";
    case AGO_TOKEN_STRUCT:    return "struct";
    case AGO_TOKEN_MATCH:     return "match";
    case AGO_TOKEN_OK:        return "ok";
    case AGO_TOKEN_ERR:       return "err";
    case AGO_TOKEN_BREAK:     return "break";
    case AGO_TOKEN_CONTINUE:  return "continue";
    case AGO_TOKEN_PLUS:      return "+";
    case AGO_TOKEN_MINUS:     return "-";
    case AGO_TOKEN_STAR:      return "*";
    case AGO_TOKEN_SLASH:     return "/";
    case AGO_TOKEN_PERCENT:   return "%";
    case AGO_TOKEN_ASSIGN:    return "=";
    case AGO_TOKEN_EQ:        return "==";
    case AGO_TOKEN_NEQ:       return "!=";
    case AGO_TOKEN_LT:        return "<";
    case AGO_TOKEN_GT:        return ">";
    case AGO_TOKEN_LE:        return "<=";
    case AGO_TOKEN_GE:        return ">=";
    case AGO_TOKEN_AND:       return "&&";
    case AGO_TOKEN_OR:        return "||";
    case AGO_TOKEN_NOT:       return "!";
    case AGO_TOKEN_ARROW:     return "->";
    case AGO_TOKEN_DOT:       return ".";
    case AGO_TOKEN_LPAREN:    return "(";
    case AGO_TOKEN_RPAREN:    return ")";
    case AGO_TOKEN_LBRACE:    return "{";
    case AGO_TOKEN_RBRACE:    return "}";
    case AGO_TOKEN_LBRACKET:  return "[";
    case AGO_TOKEN_RBRACKET:  return "]";
    case AGO_TOKEN_COMMA:     return ",";
    case AGO_TOKEN_COLON:     return ":";
    case AGO_TOKEN_NEWLINE:   return "NEWLINE";
    case AGO_TOKEN_EOF:       return "EOF";
    case AGO_TOKEN_ERROR:     return "ERROR";
    }
    return "UNKNOWN";
}
