#include "parser.h"
#include <errno.h>

/* ---- Helpers ---- */

static void parser_advance(AglParser *p) {
    p->previous = p->current;
    p->current = agl_lexer_next_token(&p->lexer);
}

static bool parser_check(const AglParser *p, AglTokenKind kind) {
    return p->current.kind == kind;
}

static bool parser_match(AglParser *p, AglTokenKind kind) {
    if (!parser_check(p, kind)) return false;
    parser_advance(p);
    return true;
}

static void parser_expect(AglParser *p, AglTokenKind kind, const char *msg) {
    if (parser_check(p, kind)) {
        parser_advance(p);
        return;
    }
    if (!agl_error_occurred(p->ctx)) {
        agl_error_set(p->ctx, AGL_ERR_SYNTAX,
                      agl_loc(p->lexer.file, p->current.line, p->current.column),
                      "%s, got '%s'", msg, agl_token_kind_name(p->current.kind));
    }
}

static void skip_newlines(AglParser *p) {
    while (parser_check(p, AGL_TOKEN_NEWLINE)) {
        parser_advance(p);
    }
}

static AglNode *node_new(AglParser *p, AglNodeKind kind) {
    return agl_ast_new(p->arena, kind, p->previous.line, p->previous.column);
}

/* ---- Pratt Parser: Precedence levels ---- */

typedef enum {
    PREC_NONE,
    PREC_OR,          /* || */
    PREC_AND,         /* && */
    PREC_EQUALITY,    /* == != */
    PREC_COMPARISON,  /* < > <= >= */
    PREC_TERM,        /* + - */
    PREC_FACTOR,      /* * / % */
    PREC_UNARY,       /* ! - (prefix) */
    PREC_CALL,        /* () . [] */
} Precedence;

static Precedence get_precedence(AglTokenKind kind) {
    switch (kind) {
    case AGL_TOKEN_OR:      return PREC_OR;
    case AGL_TOKEN_AND:     return PREC_AND;
    case AGL_TOKEN_EQ:
    case AGL_TOKEN_NEQ:     return PREC_EQUALITY;
    case AGL_TOKEN_LT:
    case AGL_TOKEN_GT:
    case AGL_TOKEN_LE:
    case AGL_TOKEN_GE:      return PREC_COMPARISON;
    case AGL_TOKEN_PLUS:
    case AGL_TOKEN_MINUS:   return PREC_TERM;
    case AGL_TOKEN_STAR:
    case AGL_TOKEN_SLASH:
    case AGL_TOKEN_PERCENT: return PREC_FACTOR;
    case AGL_TOKEN_LPAREN:
    case AGL_TOKEN_DOT:
    case AGL_TOKEN_LBRACKET: return PREC_CALL;
    default:                return PREC_NONE;
    }
}

/* Forward declarations */
static AglNode *parse_expression(AglParser *p, Precedence min_prec);
static AglNode *parse_statement(AglParser *p);
static AglNode *parse_block(AglParser *p);

/* Accept a type name: identifier or 'fn' keyword (for function types) */
static bool parser_expect_type(AglParser *p) {
    if (parser_match(p, AGL_TOKEN_IDENT) || parser_match(p, AGL_TOKEN_FN)) {
        return true;
    }
    if (!agl_error_occurred(p->ctx)) {
        agl_error_set(p->ctx, AGL_ERR_SYNTAX,
                      agl_loc(p->lexer.file, p->current.line, p->current.column),
                      "expected type name, got '%s'",
                      agl_token_kind_name(p->current.kind));
    }
    return false;
}

/* ---- Expression parsing (Pratt) ---- */

static AglNode *parse_int_literal(AglParser *p) {
    AglNode *n = node_new(p, AGL_NODE_INT_LIT);
    if (!n) return NULL;
    /* Parse the integer from the token text */
    char buf[32];
    int len = p->previous.length;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, p->previous.start, (size_t)len);
    buf[len] = '\0';
    n->as.int_lit.value = strtoll(buf, NULL, 10);
    return n;
}

static AglNode *parse_float_literal(AglParser *p) {
    AglNode *n = node_new(p, AGL_NODE_FLOAT_LIT);
    if (!n) return NULL;
    char buf[64];
    int len = p->previous.length;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, p->previous.start, (size_t)len);
    buf[len] = '\0';
    n->as.float_lit.value = strtod(buf, NULL);
    return n;
}

static AglNode *parse_string_literal(AglParser *p) {
    AglNode *n = node_new(p, AGL_NODE_STRING_LIT);
    if (!n) return NULL;
    /* Token includes quotes */
    n->as.string_lit.value = p->previous.start;
    n->as.string_lit.length = p->previous.length;
    return n;
}

static AglNode *parse_bool_literal(AglParser *p, bool value) {
    AglNode *n = node_new(p, AGL_NODE_BOOL_LIT);
    if (!n) return NULL;
    n->as.bool_lit.value = value;
    return n;
}

static AglNode *parse_identifier(AglParser *p) {
    AglNode *n = node_new(p, AGL_NODE_IDENT);
    if (!n) return NULL;
    n->as.ident.name = p->previous.start;
    n->as.ident.length = p->previous.length;
    return n;
}

static AglNode *parse_grouped(AglParser *p) {
    AglNode *expr = parse_expression(p, PREC_NONE);
    parser_expect(p, AGL_TOKEN_RPAREN, "expected ')'");
    return expr;
}

static AglNode *parse_unary(AglParser *p, AglTokenKind op) {
    AglNode *n = node_new(p, AGL_NODE_UNARY);
    if (!n) return NULL;
    n->as.unary.op = op;
    n->as.unary.operand = parse_expression(p, PREC_UNARY);
    return n;
}

