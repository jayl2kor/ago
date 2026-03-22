#include "test_harness.h"
#include "../src/parser.h"

/* ---- Helper: parse source and return program node ---- */

static AgoNode *parse(const char *source, AgoArena *arena, AgoCtx *c) {
    AgoParser parser;
    ago_parser_init(&parser, source, "test.ago", arena, c);
    return ago_parser_parse(&parser);
}

static AgoNode *parse_expr(const char *source, AgoArena *arena, AgoCtx *c) {
    AgoParser parser;
    ago_parser_init(&parser, source, "test.ago", arena, c);
    return ago_parser_parse_expression(&parser);
}

/* ================================================================
 *  RED PHASE: These tests define the expected behavior.
 *  They will fail until the parser is implemented.
 * ================================================================ */

/* ---- Literal expressions ---- */

AGO_TEST(test_parse_int_literal) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *expr = parse_expr("42", a, c);
    AGO_ASSERT_FATAL(ctx, expr != NULL);
    AGO_ASSERT_INT_EQ(ctx, expr->kind, AGO_NODE_INT_LIT);
    AGO_ASSERT(ctx, expr->as.int_lit.value == 42);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_float_literal) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *expr = parse_expr("3.14", a, c);
    AGO_ASSERT_FATAL(ctx, expr != NULL);
    AGO_ASSERT_INT_EQ(ctx, expr->kind, AGO_NODE_FLOAT_LIT);
    AGO_ASSERT(ctx, expr->as.float_lit.value == 3.14);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_string_literal) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *expr = parse_expr("\"hello\"", a, c);
    AGO_ASSERT_FATAL(ctx, expr != NULL);
    AGO_ASSERT_INT_EQ(ctx, expr->kind, AGO_NODE_STRING_LIT);
    /* value points to inside quotes: "hello" -> starts at ", length 7 */
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_bool_literals) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();

    AgoNode *t = parse_expr("true", a, c);
    AGO_ASSERT_FATAL(ctx, t != NULL);
    AGO_ASSERT_INT_EQ(ctx, t->kind, AGO_NODE_BOOL_LIT);
    AGO_ASSERT(ctx, t->as.bool_lit.value == true);

    AgoNode *f = parse_expr("false", a, c);
    AGO_ASSERT_FATAL(ctx, f != NULL);
    AGO_ASSERT_INT_EQ(ctx, f->kind, AGO_NODE_BOOL_LIT);
    AGO_ASSERT(ctx, f->as.bool_lit.value == false);

    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_identifier) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *expr = parse_expr("foo", a, c);
    AGO_ASSERT_FATAL(ctx, expr != NULL);
    AGO_ASSERT_INT_EQ(ctx, expr->kind, AGO_NODE_IDENT);
    AGO_ASSERT(ctx, memcmp(expr->as.ident.name, "foo", 3) == 0);
    ago_ctx_free(c);
    ago_arena_free(a);
}

/* ---- Binary expressions ---- */

