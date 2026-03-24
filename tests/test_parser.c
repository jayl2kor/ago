#include "test_harness.h"
#include "../src/parser.h"

/* ---- Helper: parse source and return program node ---- */

static AglNode *parse(const char *source, AglArena *arena, AglCtx *c) {
    AglParser parser;
    agl_parser_init(&parser, source, "test.ago", arena, c);
    return agl_parser_parse(&parser);
}

static AglNode *parse_expr(const char *source, AglArena *arena, AglCtx *c) {
    AglParser parser;
    agl_parser_init(&parser, source, "test.ago", arena, c);
    return agl_parser_parse_expression(&parser);
}

/* ================================================================
 *  RED PHASE: These tests define the expected behavior.
 *  They will fail until the parser is implemented.
 * ================================================================ */

/* ---- Literal expressions ---- */

AGL_TEST(test_parse_int_literal) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *expr = parse_expr("42", a, c);
    AGL_ASSERT_FATAL(ctx, expr != NULL);
    AGL_ASSERT_INT_EQ(ctx, expr->kind, AGL_NODE_INT_LIT);
    AGL_ASSERT(ctx, expr->as.int_lit.value == 42);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_float_literal) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *expr = parse_expr("3.14", a, c);
    AGL_ASSERT_FATAL(ctx, expr != NULL);
    AGL_ASSERT_INT_EQ(ctx, expr->kind, AGL_NODE_FLOAT_LIT);
    AGL_ASSERT(ctx, expr->as.float_lit.value == 3.14);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_string_literal) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *expr = parse_expr("\"hello\"", a, c);
    AGL_ASSERT_FATAL(ctx, expr != NULL);
    AGL_ASSERT_INT_EQ(ctx, expr->kind, AGL_NODE_STRING_LIT);
    /* value points to inside quotes: "hello" -> starts at ", length 7 */
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_bool_literals) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();

    AglNode *t = parse_expr("true", a, c);
    AGL_ASSERT_FATAL(ctx, t != NULL);
    AGL_ASSERT_INT_EQ(ctx, t->kind, AGL_NODE_BOOL_LIT);
    AGL_ASSERT(ctx, t->as.bool_lit.value == true);

    AglNode *f = parse_expr("false", a, c);
    AGL_ASSERT_FATAL(ctx, f != NULL);
    AGL_ASSERT_INT_EQ(ctx, f->kind, AGL_NODE_BOOL_LIT);
    AGL_ASSERT(ctx, f->as.bool_lit.value == false);

    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_identifier) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *expr = parse_expr("foo", a, c);
    AGL_ASSERT_FATAL(ctx, expr != NULL);
    AGL_ASSERT_INT_EQ(ctx, expr->kind, AGL_NODE_IDENT);
    AGL_ASSERT(ctx, memcmp(expr->as.ident.name, "foo", 3) == 0);
    agl_ctx_free(c);
    agl_arena_free(a);
}

/* ---- Binary expressions ---- */

