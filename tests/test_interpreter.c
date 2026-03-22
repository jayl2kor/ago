#include "test_harness.h"
#include "../src/interpreter.h"
#include <unistd.h>

/* ---- Helper: run Ago source and capture stdout ---- */

#define MAX_OUTPUT 4096
static char captured_output[MAX_OUTPUT];

static int run_and_capture(const char *source) {
    /* Flush any pending stdout before redirecting */
    fflush(stdout);

    /* Redirect stdout to a pipe */
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    int saved_stdout = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    AgoCtx *c = ago_ctx_new();
    int result = ago_run(source, "test.ago", c);

    /* Flush and restore stdout */
    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    /* Read captured output */
    ssize_t n = read(pipefd[0], captured_output, MAX_OUTPUT - 1);
    close(pipefd[0]);
    if (n < 0) n = 0;
    captured_output[n] = '\0';

    if (ago_error_occurred(c)) {
        ago_error_print(ago_error_get(c));
        result = -1;
    }
    ago_ctx_free(c);
    return result;
}

/* ================================================================
 *  RED PHASE: These tests define the expected behavior of the
 *  interpreter. They will fail until the interpreter is implemented.
 * ================================================================ */

/* ---- Hello World ---- */

AGO_TEST(test_hello_world) {
    int r = run_and_capture("print(\"hello world\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "hello world\n");
}

/* ---- Print integer ---- */

AGO_TEST(test_print_integer) {
    int r = run_and_capture("print(42)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "42\n");
}

/* ---- Print boolean ---- */

AGO_TEST(test_print_bool) {
    int r = run_and_capture("print(true)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

/* ---- Arithmetic ---- */

AGO_TEST(test_arithmetic) {
    int r = run_and_capture("print(2 + 3 * 4)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "14\n");
}

AGO_TEST(test_subtraction) {
    int r = run_and_capture("print(10 - 3)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "7\n");
}

AGO_TEST(test_division) {
    int r = run_and_capture("print(10 / 3)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "3\n");
}

AGO_TEST(test_modulo) {
    int r = run_and_capture("print(10 % 3)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "1\n");
}

AGO_TEST(test_unary_negate) {
    int r = run_and_capture("print(-5)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "-5\n");
}

/* ---- Let bindings ---- */

AGO_TEST(test_let_binding) {
    int r = run_and_capture("let x = 42\nprint(x)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "42\n");
}

AGO_TEST(test_let_expression) {
    int r = run_and_capture("let x = 2 + 3\nprint(x)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "5\n");
}

/* ---- Multiple statements ---- */

AGO_TEST(test_multiple_prints) {
    int r = run_and_capture("print(1)\nprint(2)\nprint(3)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "1\n2\n3\n");
}

/* ---- Comparison ---- */

AGO_TEST(test_comparison) {
    int r = run_and_capture("print(3 > 2)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGO_TEST(test_equality) {
    int r = run_and_capture("print(1 == 1)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

/* ---- Variable reassignment ---- */

AGO_TEST(test_var_reassign) {
    int r = run_and_capture("var x = 1\nx = 10\nprint(x)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "10\n");
}

AGO_TEST(test_let_immutable) {
    /* Assigning to a let variable should be a runtime error */
    int r = run_and_capture("let x = 1\nx = 10");
    AGO_ASSERT(ctx, r != 0);
}

/* ---- If/else ---- */

AGO_TEST(test_if_true) {
    int r = run_and_capture("if true {\n    print(1)\n}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "1\n");
}

AGO_TEST(test_if_false) {
    int r = run_and_capture("if false {\n    print(1)\n}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "");
}

AGO_TEST(test_if_else_true) {
    int r = run_and_capture("if true {\n    print(1)\n} else {\n    print(2)\n}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "1\n");
}

AGO_TEST(test_if_else_false) {
    int r = run_and_capture("if false {\n    print(1)\n} else {\n    print(2)\n}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "2\n");
}

AGO_TEST(test_if_condition) {
    int r = run_and_capture("let x = 5\nif x > 3 {\n    print(\"big\")\n} else {\n    print(\"small\")\n}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "big\n");
}

/* ---- While loop ---- */

AGO_TEST(test_while_loop) {
    int r = run_and_capture(
        "var i = 0\n"
        "while i < 5 {\n"
        "    i = i + 1\n"
        "}\n"
        "print(i)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "5\n");
}

AGO_TEST(test_while_no_exec) {
    int r = run_and_capture(
        "while false {\n"
        "    print(1)\n"
        "}\n"
        "print(\"done\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "done\n");
}

AGO_TEST(test_while_accumulate) {
    int r = run_and_capture(
        "var sum = 0\n"
        "var i = 1\n"
        "while i <= 10 {\n"
        "    sum = sum + i\n"
        "    i = i + 1\n"
        "}\n"
        "print(sum)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "55\n");
}

/* ---- Function definition and call ---- */

AGO_TEST(test_fn_simple) {
    int r = run_and_capture(
        "fn greet() {\n"
        "    print(\"hi\")\n"
        "}\n"
        "greet()");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "hi\n");
}

AGO_TEST(test_fn_with_params) {
    int r = run_and_capture(
        "fn add(a: int, b: int) -> int {\n"
        "    return a + b\n"
        "}\n"
        "print(add(3, 4))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "7\n");
}

AGO_TEST(test_fn_return_value) {
    int r = run_and_capture(
        "fn square(x: int) -> int {\n"
        "    return x * x\n"
        "}\n"
        "let result = square(5)\n"
        "print(result)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "25\n");
}

AGO_TEST(test_fn_multiple_calls) {
    int r = run_and_capture(
        "fn double(x: int) -> int {\n"
        "    return x * 2\n"
        "}\n"
        "print(double(3))\n"
        "print(double(10))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "6\n20\n");
}

AGO_TEST(test_fn_early_return) {
    int r = run_and_capture(
        "fn abs(x: int) -> int {\n"
        "    if x < 0 {\n"
        "        return -x\n"
        "    }\n"
        "    return x\n"
        "}\n"
        "print(abs(-5))\n"
        "print(abs(3))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "5\n3\n");
}

AGO_TEST(test_fn_recursion) {
    int r = run_and_capture(
        "fn fib(n: int) -> int {\n"
        "    if n <= 1 {\n"
        "        return n\n"
        "    }\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "}\n"
        "print(fib(10))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "55\n");
}

/* ---- Combined ---- */

AGO_TEST(test_fizzbuzz) {
    int r = run_and_capture(
        "var i = 1\n"
        "while i <= 5 {\n"
        "    if i % 3 == 0 {\n"
        "        print(\"fizz\")\n"
        "    } else {\n"
        "        print(i)\n"
        "    }\n"
        "    i = i + 1\n"
        "}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "1\n2\nfizz\n4\n5\n");
}

/* ---- Main ---- */

int main(void) {
    AgoTestCtx ctx = {0, 0};

    printf("=== Interpreter Tests ===\n");

    AGO_RUN_TEST(&ctx, test_hello_world);
    AGO_RUN_TEST(&ctx, test_print_integer);
    AGO_RUN_TEST(&ctx, test_print_bool);
    AGO_RUN_TEST(&ctx, test_arithmetic);
    AGO_RUN_TEST(&ctx, test_subtraction);
    AGO_RUN_TEST(&ctx, test_division);
    AGO_RUN_TEST(&ctx, test_modulo);
    AGO_RUN_TEST(&ctx, test_unary_negate);
    AGO_RUN_TEST(&ctx, test_let_binding);
    AGO_RUN_TEST(&ctx, test_let_expression);
    AGO_RUN_TEST(&ctx, test_multiple_prints);
    AGO_RUN_TEST(&ctx, test_comparison);
    AGO_RUN_TEST(&ctx, test_equality);

    /* Variable reassignment */
    AGO_RUN_TEST(&ctx, test_var_reassign);
    AGO_RUN_TEST(&ctx, test_let_immutable);

    /* If/else */
    AGO_RUN_TEST(&ctx, test_if_true);
    AGO_RUN_TEST(&ctx, test_if_false);
    AGO_RUN_TEST(&ctx, test_if_else_true);
    AGO_RUN_TEST(&ctx, test_if_else_false);
    AGO_RUN_TEST(&ctx, test_if_condition);

    /* While */
    AGO_RUN_TEST(&ctx, test_while_loop);
    AGO_RUN_TEST(&ctx, test_while_no_exec);
    AGO_RUN_TEST(&ctx, test_while_accumulate);

    /* Functions */
    AGO_RUN_TEST(&ctx, test_fn_simple);
    AGO_RUN_TEST(&ctx, test_fn_with_params);
    AGO_RUN_TEST(&ctx, test_fn_return_value);
    AGO_RUN_TEST(&ctx, test_fn_multiple_calls);
    AGO_RUN_TEST(&ctx, test_fn_early_return);
    AGO_RUN_TEST(&ctx, test_fn_recursion);

    /* Combined */
    AGO_RUN_TEST(&ctx, test_fizzbuzz);

    AGO_SUMMARY(&ctx);
}