AGO_TEST(test_parse_addition) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *expr = parse_expr("1 + 2", a, c);
    AGO_ASSERT_FATAL(ctx, expr != NULL);
    AGO_ASSERT_INT_EQ(ctx, expr->kind, AGO_NODE_BINARY);
    AGO_ASSERT_INT_EQ(ctx, expr->as.binary.op, AGO_TOKEN_PLUS);
    AGO_ASSERT_INT_EQ(ctx, expr->as.binary.left->kind, AGO_NODE_INT_LIT);
    AGO_ASSERT_INT_EQ(ctx, expr->as.binary.right->kind, AGO_NODE_INT_LIT);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_precedence) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    /* 1 + 2 * 3 should parse as 1 + (2 * 3) */
    AgoNode *expr = parse_expr("1 + 2 * 3", a, c);
    AGO_ASSERT_FATAL(ctx, expr != NULL);
    AGO_ASSERT_INT_EQ(ctx, expr->kind, AGO_NODE_BINARY);
    AGO_ASSERT_INT_EQ(ctx, expr->as.binary.op, AGO_TOKEN_PLUS);
    AGO_ASSERT_INT_EQ(ctx, expr->as.binary.left->kind, AGO_NODE_INT_LIT);
    /* right should be (2 * 3) */
    AgoNode *right = expr->as.binary.right;
    AGO_ASSERT_INT_EQ(ctx, right->kind, AGO_NODE_BINARY);
    AGO_ASSERT_INT_EQ(ctx, right->as.binary.op, AGO_TOKEN_STAR);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_comparison) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *expr = parse_expr("x > 0", a, c);
    AGO_ASSERT_FATAL(ctx, expr != NULL);
    AGO_ASSERT_INT_EQ(ctx, expr->kind, AGO_NODE_BINARY);
    AGO_ASSERT_INT_EQ(ctx, expr->as.binary.op, AGO_TOKEN_GT);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_logical) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    /* a && b || c  ->  (a && b) || c */
    AgoNode *expr = parse_expr("a && b || c", a, c);
    AGO_ASSERT_FATAL(ctx, expr != NULL);
    AGO_ASSERT_INT_EQ(ctx, expr->kind, AGO_NODE_BINARY);
    AGO_ASSERT_INT_EQ(ctx, expr->as.binary.op, AGO_TOKEN_OR);
    AGO_ASSERT_INT_EQ(ctx, expr->as.binary.left->kind, AGO_NODE_BINARY);
    AGO_ASSERT_INT_EQ(ctx, expr->as.binary.left->as.binary.op, AGO_TOKEN_AND);
    ago_ctx_free(c);
    ago_arena_free(a);
}

/* ---- Unary expressions ---- */

AGO_TEST(test_parse_unary_not) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *expr = parse_expr("!x", a, c);
    AGO_ASSERT_FATAL(ctx, expr != NULL);
    AGO_ASSERT_INT_EQ(ctx, expr->kind, AGO_NODE_UNARY);
    AGO_ASSERT_INT_EQ(ctx, expr->as.unary.op, AGO_TOKEN_NOT);
    AGO_ASSERT_INT_EQ(ctx, expr->as.unary.operand->kind, AGO_NODE_IDENT);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_unary_negate) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *expr = parse_expr("-42", a, c);
    AGO_ASSERT_FATAL(ctx, expr != NULL);
    AGO_ASSERT_INT_EQ(ctx, expr->kind, AGO_NODE_UNARY);
    AGO_ASSERT_INT_EQ(ctx, expr->as.unary.op, AGO_TOKEN_MINUS);
    AGO_ASSERT_INT_EQ(ctx, expr->as.unary.operand->kind, AGO_NODE_INT_LIT);
    ago_ctx_free(c);
    ago_arena_free(a);
}

/* ---- Function call ---- */

AGO_TEST(test_parse_call_no_args) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *expr = parse_expr("foo()", a, c);
    AGO_ASSERT_FATAL(ctx, expr != NULL);
    AGO_ASSERT_INT_EQ(ctx, expr->kind, AGO_NODE_CALL);
    AGO_ASSERT_INT_EQ(ctx, expr->as.call.callee->kind, AGO_NODE_IDENT);
    AGO_ASSERT_INT_EQ(ctx, expr->as.call.arg_count, 0);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_call_with_args) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *expr = parse_expr("add(1, 2)", a, c);
    AGO_ASSERT_FATAL(ctx, expr != NULL);
    AGO_ASSERT_INT_EQ(ctx, expr->kind, AGO_NODE_CALL);
    AGO_ASSERT_INT_EQ(ctx, expr->as.call.arg_count, 2);
    AGO_ASSERT_INT_EQ(ctx, expr->as.call.args[0]->kind, AGO_NODE_INT_LIT);
    AGO_ASSERT_INT_EQ(ctx, expr->as.call.args[1]->kind, AGO_NODE_INT_LIT);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_call_string_arg) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *expr = parse_expr("print(\"hello world\")", a, c);
    AGO_ASSERT_FATAL(ctx, expr != NULL);
    AGO_ASSERT_INT_EQ(ctx, expr->kind, AGO_NODE_CALL);
    AGO_ASSERT_INT_EQ(ctx, expr->as.call.arg_count, 1);
    AGO_ASSERT_INT_EQ(ctx, expr->as.call.args[0]->kind, AGO_NODE_STRING_LIT);
    ago_ctx_free(c);
    ago_arena_free(a);
}