AGL_TEST(test_parse_addition) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *expr = parse_expr("1 + 2", a, c);
    AGL_ASSERT_FATAL(ctx, expr != NULL);
    AGL_ASSERT_INT_EQ(ctx, expr->kind, AGL_NODE_BINARY);
    AGL_ASSERT_INT_EQ(ctx, expr->as.binary.op, AGL_TOKEN_PLUS);
    AGL_ASSERT_INT_EQ(ctx, expr->as.binary.left->kind, AGL_NODE_INT_LIT);
    AGL_ASSERT_INT_EQ(ctx, expr->as.binary.right->kind, AGL_NODE_INT_LIT);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_precedence) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    /* 1 + 2 * 3 should parse as 1 + (2 * 3) */
    AglNode *expr = parse_expr("1 + 2 * 3", a, c);
    AGL_ASSERT_FATAL(ctx, expr != NULL);
    AGL_ASSERT_INT_EQ(ctx, expr->kind, AGL_NODE_BINARY);
    AGL_ASSERT_INT_EQ(ctx, expr->as.binary.op, AGL_TOKEN_PLUS);
    AGL_ASSERT_INT_EQ(ctx, expr->as.binary.left->kind, AGL_NODE_INT_LIT);
    /* right should be (2 * 3) */
    AglNode *right = expr->as.binary.right;
    AGL_ASSERT_INT_EQ(ctx, right->kind, AGL_NODE_BINARY);
    AGL_ASSERT_INT_EQ(ctx, right->as.binary.op, AGL_TOKEN_STAR);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_comparison) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *expr = parse_expr("x > 0", a, c);
    AGL_ASSERT_FATAL(ctx, expr != NULL);
    AGL_ASSERT_INT_EQ(ctx, expr->kind, AGL_NODE_BINARY);
    AGL_ASSERT_INT_EQ(ctx, expr->as.binary.op, AGL_TOKEN_GT);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_logical) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    /* a && b || c  ->  (a && b) || c */
    AglNode *expr = parse_expr("a && b || c", a, c);
    AGL_ASSERT_FATAL(ctx, expr != NULL);
    AGL_ASSERT_INT_EQ(ctx, expr->kind, AGL_NODE_BINARY);
    AGL_ASSERT_INT_EQ(ctx, expr->as.binary.op, AGL_TOKEN_OR);
    AGL_ASSERT_INT_EQ(ctx, expr->as.binary.left->kind, AGL_NODE_BINARY);
    AGL_ASSERT_INT_EQ(ctx, expr->as.binary.left->as.binary.op, AGL_TOKEN_AND);
    agl_ctx_free(c);
    agl_arena_free(a);
}

/* ---- Unary expressions ---- */

AGL_TEST(test_parse_unary_not) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *expr = parse_expr("!x", a, c);
    AGL_ASSERT_FATAL(ctx, expr != NULL);
    AGL_ASSERT_INT_EQ(ctx, expr->kind, AGL_NODE_UNARY);
    AGL_ASSERT_INT_EQ(ctx, expr->as.unary.op, AGL_TOKEN_NOT);
    AGL_ASSERT_INT_EQ(ctx, expr->as.unary.operand->kind, AGL_NODE_IDENT);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_unary_negate) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *expr = parse_expr("-42", a, c);
    AGL_ASSERT_FATAL(ctx, expr != NULL);
    AGL_ASSERT_INT_EQ(ctx, expr->kind, AGL_NODE_UNARY);
    AGL_ASSERT_INT_EQ(ctx, expr->as.unary.op, AGL_TOKEN_MINUS);
    AGL_ASSERT_INT_EQ(ctx, expr->as.unary.operand->kind, AGL_NODE_INT_LIT);
    agl_ctx_free(c);
    agl_arena_free(a);
}

/* ---- Function call ---- */

AGL_TEST(test_parse_call_no_args) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *expr = parse_expr("foo()", a, c);
    AGL_ASSERT_FATAL(ctx, expr != NULL);
    AGL_ASSERT_INT_EQ(ctx, expr->kind, AGL_NODE_CALL);
    AGL_ASSERT_INT_EQ(ctx, expr->as.call.callee->kind, AGL_NODE_IDENT);
    AGL_ASSERT_INT_EQ(ctx, expr->as.call.arg_count, 0);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_call_with_args) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *expr = parse_expr("add(1, 2)", a, c);
    AGL_ASSERT_FATAL(ctx, expr != NULL);
    AGL_ASSERT_INT_EQ(ctx, expr->kind, AGL_NODE_CALL);
    AGL_ASSERT_INT_EQ(ctx, expr->as.call.arg_count, 2);
    AGL_ASSERT_INT_EQ(ctx, expr->as.call.args[0]->kind, AGL_NODE_INT_LIT);
    AGL_ASSERT_INT_EQ(ctx, expr->as.call.args[1]->kind, AGL_NODE_INT_LIT);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_call_string_arg) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *expr = parse_expr("print(\"hello world\")", a, c);
    AGL_ASSERT_FATAL(ctx, expr != NULL);
    AGL_ASSERT_INT_EQ(ctx, expr->kind, AGL_NODE_CALL);
    AGL_ASSERT_INT_EQ(ctx, expr->as.call.arg_count, 1);
    AGL_ASSERT_INT_EQ(ctx, expr->as.call.args[0]->kind, AGL_NODE_STRING_LIT);
    agl_ctx_free(c);
    agl_arena_free(a);
}

