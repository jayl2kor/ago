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

static char peek(const AglLexer *l) {
    return *l->current;
}

static char peek_next(const AglLexer *l) {
    if (*l->current == '\0') return '\0';
    return l->current[1];
}

static char advance(AglLexer *l) {
    char c = *l->current++;
    l->column++;
    return c;
}

static bool match(AglLexer *l, char expected) {
    if (*l->current != expected) return false;
    l->current++;
    l->column++;
    return true;
}

static AglToken make_token(AglTokenKind kind,
                           const char *start, int length, int line, int col) {
    AglToken t;
    t.kind = kind;
    t.start = start;
    t.length = length;
    t.line = line;
    t.column = col;
    return t;
}

static AglToken error_token(AglLexer *l, const char *message,
                            const char *start, int line, int col) {
    agl_error_set(l->ctx, AGL_ERR_SYNTAX, agl_loc(l->file, line, col),
                  "%s", message);
    return make_token(AGL_TOKEN_ERROR, start, (int)(l->current - start),
                      line, col);
}

/* ---- Go-style auto-semicolon insertion ----
 * A newline becomes a statement terminator if the preceding token is:
 *   - identifier, literal (int, float, string, true, false)
 *   - break, continue, return
 *   - ) ] }
 */
static bool should_insert_newline(AglTokenKind kind) {
    switch (kind) {
    case AGL_TOKEN_IDENT:
    case AGL_TOKEN_INT:
    case AGL_TOKEN_FLOAT:
    case AGL_TOKEN_STRING:
    case AGL_TOKEN_TRUE:
    case AGL_TOKEN_FALSE:
    case AGL_TOKEN_BREAK:
    case AGL_TOKEN_CONTINUE:
    case AGL_TOKEN_RETURN:
    case AGL_TOKEN_RPAREN:
    case AGL_TOKEN_RBRACKET:
    case AGL_TOKEN_RBRACE:
    case AGL_TOKEN_QUESTION:
        return true;
    default:
        return false;
    }
}

/* ---- Skip whitespace and comments ---- */