/* ---- Statements ---- */

AGO_TEST(test_parse_let_stmt) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *prog = parse("let x = 42", a, c);
    AGO_ASSERT_FATAL(ctx, prog != NULL);
    AGO_ASSERT_INT_EQ(ctx, prog->kind, AGO_NODE_PROGRAM);
    AGO_ASSERT_INT_EQ(ctx, prog->as.program.decl_count, 1);
    AgoNode *stmt = prog->as.program.decls[0];
    AGO_ASSERT_INT_EQ(ctx, stmt->kind, AGO_NODE_LET_STMT);
    AGO_ASSERT(ctx, memcmp(stmt->as.var_decl.name, "x", 1) == 0);
    AGO_ASSERT_FATAL(ctx, stmt->as.var_decl.initializer != NULL);
    AGO_ASSERT_INT_EQ(ctx, stmt->as.var_decl.initializer->kind, AGO_NODE_INT_LIT);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_let_with_type) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *prog = parse("let name: string = \"ago\"", a, c);
    AGO_ASSERT_FATAL(ctx, prog != NULL);
    AgoNode *stmt = prog->as.program.decls[0];
    AGO_ASSERT_INT_EQ(ctx, stmt->kind, AGO_NODE_LET_STMT);
    AGO_ASSERT_FATAL(ctx, stmt->as.var_decl.type_name != NULL);
    AGO_ASSERT(ctx, memcmp(stmt->as.var_decl.type_name, "string", 6) == 0);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_var_stmt) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *prog = parse("var count = 0", a, c);
    AGO_ASSERT_FATAL(ctx, prog != NULL);
    AgoNode *stmt = prog->as.program.decls[0];
    AGO_ASSERT_INT_EQ(ctx, stmt->kind, AGO_NODE_VAR_STMT);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_expr_stmt) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *prog = parse("print(\"hello\")", a, c);
    AGO_ASSERT_FATAL(ctx, prog != NULL);
    AGO_ASSERT_INT_EQ(ctx, prog->as.program.decl_count, 1);
    AgoNode *stmt = prog->as.program.decls[0];
    AGO_ASSERT_INT_EQ(ctx, stmt->kind, AGO_NODE_EXPR_STMT);
    AGO_ASSERT_INT_EQ(ctx, stmt->as.expr_stmt.expr->kind, AGO_NODE_CALL);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_return_stmt) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    /* parse as statement list inside a block context */
    AgoNode *prog = parse("return 42", a, c);
    AGO_ASSERT_FATAL(ctx, prog != NULL);
    AgoNode *stmt = prog->as.program.decls[0];
    AGO_ASSERT_INT_EQ(ctx, stmt->kind, AGO_NODE_RETURN_STMT);
    AGO_ASSERT_FATAL(ctx, stmt->as.return_stmt.value != NULL);
    AGO_ASSERT_INT_EQ(ctx, stmt->as.return_stmt.value->kind, AGO_NODE_INT_LIT);
    ago_ctx_free(c);
    ago_arena_free(a);
}

/* ---- If statement ---- */