/* Shared: parse (params) -> return_type { body } into n->as.fn_decl.
 * Caller must set node kind and name before calling.
 * '(' must be the current token (not yet consumed). */
static bool parse_fn_params_and_body(AglParser *p, AglNode *n) {
    parser_expect(p, AGL_TOKEN_LPAREN, "expected '('");
    if (agl_error_occurred(p->ctx)) return false;

    const char *pnames[64]; int pname_lens[64];
    const char *ptypes[64]; int ptype_lens[64];
    int pcount = 0;

    if (!parser_check(p, AGL_TOKEN_RPAREN)) {
        do {
            skip_newlines(p);
            if (pcount >= 64) {
                agl_error_set(p->ctx, AGL_ERR_SYNTAX,
                              agl_loc(p->lexer.file, p->current.line, p->current.column),
                              "too many parameters (max 64)");
                return false;
            }
            parser_expect(p, AGL_TOKEN_IDENT, "expected parameter name");
            if (agl_error_occurred(p->ctx)) return false;
            pnames[pcount] = p->previous.start;
            pname_lens[pcount] = p->previous.length;

            parser_expect(p, AGL_TOKEN_COLON, "expected ':' after parameter name");
            if (agl_error_occurred(p->ctx)) return false;

            if (!parser_expect_type(p)) return false;
            ptypes[pcount] = p->previous.start;
            ptype_lens[pcount] = p->previous.length;

            pcount++;
            skip_newlines(p);
        } while (parser_match(p, AGL_TOKEN_COMMA));
    }

    parser_expect(p, AGL_TOKEN_RPAREN, "expected ')' after parameters");
    if (agl_error_occurred(p->ctx)) return false;

    n->as.fn_decl.param_count = pcount;
    if (pcount > 0) {
        size_t sz = (size_t)pcount;
        n->as.fn_decl.param_names = agl_arena_alloc(p->arena, sizeof(char *) * sz);
        n->as.fn_decl.param_name_lengths = agl_arena_alloc(p->arena, sizeof(int) * sz);
        n->as.fn_decl.param_types = agl_arena_alloc(p->arena, sizeof(char *) * sz);
        n->as.fn_decl.param_type_lengths = agl_arena_alloc(p->arena, sizeof(int) * sz);
        memcpy(n->as.fn_decl.param_names, pnames, sizeof(char *) * sz);
        memcpy(n->as.fn_decl.param_name_lengths, pname_lens, sizeof(int) * sz);
        memcpy(n->as.fn_decl.param_types, ptypes, sizeof(char *) * sz);
        memcpy(n->as.fn_decl.param_type_lengths, ptype_lens, sizeof(int) * sz);
    }

    /* Optional return type: -> type */
    n->as.fn_decl.return_type = NULL;
    if (parser_match(p, AGL_TOKEN_ARROW)) {
        if (!parser_expect_type(p)) return false;
        n->as.fn_decl.return_type = p->previous.start;
        n->as.fn_decl.return_type_length = p->previous.length;
    }

    /* Body */
    skip_newlines(p);
    n->as.fn_decl.body = parse_block(p);
    return !agl_error_occurred(p->ctx);
}

/* Parse lambda expression: fn(params) -> type { body } */
static AglNode *parse_lambda(AglParser *p) {
    AglNode *n = node_new(p, AGL_NODE_LAMBDA);
    if (!n) return NULL;
    n->as.fn_decl.name = NULL;
    n->as.fn_decl.name_length = 0;
    if (!parse_fn_params_and_body(p, n)) return NULL;
    return n;
}

/* Parse ok(expr) or err(expr) */
static AglNode *parse_result_wrap(AglParser *p, AglNodeKind kind) {
    AglNode *n = node_new(p, kind);
    if (!n) return NULL;
    parser_expect(p, AGL_TOKEN_LPAREN, kind == AGL_NODE_RESULT_OK
                  ? "expected '(' after 'ok'" : "expected '(' after 'err'");
    if (agl_error_occurred(p->ctx)) return NULL;
    n->as.result_val.value = parse_expression(p, PREC_NONE);
    if (agl_error_occurred(p->ctx)) return NULL;
    parser_expect(p, AGL_TOKEN_RPAREN, "expected ')'");
    return n;
}