/* ---- Statements ---- */

AGL_TEST(test_parse_let_stmt) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *prog = parse("let x = 42", a, c);
    AGL_ASSERT_FATAL(ctx, prog != NULL);
    AGL_ASSERT_INT_EQ(ctx, prog->kind, AGL_NODE_PROGRAM);
    AGL_ASSERT_INT_EQ(ctx, prog->as.program.decl_count, 1);
    AglNode *stmt = prog->as.program.decls[0];
    AGL_ASSERT_INT_EQ(ctx, stmt->kind, AGL_NODE_LET_STMT);
    AGL_ASSERT(ctx, memcmp(stmt->as.var_decl.name, "x", 1) == 0);
    AGL_ASSERT_FATAL(ctx, stmt->as.var_decl.initializer != NULL);
    AGL_ASSERT_INT_EQ(ctx, stmt->as.var_decl.initializer->kind, AGL_NODE_INT_LIT);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_let_with_type) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *prog = parse("let name: string = \"ago\"", a, c);
    AGL_ASSERT_FATAL(ctx, prog != NULL);
    AglNode *stmt = prog->as.program.decls[0];
    AGL_ASSERT_INT_EQ(ctx, stmt->kind, AGL_NODE_LET_STMT);
    AGL_ASSERT_FATAL(ctx, stmt->as.var_decl.type_name != NULL);
    AGL_ASSERT(ctx, memcmp(stmt->as.var_decl.type_name, "string", 6) == 0);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_var_stmt) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *prog = parse("var count = 0", a, c);
    AGL_ASSERT_FATAL(ctx, prog != NULL);
    AglNode *stmt = prog->as.program.decls[0];
    AGL_ASSERT_INT_EQ(ctx, stmt->kind, AGL_NODE_VAR_STMT);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_expr_stmt) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *prog = parse("print(\"hello\")", a, c);
    AGL_ASSERT_FATAL(ctx, prog != NULL);
    AGL_ASSERT_INT_EQ(ctx, prog->as.program.decl_count, 1);
    AglNode *stmt = prog->as.program.decls[0];
    AGL_ASSERT_INT_EQ(ctx, stmt->kind, AGL_NODE_EXPR_STMT);
    AGL_ASSERT_INT_EQ(ctx, stmt->as.expr_stmt.expr->kind, AGL_NODE_CALL);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_return_stmt) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    /* parse as statement list inside a block context */
    AglNode *prog = parse("return 42", a, c);
    AGL_ASSERT_FATAL(ctx, prog != NULL);
    AglNode *stmt = prog->as.program.decls[0];
    AGL_ASSERT_INT_EQ(ctx, stmt->kind, AGL_NODE_RETURN_STMT);
    AGL_ASSERT_FATAL(ctx, stmt->as.return_stmt.value != NULL);
    AGL_ASSERT_INT_EQ(ctx, stmt->as.return_stmt.value->kind, AGL_NODE_INT_LIT);
    agl_ctx_free(c);
    agl_arena_free(a);
}

/* ---- If statement ---- */