AGO_TEST(test_parse_if_stmt) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *prog = parse("if x > 0 {\n    print(\"yes\")\n}", a, c);
    AGO_ASSERT_FATAL(ctx, prog != NULL);
    AgoNode *stmt = prog->as.program.decls[0];
    AGO_ASSERT_INT_EQ(ctx, stmt->kind, AGO_NODE_IF_STMT);
    AGO_ASSERT_INT_EQ(ctx, stmt->as.if_stmt.condition->kind, AGO_NODE_BINARY);
    AGO_ASSERT_FATAL(ctx, stmt->as.if_stmt.then_block != NULL);
    AGO_ASSERT(ctx, stmt->as.if_stmt.else_block == NULL);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_if_else) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *prog = parse("if x > 0 {\n    print(\"yes\")\n} else {\n    print(\"no\")\n}", a, c);
    AGO_ASSERT_FATAL(ctx, prog != NULL);
    AgoNode *stmt = prog->as.program.decls[0];
    AGO_ASSERT_INT_EQ(ctx, stmt->kind, AGO_NODE_IF_STMT);
    AGO_ASSERT_FATAL(ctx, stmt->as.if_stmt.else_block != NULL);
    ago_ctx_free(c);
    ago_arena_free(a);
}

/* ---- While statement ---- */

AGO_TEST(test_parse_while_stmt) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *prog = parse("while x > 0 {\n    print(x)\n}", a, c);
    AGO_ASSERT_FATAL(ctx, prog != NULL);
    AgoNode *stmt = prog->as.program.decls[0];
    AGO_ASSERT_INT_EQ(ctx, stmt->kind, AGO_NODE_WHILE_STMT);
    AGO_ASSERT_INT_EQ(ctx, stmt->as.while_stmt.condition->kind, AGO_NODE_BINARY);
    AGO_ASSERT_FATAL(ctx, stmt->as.while_stmt.body != NULL);
    ago_ctx_free(c);
    ago_arena_free(a);
}

/* ---- Function declaration ---- */

AGO_TEST(test_parse_fn_decl) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *prog = parse("fn add(a: int, b: int) -> int {\n    return a + b\n}", a, c);
    AGO_ASSERT_FATAL(ctx, prog != NULL);
    AgoNode *decl = prog->as.program.decls[0];
    AGO_ASSERT_INT_EQ(ctx, decl->kind, AGO_NODE_FN_DECL);
    AGO_ASSERT(ctx, memcmp(decl->as.fn_decl.name, "add", 3) == 0);
    AGO_ASSERT_INT_EQ(ctx, decl->as.fn_decl.param_count, 2);
    AGO_ASSERT_FATAL(ctx, decl->as.fn_decl.return_type != NULL);
    AGO_ASSERT(ctx, memcmp(decl->as.fn_decl.return_type, "int", 3) == 0);
    AGO_ASSERT_FATAL(ctx, decl->as.fn_decl.body != NULL);
    AGO_ASSERT_INT_EQ(ctx, decl->as.fn_decl.body->kind, AGO_NODE_BLOCK);
    ago_ctx_free(c);
    ago_arena_free(a);
}

AGO_TEST(test_parse_fn_no_return_type) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *prog = parse("fn greet() {\n    print(\"hi\")\n}", a, c);
    AGO_ASSERT_FATAL(ctx, prog != NULL);
    AgoNode *decl = prog->as.program.decls[0];
    AGO_ASSERT_INT_EQ(ctx, decl->kind, AGO_NODE_FN_DECL);
    AGO_ASSERT_INT_EQ(ctx, decl->as.fn_decl.param_count, 0);
    AGO_ASSERT(ctx, decl->as.fn_decl.return_type == NULL);
    ago_ctx_free(c);
    ago_arena_free(a);
}

/* ---- Multiple statements ---- */

AGO_TEST(test_parse_multiple_stmts) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *prog = parse("let x = 1\nlet y = 2\nprint(x)", a, c);
    AGO_ASSERT_FATAL(ctx, prog != NULL);
    AGO_ASSERT_INT_EQ(ctx, prog->as.program.decl_count, 3);
    AGO_ASSERT_INT_EQ(ctx, prog->as.program.decls[0]->kind, AGO_NODE_LET_STMT);
    AGO_ASSERT_INT_EQ(ctx, prog->as.program.decls[1]->kind, AGO_NODE_LET_STMT);
    AGO_ASSERT_INT_EQ(ctx, prog->as.program.decls[2]->kind, AGO_NODE_EXPR_STMT);
    ago_ctx_free(c);
    ago_arena_free(a);
}

/* ---- Hello world program ---- */