/* Parse match expression: match expr { ok(n) -> expr \n err(n) -> expr } */
static AglNode *parse_match_expression(AglParser *p) {
    AglNode *n = node_new(p, AGL_NODE_MATCH_EXPR);
    if (!n) return NULL;

    n->as.match_expr.subject = parse_expression(p, PREC_NONE);
    if (agl_error_occurred(p->ctx)) return NULL;

    skip_newlines(p);
    parser_expect(p, AGL_TOKEN_LBRACE, "expected '{' after match expression");
    if (agl_error_occurred(p->ctx)) return NULL;

    n->as.match_expr.ok_name = NULL;
    n->as.match_expr.ok_body = NULL;
    n->as.match_expr.err_name = NULL;
    n->as.match_expr.err_body = NULL;

    /* Parse two arms: ok and err (in any order) */
    for (int arm = 0; arm < 2; arm++) {
        skip_newlines(p);
        if (parser_match(p, AGL_TOKEN_OK)) {
            if (n->as.match_expr.ok_body) {
                agl_error_set(p->ctx, AGL_ERR_SYNTAX,
                              agl_loc(p->lexer.file, p->previous.line, p->previous.column),
                              "duplicate 'ok' arm in match");
                return NULL;
            }
            parser_expect(p, AGL_TOKEN_LPAREN, "expected '(' after 'ok'");
            if (agl_error_occurred(p->ctx)) return NULL;
            parser_expect(p, AGL_TOKEN_IDENT, "expected binding name");
            if (agl_error_occurred(p->ctx)) return NULL;
            n->as.match_expr.ok_name = p->previous.start;
            n->as.match_expr.ok_name_length = p->previous.length;
            parser_expect(p, AGL_TOKEN_RPAREN, "expected ')'");
            if (agl_error_occurred(p->ctx)) return NULL;
            parser_expect(p, AGL_TOKEN_ARROW, "expected '->' after pattern");
            if (agl_error_occurred(p->ctx)) return NULL;
            n->as.match_expr.ok_body = parse_expression(p, PREC_NONE);
            if (agl_error_occurred(p->ctx)) return NULL;
        } else if (parser_match(p, AGL_TOKEN_ERR)) {
            if (n->as.match_expr.err_body) {
                agl_error_set(p->ctx, AGL_ERR_SYNTAX,
                              agl_loc(p->lexer.file, p->previous.line, p->previous.column),
                              "duplicate 'err' arm in match");
                return NULL;
            }
            parser_expect(p, AGL_TOKEN_LPAREN, "expected '(' after 'err'");
            if (agl_error_occurred(p->ctx)) return NULL;
            parser_expect(p, AGL_TOKEN_IDENT, "expected binding name");
            if (agl_error_occurred(p->ctx)) return NULL;
            n->as.match_expr.err_name = p->previous.start;
            n->as.match_expr.err_name_length = p->previous.length;
            parser_expect(p, AGL_TOKEN_RPAREN, "expected ')'");
            if (agl_error_occurred(p->ctx)) return NULL;
            parser_expect(p, AGL_TOKEN_ARROW, "expected '->' after pattern");
            if (agl_error_occurred(p->ctx)) return NULL;
            n->as.match_expr.err_body = parse_expression(p, PREC_NONE);
            if (agl_error_occurred(p->ctx)) return NULL;
        } else {
            agl_error_set(p->ctx, AGL_ERR_SYNTAX,
                          agl_loc(p->lexer.file, p->current.line, p->current.column),
                          "expected 'ok' or 'err' arm in match");
            return NULL;
        }
        skip_newlines(p);
    }

    parser_expect(p, AGL_TOKEN_RBRACE, "expected '}' after match arms");
    return n;
}

/* Parse interpolated string: f"text {expr} text" */
static AglNode *parse_interpolated_string(AglParser *p) {
    /* p->previous is 'f', p->current is STRING */
    parser_advance(p); /* consume the string token */
    AglToken str_tok = p->previous;
    int line = str_tok.line;
    int col = str_tok.column;

    /* String content (between quotes) */
    const char *content = str_tok.start + 1;       /* skip opening " */
    int content_len = str_tok.length - 2;           /* exclude both quotes */

    AglNode *result = NULL;  /* accumulated expression */
    const char *seg_start = content;
    const char *end = content + content_len;

    while (seg_start <= end) {
        /* Find next '{' in the remaining content */
        const char *brace = seg_start;
        while (brace < end && *brace != '{') {
            if (*brace == '\\') brace++; /* skip escaped char */
            brace++;
        }

        /* Literal segment before the brace (or rest of string if no brace) */
        int seg_len = (int)(brace - seg_start);
        if (seg_len > 0 || brace == end) {
            if (seg_len > 0 || !result) {
                /* Build a quoted string literal in the arena for this segment */
                int quoted_len = seg_len + 2; /* + quotes */
                char *buf = agl_arena_alloc(p->arena, (size_t)quoted_len);
                if (!buf) return NULL;
                buf[0] = '"';
                memcpy(buf + 1, seg_start, (size_t)seg_len);
                buf[quoted_len - 1] = '"';

                AglNode *lit = agl_ast_new(p->arena, AGL_NODE_STRING_LIT, line, col);
                if (!lit) return NULL;
                lit->as.string_lit.value = buf;
                lit->as.string_lit.length = quoted_len;

                if (!result) {
                    result = lit;
                } else {
                    AglNode *add = agl_ast_new(p->arena, AGL_NODE_BINARY, line, col);
                    if (!add) return NULL;
                    add->as.binary.op = AGL_TOKEN_PLUS;
                    add->as.binary.left = result;
                    add->as.binary.right = lit;
                    result = add;
                }
            }
        }

        if (brace >= end) break; /* no more interpolations */

        /* Find matching '}' — handle nested braces */
        const char *expr_start = brace + 1;
        int depth = 1;
        const char *expr_end = expr_start;
        while (expr_end < end && depth > 0) {
            if (*expr_end == '{') depth++;
            else if (*expr_end == '}') depth--;
            if (depth > 0) expr_end++;
        }

        if (depth != 0) {
            agl_error_set(p->ctx, AGL_ERR_SYNTAX,
                          agl_loc(p->lexer.file, line, col),
                          "unterminated interpolation expression in f-string");
            return NULL;
        }

        /* Parse the expression between { and } using a sub-parser */
        int expr_len = (int)(expr_end - expr_start);
        AglParser sub;
        agl_parser_init(&sub, expr_start, p->lexer.file, p->arena, p->ctx);
        /* Override the sub-lexer to stop at the end of the expression.
         * We do this by creating a null-terminated copy of the expression. */
        char *expr_buf = agl_arena_alloc(p->arena, (size_t)(expr_len + 1));
        if (!expr_buf) return NULL;
        memcpy(expr_buf, expr_start, (size_t)expr_len);
        expr_buf[expr_len] = '\0';

        agl_parser_init(&sub, expr_buf, p->lexer.file, p->arena, p->ctx);
        AglNode *expr = parse_expression(&sub, PREC_NONE);
        if (agl_error_occurred(p->ctx)) return NULL;

        /* Wrap expression in str() call */
        AglNode *str_ident = agl_ast_new(p->arena, AGL_NODE_IDENT, line, col);
        if (!str_ident) return NULL;
        str_ident->as.ident.name = "str";
        str_ident->as.ident.length = 3;

        AglNode *str_call = agl_ast_new(p->arena, AGL_NODE_CALL, line, col);
        if (!str_call) return NULL;
        str_call->as.call.callee = str_ident;
        str_call->as.call.arg_count = 1;
        str_call->as.call.args = agl_arena_alloc(p->arena, sizeof(AglNode *));
        str_call->as.call.args[0] = expr;

        if (!result) {
            result = str_call;
        } else {
            AglNode *add = agl_ast_new(p->arena, AGL_NODE_BINARY, line, col);
            if (!add) return NULL;
            add->as.binary.op = AGL_TOKEN_PLUS;
            add->as.binary.left = result;
            add->as.binary.right = str_call;
            result = add;
        }

        seg_start = expr_end + 1; /* skip past '}' */
    }

    if (!result) {
        /* Empty f"" — return empty string */
        AglNode *lit = agl_ast_new(p->arena, AGL_NODE_STRING_LIT, line, col);
        if (!lit) return NULL;
        lit->as.string_lit.value = "\"\"";
        lit->as.string_lit.length = 2;
        return lit;
    }

    return result;
}

