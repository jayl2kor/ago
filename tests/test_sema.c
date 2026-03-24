#include "test_harness.h"
#include "../src/sema.h"
#include "../src/parser.h"
#include "../src/arena.h"

/* ---- Helper: parse + sema check ---- */

static bool sema_ok(const char *source) {
    AglCtx *ctx = agl_ctx_new();
    AglArena *arena = agl_arena_new();
    AglParser parser;
    agl_parser_init(&parser, source, "test.ago", arena, ctx);
    AglNode *program = agl_parser_parse(&parser);
    bool result = false;
    if (program && !agl_error_occurred(ctx)) {
        AglSema *sema = agl_sema_new(ctx, arena);
        result = agl_sema_check(sema, program);
        agl_sema_free(sema);
    }
    if (agl_error_occurred(ctx)) {
        agl_error_print(agl_error_get(ctx));
    }
    agl_arena_free(arena);
    agl_ctx_free(ctx);
    return result;
}

/* ---- Valid programs pass ---- */

AGL_TEST(test_sema_hello_world) {
    AGL_ASSERT(ctx, sema_ok("print(\"hello\")"));
}

AGL_TEST(test_sema_let_binding) {
    AGL_ASSERT(ctx, sema_ok("let x = 42\nprint(x)"));
}

AGL_TEST(test_sema_var_reassign) {
    AGL_ASSERT(ctx, sema_ok("var x = 1\nx = 10\nprint(x)"));
}

AGL_TEST(test_sema_fn_decl_and_call) {
    AGL_ASSERT(ctx, sema_ok(
        "fn add(a: int, b: int) -> int { return a + b }\n"
        "print(add(1, 2))"));
}

AGL_TEST(test_sema_if_else) {
    AGL_ASSERT(ctx, sema_ok(
        "let x = 5\n"
        "if x > 3 {\n"
        "    print(x)\n"
        "} else {\n"
        "    print(0)\n"
        "}"));
}

AGL_TEST(test_sema_while) {
    AGL_ASSERT(ctx, sema_ok(
        "var i = 0\n"
        "while i < 5 {\n"
        "    i = i + 1\n"
        "}"));
}

AGL_TEST(test_sema_for_in) {
    AGL_ASSERT(ctx, sema_ok(
        "let arr = [1, 2, 3]\n"
        "for x in arr {\n"
        "    print(x)\n"
        "}"));
}

AGL_TEST(test_sema_lambda) {
    AGL_ASSERT(ctx, sema_ok(
        "let f = fn(x: int) -> int { return x * 2 }\n"
        "print(f(5))"));
}

AGL_TEST(test_sema_result_match) {
    AGL_ASSERT(ctx, sema_ok(
        "let r = ok(42)\n"
        "match r {\n"
        "    ok(v) -> print(v)\n"
        "    err(e) -> print(e)\n"
        "}"));
}

AGL_TEST(test_sema_struct) {
    AGL_ASSERT(ctx, sema_ok(
        "struct Point { x: int\n y: int }\n"
        "let p = Point { x: 1, y: 2 }\n"
        "print(p.x)"));
}

/* ---- Undefined variable errors ---- */

AGL_TEST(test_sema_err_undefined_var) {
    AGL_ASSERT(ctx, !sema_ok("print(x)"));
}

AGL_TEST(test_sema_err_undefined_in_expr) {
    AGL_ASSERT(ctx, !sema_ok("let y = x + 1"));
}

AGL_TEST(test_sema_err_var_used_before_decl) {
    AGL_ASSERT(ctx, !sema_ok("print(x)\nlet x = 1"));
}

/* ---- Immutability errors ---- */

AGL_TEST(test_sema_err_assign_to_let) {
    AGL_ASSERT(ctx, !sema_ok("let x = 1\nx = 2"));
}

AGL_TEST(test_sema_err_assign_to_fn_param) {
    /* Function params are immutable (like let) */
    AGL_ASSERT(ctx, !sema_ok(
        "fn foo(x: int) {\n"
        "    x = 10\n"
        "}"));
}

/* ---- Assign to undefined ---- */

AGL_TEST(test_sema_err_assign_undefined) {
    AGL_ASSERT(ctx, !sema_ok("x = 10"));
}

/* ---- Function arity errors ---- */

AGL_TEST(test_sema_err_too_few_args) {
    AGL_ASSERT(ctx, !sema_ok(
        "fn add(a: int, b: int) -> int { return a + b }\n"
        "print(add(1))"));
}

AGL_TEST(test_sema_err_too_many_args) {
    AGL_ASSERT(ctx, !sema_ok(
        "fn add(a: int, b: int) -> int { return a + b }\n"
        "print(add(1, 2, 3))"));
}

/* ---- Scope isolation ---- */

AGL_TEST(test_sema_err_block_scope_leak) {
    /* Variable declared inside if block not visible outside */
    AGL_ASSERT(ctx, !sema_ok(
        "if true {\n"
        "    let inner = 1\n"
        "}\n"
        "print(inner)"));
}

AGL_TEST(test_sema_err_for_var_leak) {
    /* for loop variable not visible outside */
    AGL_ASSERT(ctx, !sema_ok(
        "for x in [1, 2, 3] {\n"
        "    print(x)\n"
        "}\n"
        "print(x)"));
}

AGL_TEST(test_sema_err_fn_param_leak) {
    /* Function params not visible outside */
    AGL_ASSERT(ctx, !sema_ok(
        "fn foo(a: int) { print(a) }\n"
        "print(a)"));
}

/* ---- Main ---- */

int main(void) {
    AglTestCtx ctx = {0, 0};

    printf("=== Semantic Analysis Tests ===\n");

    /* Valid programs */
    AGL_RUN_TEST(&ctx, test_sema_hello_world);
    AGL_RUN_TEST(&ctx, test_sema_let_binding);
    AGL_RUN_TEST(&ctx, test_sema_var_reassign);
    AGL_RUN_TEST(&ctx, test_sema_fn_decl_and_call);
    AGL_RUN_TEST(&ctx, test_sema_if_else);
    AGL_RUN_TEST(&ctx, test_sema_while);
    AGL_RUN_TEST(&ctx, test_sema_for_in);
    AGL_RUN_TEST(&ctx, test_sema_lambda);
    AGL_RUN_TEST(&ctx, test_sema_result_match);
    AGL_RUN_TEST(&ctx, test_sema_struct);

    /* Undefined variable */
    AGL_RUN_TEST(&ctx, test_sema_err_undefined_var);
    AGL_RUN_TEST(&ctx, test_sema_err_undefined_in_expr);
    AGL_RUN_TEST(&ctx, test_sema_err_var_used_before_decl);

    /* Immutability */
    AGL_RUN_TEST(&ctx, test_sema_err_assign_to_let);
    AGL_RUN_TEST(&ctx, test_sema_err_assign_to_fn_param);

    /* Assign to undefined */
    AGL_RUN_TEST(&ctx, test_sema_err_assign_undefined);

    /* Arity */
    AGL_RUN_TEST(&ctx, test_sema_err_too_few_args);
    AGL_RUN_TEST(&ctx, test_sema_err_too_many_args);

    /* Scope isolation */
    AGL_RUN_TEST(&ctx, test_sema_err_block_scope_leak);
    AGL_RUN_TEST(&ctx, test_sema_err_for_var_leak);
    AGL_RUN_TEST(&ctx, test_sema_err_fn_param_leak);

    AGL_SUMMARY(&ctx);
}