AGO_TEST(test_parse_hello_world) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *prog = parse("print(\"hello world\")", a, c);
    AGO_ASSERT_FATAL(ctx, prog != NULL);
    AGO_ASSERT(ctx, !ago_error_occurred(c));
    AGO_ASSERT_INT_EQ(ctx, prog->as.program.decl_count, 1);

    AgoNode *stmt = prog->as.program.decls[0];
    AGO_ASSERT_INT_EQ(ctx, stmt->kind, AGO_NODE_EXPR_STMT);

    AgoNode *call = stmt->as.expr_stmt.expr;
    AGO_ASSERT_INT_EQ(ctx, call->kind, AGO_NODE_CALL);
    AGO_ASSERT_INT_EQ(ctx, call->as.call.callee->kind, AGO_NODE_IDENT);
    AGO_ASSERT(ctx, memcmp(call->as.call.callee->as.ident.name, "print", 5) == 0);
    AGO_ASSERT_INT_EQ(ctx, call->as.call.arg_count, 1);
    AGO_ASSERT_INT_EQ(ctx, call->as.call.args[0]->kind, AGO_NODE_STRING_LIT);

    ago_ctx_free(c);
    ago_arena_free(a);
}

/* ---- Error handling ---- */

AGO_TEST(test_parse_error_missing_rparen) {
    AgoArena *a = ago_arena_new();
    AgoCtx *c = ago_ctx_new();
    AgoNode *prog = parse("foo(1, 2", a, c);
    AGO_ASSERT(ctx, ago_error_occurred(c));
    (void)prog;
    ago_ctx_free(c);
    ago_arena_free(a);
}

/* ---- Main ---- */

int main(void) {
    AgoTestCtx ctx = {0, 0};

    printf("=== Parser Tests ===\n");

    /* Literals */
    AGO_RUN_TEST(&ctx, test_parse_int_literal);
    AGO_RUN_TEST(&ctx, test_parse_float_literal);
    AGO_RUN_TEST(&ctx, test_parse_string_literal);
    AGO_RUN_TEST(&ctx, test_parse_bool_literals);
    AGO_RUN_TEST(&ctx, test_parse_identifier);

    /* Binary expressions */
    AGO_RUN_TEST(&ctx, test_parse_addition);
    AGO_RUN_TEST(&ctx, test_parse_precedence);
    AGO_RUN_TEST(&ctx, test_parse_comparison);
    AGO_RUN_TEST(&ctx, test_parse_logical);

    /* Unary expressions */
    AGO_RUN_TEST(&ctx, test_parse_unary_not);
    AGO_RUN_TEST(&ctx, test_parse_unary_negate);

    /* Function calls */
    AGO_RUN_TEST(&ctx, test_parse_call_no_args);
    AGO_RUN_TEST(&ctx, test_parse_call_with_args);
    AGO_RUN_TEST(&ctx, test_parse_call_string_arg);

    /* Statements */
    AGO_RUN_TEST(&ctx, test_parse_let_stmt);
    AGO_RUN_TEST(&ctx, test_parse_let_with_type);
    AGO_RUN_TEST(&ctx, test_parse_var_stmt);
    AGO_RUN_TEST(&ctx, test_parse_expr_stmt);
    AGO_RUN_TEST(&ctx, test_parse_return_stmt);

    /* Control flow */
    AGO_RUN_TEST(&ctx, test_parse_if_stmt);
    AGO_RUN_TEST(&ctx, test_parse_if_else);
    AGO_RUN_TEST(&ctx, test_parse_while_stmt);

    /* Declarations */
    AGO_RUN_TEST(&ctx, test_parse_fn_decl);
    AGO_RUN_TEST(&ctx, test_parse_fn_no_return_type);

    /* Integration */
    AGO_RUN_TEST(&ctx, test_parse_multiple_stmts);
    AGO_RUN_TEST(&ctx, test_parse_hello_world);

    /* Errors */
    AGO_RUN_TEST(&ctx, test_parse_error_missing_rparen);

    AGO_SUMMARY(&ctx);
}