/* Parse prefix expression */
static AglNode *parse_prefix(AglParser *p) {
    if (agl_error_occurred(p->ctx)) return NULL;

    parser_advance(p);
    AglToken tok = p->previous;

    switch (tok.kind) {
    case AGL_TOKEN_INT:     return parse_int_literal(p);
    case AGL_TOKEN_FLOAT:   return parse_float_literal(p);
    case AGL_TOKEN_STRING:  return parse_string_literal(p);
    case AGL_TOKEN_TRUE:    return parse_bool_literal(p, true);
    case AGL_TOKEN_FALSE:   return parse_bool_literal(p, false);
    case AGL_TOKEN_IDENT: {
        /* Check for f-string interpolation: f"..." */
        if (tok.length == 1 && tok.start[0] == 'f' &&
            parser_check(p, AGL_TOKEN_STRING)) {
            return parse_interpolated_string(p);
        }
        /* Check for struct literal: Name { field: val, ... }
         * Only if current is { AND the token after { is IDENT followed by : */
        AglNode *id = parse_identifier(p);
        if (!parser_check(p, AGL_TOKEN_LBRACE)) return id;
        /* Lookahead: probe a copy of the lexer to check for ident : pattern.
         * No restore needed — probe is a stack copy, not p->lexer itself. */
        {
            AglLexer probe = p->lexer;
            AglToken t1 = agl_lexer_next_token(&probe); /* skip { */
            while (t1.kind == AGL_TOKEN_NEWLINE) t1 = agl_lexer_next_token(&probe);
            AglToken t2 = agl_lexer_next_token(&probe);
            bool is_struct = (t1.kind == AGL_TOKEN_IDENT && t2.kind == AGL_TOKEN_COLON)
                          || t1.kind == AGL_TOKEN_RBRACE; /* empty struct {} */
            if (!is_struct) return id;
        }
        parser_advance(p); /* consume { */
        AglNode *n = agl_ast_new(p->arena, AGL_NODE_STRUCT_LIT, id->line, id->column);
        if (!n) return NULL;
        n->as.struct_lit.name = id->as.ident.name;
        n->as.struct_lit.name_length = id->as.ident.length;
        const char *fnames[64]; int fname_lens[64];
        AglNode *fvals[64]; int fcount = 0;
        skip_newlines(p);
        while (!parser_check(p, AGL_TOKEN_RBRACE) && !parser_check(p, AGL_TOKEN_EOF)) {
            parser_expect(p, AGL_TOKEN_IDENT, "expected field name");
            if (agl_error_occurred(p->ctx)) return NULL;
            if (fcount >= 64) { agl_error_set(p->ctx, AGL_ERR_SYNTAX, agl_loc(p->lexer.file, p->current.line, p->current.column), "too many struct fields (max 64)"); return NULL; }
            fnames[fcount] = p->previous.start;
            fname_lens[fcount] = p->previous.length;
            parser_expect(p, AGL_TOKEN_COLON, "expected ':' after field name");
            if (agl_error_occurred(p->ctx)) return NULL;
            fvals[fcount] = parse_expression(p, PREC_NONE);
            if (agl_error_occurred(p->ctx)) return NULL;
            fcount++;
            skip_newlines(p);
            if (!parser_match(p, AGL_TOKEN_COMMA)) break;
            skip_newlines(p);
        }
        parser_expect(p, AGL_TOKEN_RBRACE, "expected '}'");
        n->as.struct_lit.field_count = fcount;
        if (fcount > 0) {
            n->as.struct_lit.field_names = agl_arena_alloc(p->arena, sizeof(char*) * (size_t)fcount);
            n->as.struct_lit.field_name_lengths = agl_arena_alloc(p->arena, sizeof(int) * (size_t)fcount);
            n->as.struct_lit.field_values = agl_arena_alloc(p->arena, sizeof(AglNode*) * (size_t)fcount);
            memcpy(n->as.struct_lit.field_names, fnames, sizeof(char*) * (size_t)fcount);
            memcpy(n->as.struct_lit.field_name_lengths, fname_lens, sizeof(int) * (size_t)fcount);
            memcpy(n->as.struct_lit.field_values, fvals, sizeof(AglNode*) * (size_t)fcount);
        }
        return n;
    }
    case AGL_TOKEN_LBRACKET: {
        /* Array literal: [expr, expr, ...] */
        AglNode *n = node_new(p, AGL_NODE_ARRAY_LIT);
        if (!n) return NULL;
        AglNode *elems[128]; int count = 0;
        skip_newlines(p);
        if (!parser_check(p, AGL_TOKEN_RBRACKET)) {
            do {
                skip_newlines(p);
                if (count >= 128) { agl_error_set(p->ctx, AGL_ERR_SYNTAX, agl_loc(p->lexer.file, p->current.line, p->current.column), "too many array elements (max 128)"); return NULL; }
                elems[count++] = parse_expression(p, PREC_NONE);
                if (agl_error_occurred(p->ctx)) return NULL;
                skip_newlines(p);
            } while (parser_match(p, AGL_TOKEN_COMMA));
        }
        parser_expect(p, AGL_TOKEN_RBRACKET, "expected ']'");
        n->as.array_lit.count = count;
        if (count > 0) {
            n->as.array_lit.elements = agl_arena_alloc(p->arena, sizeof(AglNode*) * (size_t)count);
            memcpy(n->as.array_lit.elements, elems, sizeof(AglNode*) * (size_t)count);
        } else {
            n->as.array_lit.elements = NULL;
        }
        return n;
    }
    case AGL_TOKEN_LBRACE: {
        /* Map literal: {"key": val, "key2": val2, ...} or {} */
        AglNode *n = node_new(p, AGL_NODE_MAP_LIT);
        if (!n) return NULL;
        const char *mkeys[128]; int mkey_lens[128];
        AglNode *mvals[128]; int mcount = 0;
        skip_newlines(p);
        if (!parser_check(p, AGL_TOKEN_RBRACE)) {
            do {
                skip_newlines(p);
                if (mcount >= 128) {
                    agl_error_set(p->ctx, AGL_ERR_SYNTAX,
                                  agl_loc(p->lexer.file, p->current.line, p->current.column),
                                  "too many map entries (max 128)");
                    return NULL;
                }
                parser_expect(p, AGL_TOKEN_STRING, "expected string key in map literal");
                if (agl_error_occurred(p->ctx)) return NULL;
                /* Strip quotes from key */
                mkeys[mcount] = p->previous.start + 1;
                mkey_lens[mcount] = p->previous.length - 2;
                parser_expect(p, AGL_TOKEN_COLON, "expected ':' after map key");
                if (agl_error_occurred(p->ctx)) return NULL;
                mvals[mcount] = parse_expression(p, PREC_NONE);
                if (agl_error_occurred(p->ctx)) return NULL;
                mcount++;
                skip_newlines(p);
            } while (parser_match(p, AGL_TOKEN_COMMA));
        }
        parser_expect(p, AGL_TOKEN_RBRACE, "expected '}'");
        n->as.map_lit.count = mcount;
        if (mcount > 0) {
            n->as.map_lit.keys = agl_arena_alloc(p->arena, sizeof(char *) * (size_t)mcount);
            n->as.map_lit.key_lengths = agl_arena_alloc(p->arena, sizeof(int) * (size_t)mcount);
            n->as.map_lit.values = agl_arena_alloc(p->arena, sizeof(AglNode *) * (size_t)mcount);
            memcpy(n->as.map_lit.keys, mkeys, sizeof(char *) * (size_t)mcount);
            memcpy(n->as.map_lit.key_lengths, mkey_lens, sizeof(int) * (size_t)mcount);
            memcpy(n->as.map_lit.values, mvals, sizeof(AglNode *) * (size_t)mcount);
        } else {
            n->as.map_lit.keys = NULL;
            n->as.map_lit.key_lengths = NULL;
            n->as.map_lit.values = NULL;
        }
        return n;
    }
    case AGL_TOKEN_FN:      return parse_lambda(p);
    case AGL_TOKEN_OK:      return parse_result_wrap(p, AGL_NODE_RESULT_OK);
    case AGL_TOKEN_ERR:     return parse_result_wrap(p, AGL_NODE_RESULT_ERR);
    case AGL_TOKEN_MATCH:   return parse_match_expression(p);
    case AGL_TOKEN_LPAREN:  return parse_grouped(p);
    case AGL_TOKEN_NOT:     return parse_unary(p, AGL_TOKEN_NOT);
    case AGL_TOKEN_MINUS:   return parse_unary(p, AGL_TOKEN_MINUS);
    default:
        agl_error_set(p->ctx, AGL_ERR_SYNTAX,
                      agl_loc(p->lexer.file, tok.line, tok.column),
                      "unexpected token '%s'", agl_token_kind_name(tok.kind));
        return NULL;
    }
}

