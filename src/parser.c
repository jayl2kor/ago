#include "parser.h"
#include <errno.h>

/* ---- Helpers ---- */

static void parser_advance(AgoParser *p) {
    p->previous = p->current;
    p->current = ago_lexer_next_token(&p->lexer);
}

static bool parser_check(const AgoParser *p, AgoTokenKind kind) {
    return p->current.kind == kind;
}

static bool parser_match(AgoParser *p, AgoTokenKind kind) {
    if (!parser_check(p, kind)) return false;
    parser_advance(p);
    return true;
}

static void parser_expect(AgoParser *p, AgoTokenKind kind, const char *msg) {
    if (parser_check(p, kind)) {
        parser_advance(p);
        return;
    }
    if (!ago_error_occurred(p->ctx)) {
        ago_error_set(p->ctx, AGO_ERR_SYNTAX,
                      ago_loc(p->lexer.file, p->current.line, p->current.column),
                      "%s, got '%s'", msg, ago_token_kind_name(p->current.kind));
    }
}

static void skip_newlines(AgoParser *p) {
    while (parser_check(p, AGO_TOKEN_NEWLINE)) {
        parser_advance(p);
    }
}

static AgoNode *node_new(AgoParser *p, AgoNodeKind kind) {
    return ago_ast_new(p->arena, kind, p->previous.line, p->previous.column);
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

static Precedence get_precedence(AgoTokenKind kind) {
    switch (kind) {
    case AGO_TOKEN_OR:      return PREC_OR;
    case AGO_TOKEN_AND:     return PREC_AND;
    case AGO_TOKEN_EQ:
    case AGO_TOKEN_NEQ:     return PREC_EQUALITY;
    case AGO_TOKEN_LT:
    case AGO_TOKEN_GT:
    case AGO_TOKEN_LE:
    case AGO_TOKEN_GE:      return PREC_COMPARISON;
    case AGO_TOKEN_PLUS:
    case AGO_TOKEN_MINUS:   return PREC_TERM;
    case AGO_TOKEN_STAR:
    case AGO_TOKEN_SLASH:
    case AGO_TOKEN_PERCENT: return PREC_FACTOR;
    case AGO_TOKEN_LPAREN:
    case AGO_TOKEN_DOT:
    case AGO_TOKEN_LBRACKET: return PREC_CALL;
    default:                return PREC_NONE;
    }
}

/* Forward declarations */
static AgoNode *parse_expression(AgoParser *p, Precedence min_prec);
static AgoNode *parse_statement(AgoParser *p);
static AgoNode *parse_block(AgoParser *p);

/* ---- Expression parsing (Pratt) ---- */

static AgoNode *parse_int_literal(AgoParser *p) {
    AgoNode *n = node_new(p, AGO_NODE_INT_LIT);
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

static AgoNode *parse_float_literal(AgoParser *p) {
    AgoNode *n = node_new(p, AGO_NODE_FLOAT_LIT);
    if (!n) return NULL;
    char buf[64];
    int len = p->previous.length;
    if (len >= (int)sizeof(buf)) len = (int)sizeof(buf) - 1;
    memcpy(buf, p->previous.start, (size_t)len);
    buf[len] = '\0';
    n->as.float_lit.value = strtod(buf, NULL);
    return n;
}

static AgoNode *parse_string_literal(AgoParser *p) {
    AgoNode *n = node_new(p, AGO_NODE_STRING_LIT);
    if (!n) return NULL;
    /* Token includes quotes */
    n->as.string_lit.value = p->previous.start;
    n->as.string_lit.length = p->previous.length;
    return n;
}

static AgoNode *parse_bool_literal(AgoParser *p, bool value) {
    AgoNode *n = node_new(p, AGO_NODE_BOOL_LIT);
    if (!n) return NULL;
    n->as.bool_lit.value = value;
    return n;
}

static AgoNode *parse_identifier(AgoParser *p) {
    AgoNode *n = node_new(p, AGO_NODE_IDENT);
    if (!n) return NULL;
    n->as.ident.name = p->previous.start;
    n->as.ident.length = p->previous.length;
    return n;
}

static AgoNode *parse_grouped(AgoParser *p) {
    AgoNode *expr = parse_expression(p, PREC_NONE);
    parser_expect(p, AGO_TOKEN_RPAREN, "expected ')'");
    return expr;
}

static AgoNode *parse_unary(AgoParser *p, AgoTokenKind op) {
    AgoNode *n = node_new(p, AGO_NODE_UNARY);
    if (!n) return NULL;
    n->as.unary.op = op;
    n->as.unary.operand = parse_expression(p, PREC_UNARY);
    return n;
}

/* Parse prefix expression */
static AgoNode *parse_prefix(AgoParser *p) {
    if (ago_error_occurred(p->ctx)) return NULL;

    parser_advance(p);
    AgoToken tok = p->previous;

    switch (tok.kind) {
    case AGO_TOKEN_INT:     return parse_int_literal(p);
    case AGO_TOKEN_FLOAT:   return parse_float_literal(p);
    case AGO_TOKEN_STRING:  return parse_string_literal(p);
    case AGO_TOKEN_TRUE:    return parse_bool_literal(p, true);
    case AGO_TOKEN_FALSE:   return parse_bool_literal(p, false);
    case AGO_TOKEN_IDENT:   return parse_identifier(p);
    case AGO_TOKEN_LPAREN:  return parse_grouped(p);
    case AGO_TOKEN_NOT:     return parse_unary(p, AGO_TOKEN_NOT);
    case AGO_TOKEN_MINUS:   return parse_unary(p, AGO_TOKEN_MINUS);
    default:
        ago_error_set(p->ctx, AGO_ERR_SYNTAX,
                      ago_loc(p->lexer.file, tok.line, tok.column),
                      "unexpected token '%s'", ago_token_kind_name(tok.kind));
        return NULL;
    }
}

/* Parse call arguments: (expr, expr, ...) */
static AgoNode *parse_call(AgoParser *p, AgoNode *callee) {
    AgoNode *n = node_new(p, AGO_NODE_CALL);
    if (!n) return NULL;
    n->as.call.callee = callee;

    /* Collect args into a temp array */
    AgoNode *args[128];
    int count = 0;

    if (!parser_check(p, AGO_TOKEN_RPAREN)) {
        do {
            skip_newlines(p);
            if (count >= 128) {
                ago_error_set(p->ctx, AGO_ERR_SYNTAX,
                              ago_loc(p->lexer.file, p->current.line, p->current.column),
                              "too many arguments (max 128)");
                return NULL;
            }
            args[count++] = parse_expression(p, PREC_NONE);
            if (ago_error_occurred(p->ctx)) return NULL;
            skip_newlines(p);
        } while (parser_match(p, AGO_TOKEN_COMMA));
    }

    parser_expect(p, AGO_TOKEN_RPAREN, "expected ')' after arguments");
    if (ago_error_occurred(p->ctx)) return NULL;

    n->as.call.arg_count = count;
    if (count > 0) {
        n->as.call.args = ago_arena_alloc(p->arena, sizeof(AgoNode *) * (size_t)count);
        memcpy(n->as.call.args, args, sizeof(AgoNode *) * (size_t)count);
    } else {
        n->as.call.args = NULL;
    }
    return n;
}

/* Parse infix/postfix expression */
static AgoNode *parse_infix(AgoParser *p, AgoNode *left) {
    AgoTokenKind op = p->previous.kind;

    /* Function call */
    if (op == AGO_TOKEN_LPAREN) {
        return parse_call(p, left);
    }

    /* Field access */
    if (op == AGO_TOKEN_DOT) {
        parser_expect(p, AGO_TOKEN_IDENT, "expected field name after '.'");
        if (ago_error_occurred(p->ctx)) return NULL;
        AgoNode *n = node_new(p, AGO_NODE_BINARY);
        if (!n) return NULL;
        n->as.binary.op = AGO_TOKEN_DOT;
        n->as.binary.left = left;
        n->as.binary.right = parse_identifier(p);
        return n;
    }

    /* Binary operator */
    Precedence prec = get_precedence(op);
    AgoNode *right = parse_expression(p, prec); /* left-associative: same precedence */
    if (ago_error_occurred(p->ctx)) return NULL;

    AgoNode *n = node_new(p, AGO_NODE_BINARY);
    if (!n) return NULL;
    n->as.binary.op = op;
    n->as.binary.left = left;
    n->as.binary.right = right;
    return n;
}

/* Pratt expression parser */
static AgoNode *parse_expression(AgoParser *p, Precedence min_prec) {
    AgoNode *left = parse_prefix(p);
    if (ago_error_occurred(p->ctx)) return NULL;

    for (;;) {
        Precedence prec = get_precedence(p->current.kind);
        if (prec <= min_prec) break;

        parser_advance(p);
        left = parse_infix(p, left);
        if (ago_error_occurred(p->ctx)) return NULL;
    }

    return left;
}

/* ---- Statement parsing ---- */

static AgoNode *parse_var_declaration(AgoParser *p, AgoNodeKind kind) {
    /* let/var already consumed */
    parser_expect(p, AGO_TOKEN_IDENT, "expected variable name");
    if (ago_error_occurred(p->ctx)) return NULL;

    AgoNode *n = node_new(p, kind);
    if (!n) return NULL;
    n->as.var_decl.name = p->previous.start;
    n->as.var_decl.name_length = p->previous.length;
    n->as.var_decl.type_name = NULL;
    n->as.var_decl.initializer = NULL;

    /* Optional type annotation: : type */
    if (parser_match(p, AGO_TOKEN_COLON)) {
        parser_expect(p, AGO_TOKEN_IDENT, "expected type name");
        if (ago_error_occurred(p->ctx)) return NULL;
        n->as.var_decl.type_name = p->previous.start;
        n->as.var_decl.type_name_length = p->previous.length;
    }

    /* = initializer */
    if (parser_match(p, AGO_TOKEN_ASSIGN)) {
        n->as.var_decl.initializer = parse_expression(p, PREC_NONE);
    }

    return n;
}

static AgoNode *parse_return_statement(AgoParser *p) {
    AgoNode *n = node_new(p, AGO_NODE_RETURN_STMT);
    if (!n) return NULL;
    n->as.return_stmt.value = NULL;

    /* return can be followed by an expression or newline/EOF */
    if (!parser_check(p, AGO_TOKEN_NEWLINE) && !parser_check(p, AGO_TOKEN_EOF) &&
        !parser_check(p, AGO_TOKEN_RBRACE)) {
        n->as.return_stmt.value = parse_expression(p, PREC_NONE);
    }

    return n;
}

static AgoNode *parse_if_statement(AgoParser *p) {
    AgoNode *n = node_new(p, AGO_NODE_IF_STMT);
    if (!n) return NULL;

    n->as.if_stmt.condition = parse_expression(p, PREC_NONE);
    if (ago_error_occurred(p->ctx)) return NULL;

    skip_newlines(p);
    n->as.if_stmt.then_block = parse_block(p);
    if (ago_error_occurred(p->ctx)) return NULL;

    n->as.if_stmt.else_block = NULL;
    skip_newlines(p);
    if (parser_match(p, AGO_TOKEN_ELSE)) {
        skip_newlines(p);
        if (parser_check(p, AGO_TOKEN_IF)) {
            /* else if ... */
            parser_advance(p);
            n->as.if_stmt.else_block = parse_if_statement(p);
        } else {
            n->as.if_stmt.else_block = parse_block(p);
        }
    }

    return n;
}

static AgoNode *parse_while_statement(AgoParser *p) {
    AgoNode *n = node_new(p, AGO_NODE_WHILE_STMT);
    if (!n) return NULL;

    n->as.while_stmt.condition = parse_expression(p, PREC_NONE);
    if (ago_error_occurred(p->ctx)) return NULL;

    skip_newlines(p);
    n->as.while_stmt.body = parse_block(p);
    return n;
}

static AgoNode *parse_for_statement(AgoParser *p) {
    AgoNode *n = node_new(p, AGO_NODE_FOR_STMT);
    if (!n) return NULL;

    parser_expect(p, AGO_TOKEN_IDENT, "expected variable name after 'for'");
    if (ago_error_occurred(p->ctx)) return NULL;
    n->as.for_stmt.var_name = p->previous.start;
    n->as.for_stmt.var_name_length = p->previous.length;

    parser_expect(p, AGO_TOKEN_IN, "expected 'in' after variable name");
    if (ago_error_occurred(p->ctx)) return NULL;

    n->as.for_stmt.iterable = parse_expression(p, PREC_NONE);
    if (ago_error_occurred(p->ctx)) return NULL;

    skip_newlines(p);
    n->as.for_stmt.body = parse_block(p);
    return n;
}

/* Parse a block: { stmt; stmt; ... } */
static AgoNode *parse_block(AgoParser *p) {
    parser_expect(p, AGO_TOKEN_LBRACE, "expected '{'");
    if (ago_error_occurred(p->ctx)) return NULL;

    AgoNode *n = node_new(p, AGO_NODE_BLOCK);
    if (!n) return NULL;

    AgoNode *stmts[256];
    int count = 0;

    skip_newlines(p);
    while (!parser_check(p, AGO_TOKEN_RBRACE) && !parser_check(p, AGO_TOKEN_EOF)) {
        if (ago_error_occurred(p->ctx)) return NULL;
        if (count >= 256) {
            ago_error_set(p->ctx, AGO_ERR_SYNTAX,
                          ago_loc(p->lexer.file, p->current.line, p->current.column),
                          "too many statements in block (max 256)");
            return NULL;
        }
        stmts[count++] = parse_statement(p);
        skip_newlines(p);
    }

    parser_expect(p, AGO_TOKEN_RBRACE, "expected '}'");

    n->as.block.stmt_count = count;
    if (count > 0) {
        n->as.block.stmts = ago_arena_alloc(p->arena, sizeof(AgoNode *) * (size_t)count);
        memcpy(n->as.block.stmts, stmts, sizeof(AgoNode *) * (size_t)count);
    } else {
        n->as.block.stmts = NULL;
    }
    return n;
}

/* Parse function declaration */
static AgoNode *parse_fn_declaration(AgoParser *p) {
    parser_expect(p, AGO_TOKEN_IDENT, "expected function name");
    if (ago_error_occurred(p->ctx)) return NULL;

    AgoNode *n = node_new(p, AGO_NODE_FN_DECL);
    if (!n) return NULL;
    n->as.fn_decl.name = p->previous.start;
    n->as.fn_decl.name_length = p->previous.length;

    /* Parameters */
    parser_expect(p, AGO_TOKEN_LPAREN, "expected '(' after function name");
    if (ago_error_occurred(p->ctx)) return NULL;

    const char *param_names[64];
    int param_name_lens[64];
    const char *param_types[64];
    int param_type_lens[64];
    int param_count = 0;

    if (!parser_check(p, AGO_TOKEN_RPAREN)) {
        do {
            skip_newlines(p);
            parser_expect(p, AGO_TOKEN_IDENT, "expected parameter name");
            if (ago_error_occurred(p->ctx)) return NULL;
            param_names[param_count] = p->previous.start;
            param_name_lens[param_count] = p->previous.length;

            parser_expect(p, AGO_TOKEN_COLON, "expected ':' after parameter name");
            if (ago_error_occurred(p->ctx)) return NULL;

            parser_expect(p, AGO_TOKEN_IDENT, "expected parameter type");
            if (ago_error_occurred(p->ctx)) return NULL;
            param_types[param_count] = p->previous.start;
            param_type_lens[param_count] = p->previous.length;

            param_count++;
            skip_newlines(p);
        } while (parser_match(p, AGO_TOKEN_COMMA));
    }

    parser_expect(p, AGO_TOKEN_RPAREN, "expected ')' after parameters");
    if (ago_error_occurred(p->ctx)) return NULL;

    n->as.fn_decl.param_count = param_count;
    if (param_count > 0) {
        n->as.fn_decl.param_names = ago_arena_alloc(p->arena, sizeof(char *) * (size_t)param_count);
        n->as.fn_decl.param_name_lengths = ago_arena_alloc(p->arena, sizeof(int) * (size_t)param_count);
        n->as.fn_decl.param_types = ago_arena_alloc(p->arena, sizeof(char *) * (size_t)param_count);
        n->as.fn_decl.param_type_lengths = ago_arena_alloc(p->arena, sizeof(int) * (size_t)param_count);
        memcpy(n->as.fn_decl.param_names, param_names, sizeof(char *) * (size_t)param_count);
        memcpy(n->as.fn_decl.param_name_lengths, param_name_lens, sizeof(int) * (size_t)param_count);
        memcpy(n->as.fn_decl.param_types, param_types, sizeof(char *) * (size_t)param_count);
        memcpy(n->as.fn_decl.param_type_lengths, param_type_lens, sizeof(int) * (size_t)param_count);
    }

    /* Optional return type: -> type */
    n->as.fn_decl.return_type = NULL;
    if (parser_match(p, AGO_TOKEN_ARROW)) {
        parser_expect(p, AGO_TOKEN_IDENT, "expected return type");
        if (ago_error_occurred(p->ctx)) return NULL;
        n->as.fn_decl.return_type = p->previous.start;
        n->as.fn_decl.return_type_length = p->previous.length;
    }

    /* Body */
    skip_newlines(p);
    n->as.fn_decl.body = parse_block(p);
    return n;
}

/* Parse a single statement */
static AgoNode *parse_statement(AgoParser *p) {
    if (ago_error_occurred(p->ctx)) return NULL;

    if (parser_match(p, AGO_TOKEN_LET)) {
        AgoNode *n = parse_var_declaration(p, AGO_NODE_LET_STMT);
        parser_match(p, AGO_TOKEN_NEWLINE);
        return n;
    }
    if (parser_match(p, AGO_TOKEN_VAR)) {
        AgoNode *n = parse_var_declaration(p, AGO_NODE_VAR_STMT);
        parser_match(p, AGO_TOKEN_NEWLINE);
        return n;
    }
    if (parser_match(p, AGO_TOKEN_RETURN)) {
        AgoNode *n = parse_return_statement(p);
        parser_match(p, AGO_TOKEN_NEWLINE);
        return n;
    }
    if (parser_match(p, AGO_TOKEN_IF)) {
        return parse_if_statement(p);
    }
    if (parser_match(p, AGO_TOKEN_WHILE)) {
        return parse_while_statement(p);
    }
    if (parser_match(p, AGO_TOKEN_FOR)) {
        return parse_for_statement(p);
    }
    if (parser_match(p, AGO_TOKEN_FN)) {
        return parse_fn_declaration(p);
    }

    /* Expression statement — or assignment if followed by '=' */
    AgoNode *expr = parse_expression(p, PREC_NONE);
    if (ago_error_occurred(p->ctx)) return NULL;

    if (parser_match(p, AGO_TOKEN_ASSIGN)) {
        /* Assignment: expr must be an identifier */
        if (expr->kind != AGO_NODE_IDENT) {
            ago_error_set(p->ctx, AGO_ERR_SYNTAX,
                          ago_loc(p->lexer.file, expr->line, expr->column),
                          "invalid assignment target");
            return NULL;
        }
        AgoNode *n = ago_ast_new(p->arena, AGO_NODE_ASSIGN_STMT, expr->line, expr->column);
        if (!n) return NULL;
        n->as.assign_stmt.name = expr->as.ident.name;
        n->as.assign_stmt.name_length = expr->as.ident.length;
        n->as.assign_stmt.value = parse_expression(p, PREC_NONE);
        parser_match(p, AGO_TOKEN_NEWLINE);
        return n;
    }

    AgoNode *n = ago_ast_new(p->arena, AGO_NODE_EXPR_STMT, expr->line, expr->column);
    if (!n) return NULL;
    n->as.expr_stmt.expr = expr;
    parser_match(p, AGO_TOKEN_NEWLINE);
    return n;
}

/* ---- Public API ---- */

void ago_parser_init(AgoParser *parser, const char *source, const char *file,
                     AgoArena *arena, AgoCtx *ctx) {
    ago_lexer_init(&parser->lexer, source, file, ctx);
    parser->arena = arena;
    parser->ctx = ctx;
    /* Prime the parser with the first token */
    parser->current = ago_lexer_next_token(&parser->lexer);
    parser->previous = parser->current;
}

AgoNode *ago_parser_parse_expression(AgoParser *parser) {
    return parse_expression(parser, PREC_NONE);
}

AgoNode *ago_parser_parse(AgoParser *parser) {
    AgoNode *prog = ago_ast_new(parser->arena, AGO_NODE_PROGRAM, 1, 1);
    if (!prog) return NULL;

    AgoNode *decls[512];
    int count = 0;

    skip_newlines(parser);
    while (!parser_check(parser, AGO_TOKEN_EOF)) {
        if (ago_error_occurred(parser->ctx)) return NULL;
        if (count >= 512) {
            ago_error_set(parser->ctx, AGO_ERR_SYNTAX,
                          ago_loc(parser->lexer.file, parser->current.line,
                                  parser->current.column),
                          "too many top-level declarations (max 512)");
            return NULL;
        }
        decls[count++] = parse_statement(parser);
        skip_newlines(parser);
    }

    prog->as.program.decl_count = count;
    if (count > 0) {
        prog->as.program.decls = ago_arena_alloc(parser->arena,
                                                  sizeof(AgoNode *) * (size_t)count);
        memcpy(prog->as.program.decls, decls, sizeof(AgoNode *) * (size_t)count);
    } else {
        prog->as.program.decls = NULL;
    }
    return prog;
}