static bool skip_whitespace_and_comments(AglLexer *l, bool *saw_newline) {
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

static AglToken scan_number(AglLexer *l, const char *start, int line, int col) {
    AglTokenKind kind = AGL_TOKEN_INT;

    while (is_digit(peek(l))) advance(l);

    /* Check for fractional part */
    if (peek(l) == '.' && is_digit(peek_next(l))) {
        kind = AGL_TOKEN_FLOAT;
        advance(l); /* consume '.' */
        while (is_digit(peek(l))) advance(l);
    }

    return make_token(kind, start, (int)(l->current - start), line, col);
}

/* ---- Scan a string literal ---- */

static AglToken scan_string(AglLexer *l, const char *start, int line, int col) {
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
    return make_token(AGL_TOKEN_STRING, start, (int)(l->current - start),
                      line, col);
}

/* ---- Keyword lookup ---- */

typedef struct {
    const char *name;
    int length;
    AglTokenKind kind;
} AglKeyword;

static const AglKeyword keywords[] = {
    {"break",    5, AGL_TOKEN_BREAK},
    {"continue", 8, AGL_TOKEN_CONTINUE},
    {"else",     4, AGL_TOKEN_ELSE},
    {"err",      3, AGL_TOKEN_ERR},
    {"false",    5, AGL_TOKEN_FALSE},
    {"fn",       2, AGL_TOKEN_FN},
    {"for",      3, AGL_TOKEN_FOR},
    {"if",       2, AGL_TOKEN_IF},
    {"import",   6, AGL_TOKEN_IMPORT},
    {"in",       2, AGL_TOKEN_IN},
    {"let",      3, AGL_TOKEN_LET},
    {"match",    5, AGL_TOKEN_MATCH},
    {"ok",       2, AGL_TOKEN_OK},
    {"return",   6, AGL_TOKEN_RETURN},
    {"struct",   6, AGL_TOKEN_STRUCT},
    {"true",     4, AGL_TOKEN_TRUE},
    {"var",      3, AGL_TOKEN_VAR},
    {"while",    5, AGL_TOKEN_WHILE},
};

#define KEYWORD_COUNT (sizeof(keywords) / sizeof(keywords[0]))

static AglTokenKind lookup_keyword(const char *start, int length) {
    for (size_t i = 0; i < KEYWORD_COUNT; i++) {
        if (agl_str_eq(keywords[i].name, keywords[i].length, start, length)) {
            return keywords[i].kind;
        }
    }
    return AGL_TOKEN_IDENT;
}

/* ---- Scan an identifier or keyword ---- */

static AglToken scan_identifier(AglLexer *l, const char *start, int line, int col) {
    while (is_alnum(peek(l))) advance(l);

    int length = (int)(l->current - start);
    AglTokenKind kind = lookup_keyword(start, length);
    return make_token(kind, start, length, line, col);
}

/* ---- Public API ---- */

void agl_lexer_init(AglLexer *lexer, const char *source, const char *file,
                    AglCtx *ctx) {
    lexer->source = source;
    lexer->current = source;
    lexer->file = file;
    lexer->line = 1;
    lexer->column = 1;
    lexer->paren_depth = 0;
    lexer->insert_newline = false;
    lexer->ctx = ctx;
}

AglToken agl_lexer_next_token(AglLexer *lexer) {
    bool saw_newline = false;
    skip_whitespace_and_comments(lexer, &saw_newline);

    /* Auto-semicolon: insert newline token if needed */
    if (saw_newline && lexer->insert_newline && lexer->paren_depth == 0) {
        lexer->insert_newline = false;
        return make_token(AGL_TOKEN_NEWLINE, "\n", 1,
                          lexer->line - 1, 0);
    }

    if (peek(lexer) == '\0') {
        /* Insert final newline if needed before EOF */
        if (lexer->insert_newline) {
            lexer->insert_newline = false;
            return make_token(AGL_TOKEN_NEWLINE, "", 0,
                              lexer->line, lexer->column);
        }
        return make_token(AGL_TOKEN_EOF, lexer->current, 0,
                          lexer->line, lexer->column);
    }

    const char *start = lexer->current;
    int line = lexer->line;
    int col = lexer->column;
    char c = advance(lexer);

    AglToken tok;

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
    AglTokenKind kind;
    switch (c) {
    case '(': lexer->paren_depth++; kind = AGL_TOKEN_LPAREN;   break;
    case ')': if (lexer->paren_depth > 0) lexer->paren_depth--; kind = AGL_TOKEN_RPAREN; break;
    case '{': kind = AGL_TOKEN_LBRACE;   break;
    case '}': kind = AGL_TOKEN_RBRACE;   break;
    case '[': lexer->paren_depth++; kind = AGL_TOKEN_LBRACKET; break;
    case ']': if (lexer->paren_depth > 0) lexer->paren_depth--; kind = AGL_TOKEN_RBRACKET; break;
    case ',': kind = AGL_TOKEN_COMMA;   break;
    case ':': kind = AGL_TOKEN_COLON;   break;
    case '.': kind = AGL_TOKEN_DOT;     break;
    case '?': kind = AGL_TOKEN_QUESTION; break;
    case '+': kind = AGL_TOKEN_PLUS;    break;
    case '*': kind = AGL_TOKEN_STAR;    break;
    case '/': kind = AGL_TOKEN_SLASH;   break;
    case '%': kind = AGL_TOKEN_PERCENT; break;
    case '-':
        kind = match(lexer, '>') ? AGL_TOKEN_ARROW : AGL_TOKEN_MINUS;
        break;
    case '=':
        kind = match(lexer, '=') ? AGL_TOKEN_EQ : AGL_TOKEN_ASSIGN;
        break;
    case '!':
        kind = match(lexer, '=') ? AGL_TOKEN_NEQ : AGL_TOKEN_NOT;
        break;
    case '<':
        kind = match(lexer, '=') ? AGL_TOKEN_LE : AGL_TOKEN_LT;
        break;
    case '>':
        kind = match(lexer, '=') ? AGL_TOKEN_GE : AGL_TOKEN_GT;
        break;
    case '&':
        if (match(lexer, '&')) { kind = AGL_TOKEN_AND; }
        else {
            tok = error_token(lexer, "unexpected character '&', did you mean '&&'?",
                              start, line, col);
            lexer->insert_newline = false;
            return tok;
        }
        break;
    case '|':
        if (match(lexer, '|')) { kind = AGL_TOKEN_OR; }
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

const char *agl_token_kind_name(AglTokenKind kind) {
    switch (kind) {
    case AGL_TOKEN_INT:       return "INT";
    case AGL_TOKEN_FLOAT:     return "FLOAT";
    case AGL_TOKEN_STRING:    return "STRING";
    case AGL_TOKEN_TRUE:      return "true";
    case AGL_TOKEN_FALSE:     return "false";
    case AGL_TOKEN_IDENT:     return "IDENT";
    case AGL_TOKEN_LET:       return "let";
    case AGL_TOKEN_VAR:       return "var";
    case AGL_TOKEN_FN:        return "fn";
    case AGL_TOKEN_RETURN:    return "return";
    case AGL_TOKEN_IF:        return "if";
    case AGL_TOKEN_ELSE:      return "else";
    case AGL_TOKEN_WHILE:     return "while";
    case AGL_TOKEN_FOR:       return "for";
    case AGL_TOKEN_IN:        return "in";
    case AGL_TOKEN_STRUCT:    return "struct";
    case AGL_TOKEN_IMPORT:    return "import";
    case AGL_TOKEN_MATCH:     return "match";
    case AGL_TOKEN_OK:        return "ok";
    case AGL_TOKEN_ERR:       return "err";
    case AGL_TOKEN_BREAK:     return "break";
    case AGL_TOKEN_CONTINUE:  return "continue";
    case AGL_TOKEN_PLUS:      return "+";
    case AGL_TOKEN_MINUS:     return "-";
    case AGL_TOKEN_STAR:      return "*";
    case AGL_TOKEN_SLASH:     return "/";
    case AGL_TOKEN_PERCENT:   return "%";
    case AGL_TOKEN_ASSIGN:    return "=";
    case AGL_TOKEN_EQ:        return "==";
    case AGL_TOKEN_NEQ:       return "!=";
    case AGL_TOKEN_LT:        return "<";
    case AGL_TOKEN_GT:        return ">";
    case AGL_TOKEN_LE:        return "<=";
    case AGL_TOKEN_GE:        return ">=";
    case AGL_TOKEN_AND:       return "&&";
    case AGL_TOKEN_OR:        return "||";
    case AGL_TOKEN_NOT:       return "!";
    case AGL_TOKEN_ARROW:     return "->";
    case AGL_TOKEN_DOT:       return ".";
    case AGL_TOKEN_QUESTION:  return "?";
    case AGL_TOKEN_LPAREN:    return "(";
    case AGL_TOKEN_RPAREN:    return ")";
    case AGL_TOKEN_LBRACE:    return "{";
    case AGL_TOKEN_RBRACE:    return "}";
    case AGL_TOKEN_LBRACKET:  return "[";
    case AGL_TOKEN_RBRACKET:  return "]";
    case AGL_TOKEN_COMMA:     return ",";
    case AGL_TOKEN_COLON:     return ":";
    case AGL_TOKEN_NEWLINE:   return "NEWLINE";
    case AGL_TOKEN_EOF:       return "EOF";
    case AGL_TOKEN_ERROR:     return "ERROR";
    }
    return "UNKNOWN";
}