/* Parse call arguments: (expr, expr, ...) */
static AglNode *parse_call(AglParser *p, AglNode *callee) {
    AglNode *n = node_new(p, AGL_NODE_CALL);
    if (!n) return NULL;
    n->as.call.callee = callee;

    /* Collect args into a temp array */
    AglNode *args[128];
    int count = 0;

    if (!parser_check(p, AGL_TOKEN_RPAREN)) {
        do {
            skip_newlines(p);
            if (count >= 128) {
                agl_error_set(p->ctx, AGL_ERR_SYNTAX,
                              agl_loc(p->lexer.file, p->current.line, p->current.column),
                              "too many arguments (max 128)");
                return NULL;
            }
            args[count++] = parse_expression(p, PREC_NONE);
            if (agl_error_occurred(p->ctx)) return NULL;
            skip_newlines(p);
        } while (parser_match(p, AGL_TOKEN_COMMA));
    }

    parser_expect(p, AGL_TOKEN_RPAREN, "expected ')' after arguments");
    if (agl_error_occurred(p->ctx)) return NULL;

    n->as.call.arg_count = count;
    if (count > 0) {
        n->as.call.args = agl_arena_alloc(p->arena, sizeof(AglNode *) * (size_t)count);
        memcpy(n->as.call.args, args, sizeof(AglNode *) * (size_t)count);
    } else {
        n->as.call.args = NULL;
    }
    return n;
}