AGL_TEST(test_parse_if_stmt) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *prog = parse("if x > 0 {\n    print(\"yes\")\n}", a, c);
    AGL_ASSERT_FATAL(ctx, prog != NULL);
    AglNode *stmt = prog->as.program.decls[0];
    AGL_ASSERT_INT_EQ(ctx, stmt->kind, AGL_NODE_IF_STMT);
    AGL_ASSERT_INT_EQ(ctx, stmt->as.if_stmt.condition->kind, AGL_NODE_BINARY);
    AGL_ASSERT_FATAL(ctx, stmt->as.if_stmt.then_block != NULL);
    AGL_ASSERT(ctx, stmt->as.if_stmt.else_block == NULL);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_if_else) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *prog = parse("if x > 0 {\n    print(\"yes\")\n} else {\n    print(\"no\")\n}", a, c);
    AGL_ASSERT_FATAL(ctx, prog != NULL);
    AglNode *stmt = prog->as.program.decls[0];
    AGL_ASSERT_INT_EQ(ctx, stmt->kind, AGL_NODE_IF_STMT);
    AGL_ASSERT_FATAL(ctx, stmt->as.if_stmt.else_block != NULL);
    agl_ctx_free(c);
    agl_arena_free(a);
}

/* ---- While statement ---- */

AGL_TEST(test_parse_while_stmt) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *prog = parse("while x > 0 {\n    print(x)\n}", a, c);
    AGL_ASSERT_FATAL(ctx, prog != NULL);
    AglNode *stmt = prog->as.program.decls[0];
    AGL_ASSERT_INT_EQ(ctx, stmt->kind, AGL_NODE_WHILE_STMT);
    AGL_ASSERT_INT_EQ(ctx, stmt->as.while_stmt.condition->kind, AGL_NODE_BINARY);
    AGL_ASSERT_FATAL(ctx, stmt->as.while_stmt.body != NULL);
    agl_ctx_free(c);
    agl_arena_free(a);
}

/* ---- Function declaration ---- */

AGL_TEST(test_parse_fn_decl) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *prog = parse("fn add(a: int, b: int) -> int {\n    return a + b\n}", a, c);
    AGL_ASSERT_FATAL(ctx, prog != NULL);
    AglNode *decl = prog->as.program.decls[0];
    AGL_ASSERT_INT_EQ(ctx, decl->kind, AGL_NODE_FN_DECL);
    AGL_ASSERT(ctx, memcmp(decl->as.fn_decl.name, "add", 3) == 0);
    AGL_ASSERT_INT_EQ(ctx, decl->as.fn_decl.param_count, 2);
    AGL_ASSERT_FATAL(ctx, decl->as.fn_decl.return_type != NULL);
    AGL_ASSERT(ctx, memcmp(decl->as.fn_decl.return_type, "int", 3) == 0);
    AGL_ASSERT_FATAL(ctx, decl->as.fn_decl.body != NULL);
    AGL_ASSERT_INT_EQ(ctx, decl->as.fn_decl.body->kind, AGL_NODE_BLOCK);
    agl_ctx_free(c);
    agl_arena_free(a);
}

AGL_TEST(test_parse_fn_no_return_type) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *prog = parse("fn greet() {\n    print(\"hi\")\n}", a, c);
    AGL_ASSERT_FATAL(ctx, prog != NULL);
    AglNode *decl = prog->as.program.decls[0];
    AGL_ASSERT_INT_EQ(ctx, decl->kind, AGL_NODE_FN_DECL);
    AGL_ASSERT_INT_EQ(ctx, decl->as.fn_decl.param_count, 0);
    AGL_ASSERT(ctx, decl->as.fn_decl.return_type == NULL);
    agl_ctx_free(c);
    agl_arena_free(a);
}

/* ---- Multiple statements ---- */

AGL_TEST(test_parse_multiple_stmts) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *prog = parse("let x = 1\nlet y = 2\nprint(x)", a, c);
    AGL_ASSERT_FATAL(ctx, prog != NULL);
    AGL_ASSERT_INT_EQ(ctx, prog->as.program.decl_count, 3);
    AGL_ASSERT_INT_EQ(ctx, prog->as.program.decls[0]->kind, AGL_NODE_LET_STMT);
    AGL_ASSERT_INT_EQ(ctx, prog->as.program.decls[1]->kind, AGL_NODE_LET_STMT);
    AGL_ASSERT_INT_EQ(ctx, prog->as.program.decls[2]->kind, AGL_NODE_EXPR_STMT);
    agl_ctx_free(c);
    agl_arena_free(a);
}

/* ---- Hello world program ---- */