/* Parse infix/postfix expression */
static AglNode *parse_infix(AglParser *p, AglNode *left) {
    AglTokenKind op = p->previous.kind;

    /* Function call */
    if (op == AGL_TOKEN_LPAREN) {
        return parse_call(p, left);
    }

    /* Index access: expr[expr] */
    if (op == AGL_TOKEN_LBRACKET) {
        AglNode *n = node_new(p, AGL_NODE_INDEX);
        if (!n) return NULL;
        n->as.index_expr.object = left;
        n->as.index_expr.index = parse_expression(p, PREC_NONE);
        if (agl_error_occurred(p->ctx)) return NULL;
        parser_expect(p, AGL_TOKEN_RBRACKET, "expected ']'");
        return n;
    }

    /* Field access */
    if (op == AGL_TOKEN_DOT) {
        parser_expect(p, AGL_TOKEN_IDENT, "expected field name after '.'");
        if (agl_error_occurred(p->ctx)) return NULL;
        AglNode *n = node_new(p, AGL_NODE_BINARY);
        if (!n) return NULL;
        n->as.binary.op = AGL_TOKEN_DOT;
        n->as.binary.left = left;
        n->as.binary.right = parse_identifier(p);
        return n;
    }

    /* Binary operator */
    Precedence prec = get_precedence(op);
    AglNode *right = parse_expression(p, prec); /* left-associative: same precedence */
    if (agl_error_occurred(p->ctx)) return NULL;

    AglNode *n = node_new(p, AGL_NODE_BINARY);
    if (!n) return NULL;
    n->as.binary.op = op;
    n->as.binary.left = left;
    n->as.binary.right = right;
    return n;
}

/* Pratt expression parser */
static AglNode *parse_expression(AglParser *p, Precedence min_prec) {
    AglNode *left = parse_prefix(p);
    if (agl_error_occurred(p->ctx)) return NULL;

    for (;;) {
        Precedence prec = get_precedence(p->current.kind);
        if (prec <= min_prec) break;

        parser_advance(p);
        left = parse_infix(p, left);
        if (agl_error_occurred(p->ctx)) return NULL;
    }

    return left;
}

/* ---- Statement parsing ---- */

static AglNode *parse_var_declaration(AglParser *p, AglNodeKind kind) {
    /* let/var already consumed */
    parser_expect(p, AGL_TOKEN_IDENT, "expected variable name");
    if (agl_error_occurred(p->ctx)) return NULL;

    AglNode *n = node_new(p, kind);
    if (!n) return NULL;
    n->as.var_decl.name = p->previous.start;
    n->as.var_decl.name_length = p->previous.length;
    n->as.var_decl.type_name = NULL;
    n->as.var_decl.initializer = NULL;

    /* Optional type annotation: : type */
    if (parser_match(p, AGL_TOKEN_COLON)) {
        parser_expect(p, AGL_TOKEN_IDENT, "expected type name");
        if (agl_error_occurred(p->ctx)) return NULL;
        n->as.var_decl.type_name = p->previous.start;
        n->as.var_decl.type_name_length = p->previous.length;
    }

    /* = initializer */
    if (parser_match(p, AGL_TOKEN_ASSIGN)) {
        n->as.var_decl.initializer = parse_expression(p, PREC_NONE);
    }

    return n;
}

static AglNode *parse_return_statement(AglParser *p) {
    AglNode *n = node_new(p, AGL_NODE_RETURN_STMT);
    if (!n) return NULL;
    n->as.return_stmt.value = NULL;

    /* return can be followed by an expression or newline/EOF */
    if (!parser_check(p, AGL_TOKEN_NEWLINE) && !parser_check(p, AGL_TOKEN_EOF) &&
        !parser_check(p, AGL_TOKEN_RBRACE)) {
        n->as.return_stmt.value = parse_expression(p, PREC_NONE);
    }

    return n;
}

static AglNode *parse_if_statement(AglParser *p) {
    AglNode *n = node_new(p, AGL_NODE_IF_STMT);
    if (!n) return NULL;

    n->as.if_stmt.condition = parse_expression(p, PREC_NONE);
    if (agl_error_occurred(p->ctx)) return NULL;

    skip_newlines(p);
    n->as.if_stmt.then_block = parse_block(p);
    if (agl_error_occurred(p->ctx)) return NULL;

    n->as.if_stmt.else_block = NULL;
    skip_newlines(p);
    if (parser_match(p, AGL_TOKEN_ELSE)) {
        skip_newlines(p);
        if (parser_check(p, AGL_TOKEN_IF)) {
            /* else if ... */
            parser_advance(p);
            n->as.if_stmt.else_block = parse_if_statement(p);
        } else {
            n->as.if_stmt.else_block = parse_block(p);
        }
    }

    return n;
}

static AglNode *parse_while_statement(AglParser *p) {
    AglNode *n = node_new(p, AGL_NODE_WHILE_STMT);
    if (!n) return NULL;

    n->as.while_stmt.condition = parse_expression(p, PREC_NONE);
    if (agl_error_occurred(p->ctx)) return NULL;

    skip_newlines(p);
    n->as.while_stmt.body = parse_block(p);
    return n;
}

static AglNode *parse_for_statement(AglParser *p) {
    AglNode *n = node_new(p, AGL_NODE_FOR_STMT);
    if (!n) return NULL;

    parser_expect(p, AGL_TOKEN_IDENT, "expected variable name after 'for'");
    if (agl_error_occurred(p->ctx)) return NULL;
    n->as.for_stmt.var_name = p->previous.start;
    n->as.for_stmt.var_name_length = p->previous.length;

    parser_expect(p, AGL_TOKEN_IN, "expected 'in' after variable name");
    if (agl_error_occurred(p->ctx)) return NULL;

    n->as.for_stmt.iterable = parse_expression(p, PREC_NONE);
    if (agl_error_occurred(p->ctx)) return NULL;

    skip_newlines(p);
    n->as.for_stmt.body = parse_block(p);
    return n;
}

/* Parse a block: { stmt; stmt; ... } */
static AglNode *parse_block(AglParser *p) {
    parser_expect(p, AGL_TOKEN_LBRACE, "expected '{'");
    if (agl_error_occurred(p->ctx)) return NULL;

    AglNode *n = node_new(p, AGL_NODE_BLOCK);
    if (!n) return NULL;

    AglNode *stmts[256];
    int count = 0;

    skip_newlines(p);
    while (!parser_check(p, AGL_TOKEN_RBRACE) && !parser_check(p, AGL_TOKEN_EOF)) {
        if (agl_error_occurred(p->ctx)) return NULL;
        if (count >= 256) {
            agl_error_set(p->ctx, AGL_ERR_SYNTAX,
                          agl_loc(p->lexer.file, p->current.line, p->current.column),
                          "too many statements in block (max 256)");
            return NULL;
        }
        stmts[count++] = parse_statement(p);
        skip_newlines(p);
    }

    parser_expect(p, AGL_TOKEN_RBRACE, "expected '}'");

    n->as.block.stmt_count = count;
    if (count > 0) {
        n->as.block.stmts = agl_arena_alloc(p->arena, sizeof(AglNode *) * (size_t)count);
        memcpy(n->as.block.stmts, stmts, sizeof(AglNode *) * (size_t)count);
    } else {
        n->as.block.stmts = NULL;
    }
    return n;
}

/* Parse function declaration: fn name(params) -> type { body } */
static AglNode *parse_fn_declaration(AglParser *p) {
    parser_expect(p, AGL_TOKEN_IDENT, "expected function name");
    if (agl_error_occurred(p->ctx)) return NULL;

    AglNode *n = node_new(p, AGL_NODE_FN_DECL);
    if (!n) return NULL;
    n->as.fn_decl.name = p->previous.start;
    n->as.fn_decl.name_length = p->previous.length;
    if (!parse_fn_params_and_body(p, n)) return NULL;
    return n;
}