AGL_TEST(test_parse_hello_world) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *prog = parse("print(\"hello world\")", a, c);
    AGL_ASSERT_FATAL(ctx, prog != NULL);
    AGL_ASSERT(ctx, !agl_error_occurred(c));
    AGL_ASSERT_INT_EQ(ctx, prog->as.program.decl_count, 1);

    AglNode *stmt = prog->as.program.decls[0];
    AGL_ASSERT_INT_EQ(ctx, stmt->kind, AGL_NODE_EXPR_STMT);

    AglNode *call = stmt->as.expr_stmt.expr;
    AGL_ASSERT_INT_EQ(ctx, call->kind, AGL_NODE_CALL);
    AGL_ASSERT_INT_EQ(ctx, call->as.call.callee->kind, AGL_NODE_IDENT);
    AGL_ASSERT(ctx, memcmp(call->as.call.callee->as.ident.name, "print", 5) == 0);
    AGL_ASSERT_INT_EQ(ctx, call->as.call.arg_count, 1);
    AGL_ASSERT_INT_EQ(ctx, call->as.call.args[0]->kind, AGL_NODE_STRING_LIT);

    agl_ctx_free(c);
    agl_arena_free(a);
}

/* ---- Error handling ---- */

AGL_TEST(test_parse_error_missing_rparen) {
    AglArena *a = agl_arena_new();
    AglCtx *c = agl_ctx_new();
    AglNode *prog = parse("foo(1, 2", a, c);
    AGL_ASSERT(ctx, agl_error_occurred(c));
    (void)prog;
    agl_ctx_free(c);
    agl_arena_free(a);
}

/* ---- Main ---- */

int main(void) {
    AglTestCtx ctx = {0, 0};

    printf("=== Parser Tests ===\n");

    /* Literals */
    AGL_RUN_TEST(&ctx, test_parse_int_literal);
    AGL_RUN_TEST(&ctx, test_parse_float_literal);
    AGL_RUN_TEST(&ctx, test_parse_string_literal);
    AGL_RUN_TEST(&ctx, test_parse_bool_literals);
    AGL_RUN_TEST(&ctx, test_parse_identifier);

    /* Binary expressions */
    AGL_RUN_TEST(&ctx, test_parse_addition);
    AGL_RUN_TEST(&ctx, test_parse_precedence);
    AGL_RUN_TEST(&ctx, test_parse_comparison);
    AGL_RUN_TEST(&ctx, test_parse_logical);

    /* Unary expressions */
    AGL_RUN_TEST(&ctx, test_parse_unary_not);
    AGL_RUN_TEST(&ctx, test_parse_unary_negate);

    /* Function calls */
    AGL_RUN_TEST(&ctx, test_parse_call_no_args);
    AGL_RUN_TEST(&ctx, test_parse_call_with_args);
    AGL_RUN_TEST(&ctx, test_parse_call_string_arg);

    /* Statements */
    AGL_RUN_TEST(&ctx, test_parse_let_stmt);
    AGL_RUN_TEST(&ctx, test_parse_let_with_type);
    AGL_RUN_TEST(&ctx, test_parse_var_stmt);
    AGL_RUN_TEST(&ctx, test_parse_expr_stmt);
    AGL_RUN_TEST(&ctx, test_parse_return_stmt);

    /* Control flow */
    AGL_RUN_TEST(&ctx, test_parse_if_stmt);
    AGL_RUN_TEST(&ctx, test_parse_if_else);
    AGL_RUN_TEST(&ctx, test_parse_while_stmt);

    /* Declarations */
    AGL_RUN_TEST(&ctx, test_parse_fn_decl);
    AGL_RUN_TEST(&ctx, test_parse_fn_no_return_type);

    /* Integration */
    AGL_RUN_TEST(&ctx, test_parse_multiple_stmts);
    AGL_RUN_TEST(&ctx, test_parse_hello_world);

    /* Errors */
    AGL_RUN_TEST(&ctx, test_parse_error_missing_rparen);

    AGL_SUMMARY(&ctx);
}