/* Parse a single statement */
static AglNode *parse_statement(AglParser *p) {
    if (agl_error_occurred(p->ctx)) return NULL;

    if (parser_match(p, AGL_TOKEN_LET)) {
        AglNode *n = parse_var_declaration(p, AGL_NODE_LET_STMT);
        parser_match(p, AGL_TOKEN_NEWLINE);
        return n;
    }
    if (parser_match(p, AGL_TOKEN_VAR)) {
        AglNode *n = parse_var_declaration(p, AGL_NODE_VAR_STMT);
        parser_match(p, AGL_TOKEN_NEWLINE);
        return n;
    }
    if (parser_match(p, AGL_TOKEN_RETURN)) {
        AglNode *n = parse_return_statement(p);
        parser_match(p, AGL_TOKEN_NEWLINE);
        return n;
    }
    if (parser_match(p, AGL_TOKEN_IF)) {
        return parse_if_statement(p);
    }
    if (parser_match(p, AGL_TOKEN_WHILE)) {
        return parse_while_statement(p);
    }
    if (parser_match(p, AGL_TOKEN_FOR)) {
        return parse_for_statement(p);
    }
    if (parser_match(p, AGL_TOKEN_BREAK)) {
        AglNode *n = node_new(p, AGL_NODE_BREAK_STMT);
        parser_match(p, AGL_TOKEN_NEWLINE);
        return n;
    }
    if (parser_match(p, AGL_TOKEN_CONTINUE)) {
        AglNode *n = node_new(p, AGL_NODE_CONTINUE_STMT);
        parser_match(p, AGL_TOKEN_NEWLINE);
        return n;
    }
    if (parser_match(p, AGL_TOKEN_IMPORT)) {
        parser_expect(p, AGL_TOKEN_STRING, "expected module path after 'import'");
        if (agl_error_occurred(p->ctx)) return NULL;
        AglNode *n = node_new(p, AGL_NODE_IMPORT);
        if (!n) return NULL;
        /* Strip quotes from path: token includes surrounding " */
        n->as.import_stmt.path = p->previous.start + 1;
        n->as.import_stmt.path_length = p->previous.length - 2;
        parser_match(p, AGL_TOKEN_NEWLINE);
        return n;
    }
    if (parser_match(p, AGL_TOKEN_FN)) {
        return parse_fn_declaration(p);
    }
    if (parser_match(p, AGL_TOKEN_STRUCT)) {
        /* struct Name { field: type \n field: type \n } */
        parser_expect(p, AGL_TOKEN_IDENT, "expected struct name");
        if (agl_error_occurred(p->ctx)) return NULL;
        AglNode *n = node_new(p, AGL_NODE_STRUCT_DECL);
        if (!n) return NULL;
        n->as.struct_decl.name = p->previous.start;
        n->as.struct_decl.name_length = p->previous.length;
        skip_newlines(p);
        parser_expect(p, AGL_TOKEN_LBRACE, "expected '{'");
        if (agl_error_occurred(p->ctx)) return NULL;
        const char *fnames[64]; int flens[64];
        const char *ftypes[64]; int ftlens[64];
        int fcount = 0;
        skip_newlines(p);
        while (!parser_check(p, AGL_TOKEN_RBRACE) && !parser_check(p, AGL_TOKEN_EOF)) {
            if (fcount >= 64) { agl_error_set(p->ctx, AGL_ERR_SYNTAX, agl_loc(p->lexer.file, p->current.line, p->current.column), "too many struct fields (max 64)"); return NULL; }
            parser_expect(p, AGL_TOKEN_IDENT, "expected field name");
            if (agl_error_occurred(p->ctx)) return NULL;
            fnames[fcount] = p->previous.start; flens[fcount] = p->previous.length;
            parser_expect(p, AGL_TOKEN_COLON, "expected ':'");
            if (agl_error_occurred(p->ctx)) return NULL;
            parser_expect(p, AGL_TOKEN_IDENT, "expected field type");
            if (agl_error_occurred(p->ctx)) return NULL;
            ftypes[fcount] = p->previous.start; ftlens[fcount] = p->previous.length;
            fcount++;
            skip_newlines(p);
        }
        parser_expect(p, AGL_TOKEN_RBRACE, "expected '}'");
        n->as.struct_decl.field_count = fcount;
        if (fcount > 0) {
            n->as.struct_decl.field_names = agl_arena_alloc(p->arena, sizeof(char*) * (size_t)fcount);
            n->as.struct_decl.field_name_lengths = agl_arena_alloc(p->arena, sizeof(int) * (size_t)fcount);
            n->as.struct_decl.field_types = agl_arena_alloc(p->arena, sizeof(char*) * (size_t)fcount);
            n->as.struct_decl.field_type_lengths = agl_arena_alloc(p->arena, sizeof(int) * (size_t)fcount);
            memcpy(n->as.struct_decl.field_names, fnames, sizeof(char*) * (size_t)fcount);
            memcpy(n->as.struct_decl.field_name_lengths, flens, sizeof(int) * (size_t)fcount);
            memcpy(n->as.struct_decl.field_types, ftypes, sizeof(char*) * (size_t)fcount);
            memcpy(n->as.struct_decl.field_type_lengths, ftlens, sizeof(int) * (size_t)fcount);
        }
        return n;
    }

    /* Expression statement — or assignment if followed by '=' */
    AglNode *expr = parse_expression(p, PREC_NONE);
    if (agl_error_occurred(p->ctx)) return NULL;

    if (parser_match(p, AGL_TOKEN_ASSIGN)) {
        /* Assignment: expr must be an identifier */
        if (expr->kind != AGL_NODE_IDENT) {
            agl_error_set(p->ctx, AGL_ERR_SYNTAX,
                          agl_loc(p->lexer.file, expr->line, expr->column),
                          "invalid assignment target");
            return NULL;
        }
        AglNode *n = agl_ast_new(p->arena, AGL_NODE_ASSIGN_STMT, expr->line, expr->column);
        if (!n) return NULL;
        n->as.assign_stmt.name = expr->as.ident.name;
        n->as.assign_stmt.name_length = expr->as.ident.length;
        n->as.assign_stmt.value = parse_expression(p, PREC_NONE);
        parser_match(p, AGL_TOKEN_NEWLINE);
        return n;
    }

    AglNode *n = agl_ast_new(p->arena, AGL_NODE_EXPR_STMT, expr->line, expr->column);
    if (!n) return NULL;
    n->as.expr_stmt.expr = expr;
    parser_match(p, AGL_TOKEN_NEWLINE);
    return n;
}

/* ---- Public API ---- */

void agl_parser_init(AglParser *parser, const char *source, const char *file,
                     AglArena *arena, AglCtx *ctx) {
    agl_lexer_init(&parser->lexer, source, file, ctx);
    parser->arena = arena;
    parser->ctx = ctx;
    /* Prime the parser with the first token */
    parser->current = agl_lexer_next_token(&parser->lexer);
    parser->previous = parser->current;
}

AglNode *agl_parser_parse_expression(AglParser *parser) {
    return parse_expression(parser, PREC_NONE);
}

AglNode *agl_parser_parse(AglParser *parser) {
    AglNode *prog = agl_ast_new(parser->arena, AGL_NODE_PROGRAM, 1, 1);
    if (!prog) return NULL;

    AglNode *decls[512];
    int count = 0;

    skip_newlines(parser);
    while (!parser_check(parser, AGL_TOKEN_EOF)) {
        if (agl_error_occurred(parser->ctx)) return NULL;
        if (count >= 512) {
            agl_error_set(parser->ctx, AGL_ERR_SYNTAX,
                          agl_loc(parser->lexer.file, parser->current.line,
                                  parser->current.column),
                          "too many top-level declarations (max 512)");
            return NULL;
        }
        decls[count++] = parse_statement(parser);
        skip_newlines(parser);
    }

    prog->as.program.decl_count = count;
    if (count > 0) {
        prog->as.program.decls = agl_arena_alloc(parser->arena,
                                                  sizeof(AglNode *) * (size_t)count);
        memcpy(prog->as.program.decls, decls, sizeof(AglNode *) * (size_t)count);
    } else {
        prog->as.program.decls = NULL;
    }
    return prog;
}
