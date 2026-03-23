#include "test_harness.h"
#include "../src/interpreter.h"
#include <unistd.h>
#include <sys/stat.h>

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

/* ---- Helper: write a temp file ---- */

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f) { fputs(content, f); fclose(f); }
}

/* ---- Helper: run from file and capture stdout ---- */

static int run_file_and_capture(const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *source = malloc((size_t)len + 1);
    if (!source) { fclose(f); return -1; }
    fread(source, 1, (size_t)len, f);
    source[len] = '\0';
    fclose(f);

    fflush(stdout);
    int pipefd[2];
    if (pipe(pipefd) != 0) { free(source); return -1; }
    int saved_stdout = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    AgoCtx *c = ago_ctx_new();
    int result = ago_run(source, filepath, c);

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    ssize_t n = read(pipefd[0], captured_output, MAX_OUTPUT - 1);
    close(pipefd[0]);
    if (n < 0) n = 0;
    captured_output[n] = '\0';

    if (ago_error_occurred(c)) {
        ago_error_print(ago_error_get(c));
        result = -1;
    }
    ago_ctx_free(c);
    free(source);
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

/* ---- Arrays ---- */

AGO_TEST(test_array_literal) {
    int r = run_and_capture(
        "let arr = [1, 2, 3]\n"
        "print(arr)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "[1, 2, 3]\n");
}

AGO_TEST(test_array_index) {
    int r = run_and_capture(
        "let arr = [10, 20, 30]\n"
        "print(arr[0])\n"
        "print(arr[2])");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "10\n30\n");
}

AGO_TEST(test_array_length) {
    int r = run_and_capture(
        "let arr = [1, 2, 3, 4, 5]\n"
        "print(len(arr))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "5\n");
}

AGO_TEST(test_array_out_of_bounds) {
    int r = run_and_capture(
        "let arr = [1, 2]\n"
        "print(arr[5])");
    AGO_ASSERT(ctx, r != 0);
}

/* ---- For-in loop ---- */

AGO_TEST(test_for_in_array) {
    int r = run_and_capture(
        "let nums = [1, 2, 3]\n"
        "for n in nums {\n"
        "    print(n)\n"
        "}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "1\n2\n3\n");
}

AGO_TEST(test_for_in_sum) {
    int r = run_and_capture(
        "let nums = [1, 2, 3, 4, 5]\n"
        "var total = 0\n"
        "for n in nums {\n"
        "    total = total + n\n"
        "}\n"
        "print(total)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "15\n");
}

AGO_TEST(test_for_in_empty) {
    int r = run_and_capture(
        "let empty = []\n"
        "for x in empty {\n"
        "    print(x)\n"
        "}\n"
        "print(\"done\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "done\n");
}

/* ---- Structs ---- */

AGO_TEST(test_struct_create) {
    int r = run_and_capture(
        "struct Point {\n"
        "    x: int\n"
        "    y: int\n"
        "}\n"
        "let p = Point { x: 10, y: 20 }\n"
        "print(p.x)\n"
        "print(p.y)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "10\n20\n");
}

AGO_TEST(test_struct_in_function) {
    int r = run_and_capture(
        "struct Point {\n"
        "    x: int\n"
        "    y: int\n"
        "}\n"
        "fn make_point(x: int, y: int) -> Point {\n"
        "    return Point { x: x, y: y }\n"
        "}\n"
        "let p = make_point(3, 4)\n"
        "print(p.x + p.y)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "7\n");
}

/* ---- Float arithmetic ---- */

AGO_TEST(test_float_add) {
    int r = run_and_capture("print(1.5 + 2.5)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "4\n");
}

AGO_TEST(test_float_mul) {
    int r = run_and_capture("print(2.5 * 4.0)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "10\n");
}

AGO_TEST(test_int_float_mixed) {
    int r = run_and_capture("print(1 + 0.5)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "1.5\n");
}

/* ---- Error path tests ---- */

AGO_TEST(test_err_div_by_zero) {
    int r = run_and_capture("print(1 / 0)");
    AGO_ASSERT(ctx, r != 0);
}

AGO_TEST(test_err_mod_by_zero) {
    int r = run_and_capture("print(1 % 0)");
    AGO_ASSERT(ctx, r != 0);
}

AGO_TEST(test_err_undefined_variable) {
    int r = run_and_capture("print(x)");
    AGO_ASSERT(ctx, r != 0);
}

AGO_TEST(test_err_unknown_function) {
    int r = run_and_capture("foo()");
    AGO_ASSERT(ctx, r != 0);
}

AGO_TEST(test_err_wrong_arg_count) {
    int r = run_and_capture(
        "fn add(a: int, b: int) -> int { return a + b }\n"
        "print(add(1))");
    AGO_ASSERT(ctx, r != 0);
}

AGO_TEST(test_err_type_mismatch) {
    int r = run_and_capture("print(1 + true)");
    AGO_ASSERT(ctx, r != 0);
}

AGO_TEST(test_err_unary_type) {
    int r = run_and_capture("print(!5)");
    AGO_ASSERT(ctx, r != 0);
}

AGO_TEST(test_err_index_non_array) {
    int r = run_and_capture("let x = 5\nprint(x[0])");
    AGO_ASSERT(ctx, r != 0);
}

AGO_TEST(test_err_negative_index) {
    int r = run_and_capture("let arr = [1, 2]\nprint(arr[-1])");
    AGO_ASSERT(ctx, r != 0);
}

AGO_TEST(test_err_for_non_array) {
    int r = run_and_capture("for x in 5 {\n    print(x)\n}");
    AGO_ASSERT(ctx, r != 0);
}

AGO_TEST(test_err_len_wrong_type) {
    int r = run_and_capture("print(len(42))");
    AGO_ASSERT(ctx, r != 0);
}

/* ---- Else-if chain ---- */

AGO_TEST(test_else_if_chain) {
    int r = run_and_capture(
        "let x = 2\n"
        "if x == 1 {\n    print(\"one\")\n"
        "} else if x == 2 {\n    print(\"two\")\n"
        "} else {\n    print(\"other\")\n}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "two\n");
}

/* ---- Lambda / Closures ---- */

AGO_TEST(test_lambda_basic) {
    int r = run_and_capture(
        "let double = fn(x: int) -> int {\n"
        "    return x * 2\n"
        "}\n"
        "print(double(5))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "10\n");
}

AGO_TEST(test_lambda_no_params) {
    int r = run_and_capture(
        "let greet = fn() {\n"
        "    print(\"hello\")\n"
        "}\n"
        "greet()");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "hello\n");
}

AGO_TEST(test_lambda_higher_order) {
    int r = run_and_capture(
        "fn apply(f: fn, x: int) -> int {\n"
        "    return f(x)\n"
        "}\n"
        "let sq = fn(n: int) -> int { return n * n }\n"
        "print(apply(sq, 4))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "16\n");
}

AGO_TEST(test_lambda_closure) {
    int r = run_and_capture(
        "fn make_adder(n: int) -> fn {\n"
        "    return fn(x: int) -> int {\n"
        "        return x + n\n"
        "    }\n"
        "}\n"
        "let add5 = make_adder(5)\n"
        "print(add5(10))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "15\n");
}

AGO_TEST(test_lambda_closure_multiple) {
    int r = run_and_capture(
        "fn make_adder(n: int) -> fn {\n"
        "    return fn(x: int) -> int {\n"
        "        return x + n\n"
        "    }\n"
        "}\n"
        "let add3 = make_adder(3)\n"
        "let add10 = make_adder(10)\n"
        "print(add3(1))\n"
        "print(add10(1))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "4\n11\n");
}

AGO_TEST(test_lambda_inline_call) {
    /* Pass lambda directly as argument */
    int r = run_and_capture(
        "fn apply(f: fn, x: int) -> int {\n"
        "    return f(x)\n"
        "}\n"
        "print(apply(fn(n: int) -> int { return n + 1 }, 9))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "10\n");
}

AGO_TEST(test_lambda_as_value) {
    /* Lambda is truthy, prints as <fn> */
    int r = run_and_capture(
        "let f = fn() { }\n"
        "print(f)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "<fn>\n");
}

AGO_TEST(test_err_call_non_function) {
    int r = run_and_capture(
        "let x = 42\n"
        "x(1)");
    AGO_ASSERT(ctx, r != 0);
}

/* ---- Result + Match ---- */

AGO_TEST(test_match_ok) {
    int r = run_and_capture(
        "let result = ok(42)\n"
        "match result {\n"
        "    ok(x) -> print(x)\n"
        "    err(e) -> print(e)\n"
        "}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "42\n");
}

AGO_TEST(test_match_err) {
    int r = run_and_capture(
        "let result = err(\"bad\")\n"
        "match result {\n"
        "    ok(x) -> print(x)\n"
        "    err(e) -> print(e)\n"
        "}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "bad\n");
}

AGO_TEST(test_match_as_expression) {
    int r = run_and_capture(
        "let val = match ok(10) {\n"
        "    ok(x) -> x + 1\n"
        "    err(e) -> 0\n"
        "}\n"
        "print(val)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "11\n");
}

AGO_TEST(test_match_err_as_expression) {
    int r = run_and_capture(
        "let val = match err(\"x\") {\n"
        "    ok(x) -> 1\n"
        "    err(e) -> -1\n"
        "}\n"
        "print(val)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "-1\n");
}

AGO_TEST(test_result_from_function) {
    int r = run_and_capture(
        "fn safe_div(a: int, b: int) -> result {\n"
        "    if b == 0 {\n"
        "        return err(\"division by zero\")\n"
        "    }\n"
        "    return ok(a / b)\n"
        "}\n"
        "match safe_div(10, 2) {\n"
        "    ok(v) -> print(v)\n"
        "    err(e) -> print(e)\n"
        "}\n"
        "match safe_div(10, 0) {\n"
        "    ok(v) -> print(v)\n"
        "    err(e) -> print(e)\n"
        "}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "5\ndivision by zero\n");
}

AGO_TEST(test_print_result) {
    int r = run_and_capture(
        "print(ok(42))\n"
        "print(err(\"bad\"))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "ok(42)\nerr(bad)\n");
}

AGO_TEST(test_result_chaining) {
    int r = run_and_capture(
        "fn safe_sqrt(n: int) -> result {\n"
        "    if n < 0 {\n"
        "        return err(\"negative\")\n"
        "    }\n"
        "    return ok(n)\n"
        "}\n"
        "fn double_safe(n: int) -> result {\n"
        "    let r = safe_sqrt(n)\n"
        "    return match r {\n"
        "        ok(v) -> ok(v * 2)\n"
        "        err(e) -> err(e)\n"
        "    }\n"
        "}\n"
        "match double_safe(5) {\n"
        "    ok(v) -> print(v)\n"
        "    err(e) -> print(e)\n"
        "}\n"
        "match double_safe(-1) {\n"
        "    ok(v) -> print(v)\n"
        "    err(e) -> print(e)\n"
        "}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "10\nnegative\n");
}

AGO_TEST(test_err_match_non_result) {
    int r = run_and_capture(
        "match 42 {\n"
        "    ok(x) -> print(x)\n"
        "    err(e) -> print(e)\n"
        "}");
    AGO_ASSERT(ctx, r != 0);
}

AGO_TEST(test_err_match_duplicate_arm) {
    int r = run_and_capture(
        "match ok(1) {\n"
        "    ok(x) -> x\n"
        "    ok(y) -> y\n"
        "}");
    AGO_ASSERT(ctx, r != 0);
}

/* ---- GC integration ---- */

AGO_TEST(test_gc_loop_temp_arrays) {
    /* Loop creating many temporary arrays — GC should collect them */
    int r = run_and_capture(
        "var i = 0\n"
        "var arr = [0]\n"
        "while i < 200 {\n"
        "    arr = [1, 2, 3, 4, 5]\n"
        "    i = i + 1\n"
        "}\n"
        "print(\"done\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "done\n");
}

AGO_TEST(test_gc_reachable_survives) {
    /* Long-lived array must survive GC cycles triggered by temp objects */
    int r = run_and_capture(
        "let keeper = [10, 20, 30]\n"
        "var i = 0\n"
        "var temp = [0]\n"
        "while i < 200 {\n"
        "    temp = [i, i + 1]\n"
        "    i = i + 1\n"
        "}\n"
        "print(keeper[1])");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "20\n");
}

AGO_TEST(test_gc_closure_survives) {
    /* Closure captured values must survive GC */
    int r = run_and_capture(
        "fn make_adder(n: int) -> fn {\n"
        "    return fn(x: int) -> int { return x + n }\n"
        "}\n"
        "let add5 = make_adder(5)\n"
        "var i = 0\n"
        "var temp = [0]\n"
        "while i < 200 {\n"
        "    temp = [1, 2, 3]\n"
        "    i = i + 1\n"
        "}\n"
        "print(add5(10))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "15\n");
}

AGO_TEST(test_gc_result_survives) {
    /* Result values must survive GC */
    int r = run_and_capture(
        "let r = ok(42)\n"
        "var i = 0\n"
        "var temp = [0]\n"
        "while i < 200 {\n"
        "    temp = [i]\n"
        "    i = i + 1\n"
        "}\n"
        "match r {\n"
        "    ok(v) -> print(v)\n"
        "    err(e) -> print(e)\n"
        "}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "42\n");
}

/* ---- Stdlib: String operations ---- */

AGO_TEST(test_string_equality) {
    int r = run_and_capture("print(\"hello\" == \"hello\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGO_TEST(test_string_inequality) {
    int r = run_and_capture("print(\"hello\" != \"world\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGO_TEST(test_string_eq_false) {
    int r = run_and_capture("print(\"hello\" == \"world\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "false\n");
}

AGO_TEST(test_string_concat) {
    int r = run_and_capture("print(\"hello\" + \" world\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "hello world\n");
}

AGO_TEST(test_string_concat_multi) {
    int r = run_and_capture("let s = \"a\" + \"b\" + \"c\"\nprint(s)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "abc\n");
}

AGO_TEST(test_string_len) {
    int r = run_and_capture("print(len(\"hello\"))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "5\n");
}

/* ---- String ordering ---- */

AGO_TEST(test_string_less_than) {
    int r = run_and_capture("print(\"apple\" < \"banana\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGO_TEST(test_string_greater_than) {
    int r = run_and_capture("print(\"banana\" > \"apple\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGO_TEST(test_string_less_equal) {
    int r = run_and_capture(
        "print(\"abc\" <= \"abc\")\n"
        "print(\"abc\" <= \"abd\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\ntrue\n");
}

AGO_TEST(test_string_greater_equal) {
    int r = run_and_capture(
        "print(\"xyz\" >= \"xyz\")\n"
        "print(\"xyz\" >= \"xyy\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\ntrue\n");
}

AGO_TEST(test_string_order_prefix) {
    /* shorter string is less than longer with same prefix */
    int r = run_and_capture("print(\"ab\" < \"abc\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGO_TEST(test_string_order_false) {
    int r = run_and_capture("print(\"banana\" < \"apple\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "false\n");
}

/* ---- Stdlib: Type and conversion ---- */

AGO_TEST(test_type_int) {
    int r = run_and_capture("print(type(42))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "int\n");
}

AGO_TEST(test_type_string) {
    int r = run_and_capture("print(type(\"hi\"))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "string\n");
}

AGO_TEST(test_type_array) {
    int r = run_and_capture("print(type([1,2]))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "array\n");
}

AGO_TEST(test_type_fn) {
    int r = run_and_capture("print(type(fn() {}))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "fn\n");
}

AGO_TEST(test_type_bool) {
    int r = run_and_capture("print(type(true))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "bool\n");
}

AGO_TEST(test_str_int) {
    int r = run_and_capture("let s = str(42)\nprint(s)\nprint(type(s))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "42\nstring\n");
}

AGO_TEST(test_str_bool) {
    int r = run_and_capture("print(str(true))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGO_TEST(test_int_parse) {
    int r = run_and_capture("let n = int(\"42\")\nprint(n + 1)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "43\n");
}

AGO_TEST(test_float_parse) {
    int r = run_and_capture("let f = float(\"3.14\")\nprint(f)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "3.14\n");
}

/* ---- Stdlib: Array functions ---- */

AGO_TEST(test_push) {
    int r = run_and_capture(
        "let a = [1, 2]\n"
        "let b = push(a, 3)\n"
        "print(a)\n"
        "print(b)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "[1, 2]\n[1, 2, 3]\n");
}

AGO_TEST(test_map) {
    int r = run_and_capture(
        "let a = map([1, 2, 3], fn(x: int) -> int { return x * 2 })\n"
        "print(a)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "[2, 4, 6]\n");
}

AGO_TEST(test_filter) {
    int r = run_and_capture(
        "let a = filter([1, 2, 3, 4, 5], fn(x: int) -> bool { return x > 3 })\n"
        "print(a)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "[4, 5]\n");
}

/* ---- Stdlib: Math ---- */

AGO_TEST(test_abs) {
    int r = run_and_capture("print(abs(-5))\nprint(abs(3))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "5\n3\n");
}

AGO_TEST(test_min_max) {
    int r = run_and_capture("print(min(3, 7))\nprint(max(3, 7))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "3\n7\n");
}

/* ---- Import / Modules ---- */

#define TEST_DIR "/tmp/ago_test"

static void setup_test_dir(void) {
    (void)mkdir(TEST_DIR, 0755);
}

AGO_TEST(test_import_function) {
    setup_test_dir();
    write_file(TEST_DIR "/math.ago", "fn double(x: int) -> int { return x * 2 }\n");
    write_file(TEST_DIR "/main.ago",
        "import \"math\"\n"
        "print(double(5))\n");
    int r = run_file_and_capture(TEST_DIR "/main.ago");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "10\n");
}

AGO_TEST(test_import_variable) {
    setup_test_dir();
    write_file(TEST_DIR "/consts.ago", "let PI = 3\n");
    write_file(TEST_DIR "/main2.ago",
        "import \"consts\"\n"
        "print(PI)\n");
    int r = run_file_and_capture(TEST_DIR "/main2.ago");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "3\n");
}

AGO_TEST(test_import_multiple) {
    setup_test_dir();
    write_file(TEST_DIR "/a.ago", "fn fa() -> int { return 1 }\n");
    write_file(TEST_DIR "/b.ago", "fn fb() -> int { return 2 }\n");
    write_file(TEST_DIR "/main3.ago",
        "import \"a\"\n"
        "import \"b\"\n"
        "print(fa() + fb())\n");
    int r = run_file_and_capture(TEST_DIR "/main3.ago");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "3\n");
}

AGO_TEST(test_import_transitive) {
    setup_test_dir();
    write_file(TEST_DIR "/base.ago", "fn base_val() -> int { return 10 }\n");
    write_file(TEST_DIR "/mid.ago",
        "import \"base\"\n"
        "fn mid_val() -> int { return base_val() + 5 }\n");
    write_file(TEST_DIR "/main4.ago",
        "import \"mid\"\n"
        "print(mid_val())\n");
    int r = run_file_and_capture(TEST_DIR "/main4.ago");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "15\n");
}

AGO_TEST(test_import_not_found) {
    setup_test_dir();
    write_file(TEST_DIR "/main5.ago", "import \"nonexistent\"\n");
    int r = run_file_and_capture(TEST_DIR "/main5.ago");
    AGO_ASSERT(ctx, r != 0);
}

AGO_TEST(test_import_path_traversal) {
    /* Path traversal must be rejected */
    setup_test_dir();
    write_file(TEST_DIR "/evil.ago", "import \"../../etc/passwd\"\n");
    int r = run_file_and_capture(TEST_DIR "/evil.ago");
    AGO_ASSERT(ctx, r != 0);
}

AGO_TEST(test_import_dotdot) {
    setup_test_dir();
    write_file(TEST_DIR "/evil2.ago", "import \"../something\"\n");
    int r = run_file_and_capture(TEST_DIR "/evil2.ago");
    AGO_ASSERT(ctx, r != 0);
}

/* ---- REPL ---- */

static int repl_exec_capture(AgoRepl *repl, const char *source) {
    fflush(stdout);
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    int saved_stdout = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    int result = ago_repl_exec(repl, source);

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    ssize_t n = read(pipefd[0], captured_output, MAX_OUTPUT - 1);
    close(pipefd[0]);
    if (n < 0) n = 0;
    captured_output[n] = '\0';
    return result;
}

AGO_TEST(test_repl_basic) {
    AgoRepl *repl = ago_repl_new();
    AGO_ASSERT_FATAL(ctx, repl != NULL);
    int r = repl_exec_capture(repl, "print(42)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "42\n");
    ago_repl_free(repl);
}

AGO_TEST(test_repl_persistent_var) {
    AgoRepl *repl = ago_repl_new();
    AGO_ASSERT_FATAL(ctx, repl != NULL);
    repl_exec_capture(repl, "let x = 10");
    int r = repl_exec_capture(repl, "print(x + 5)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "15\n");
    ago_repl_free(repl);
}

AGO_TEST(test_repl_persistent_fn) {
    AgoRepl *repl = ago_repl_new();
    AGO_ASSERT_FATAL(ctx, repl != NULL);
    repl_exec_capture(repl, "fn double(x: int) -> int { return x * 2 }");
    int r = repl_exec_capture(repl, "print(double(7))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "14\n");
    ago_repl_free(repl);
}

AGO_TEST(test_repl_error_recovery) {
    AgoRepl *repl = ago_repl_new();
    AGO_ASSERT_FATAL(ctx, repl != NULL);
    repl_exec_capture(repl, "let x = 5");
    /* This should error but not crash the REPL */
    int r1 = repl_exec_capture(repl, "print(undefined_var)");
    AGO_ASSERT(ctx, r1 != 0);
    /* REPL should still work after error */
    int r2 = repl_exec_capture(repl, "print(x)");
    AGO_ASSERT_INT_EQ(ctx, r2, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "5\n");
    ago_repl_free(repl);
}

AGO_TEST(test_repl_multiline) {
    AgoRepl *repl = ago_repl_new();
    AGO_ASSERT_FATAL(ctx, repl != NULL);
    int r = repl_exec_capture(repl,
        "fn add(a: int, b: int) -> int {\n"
        "    return a + b\n"
        "}\n"
        "print(add(3, 4))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "7\n");
    ago_repl_free(repl);
}

AGO_TEST(test_repl_var_reassign) {
    AgoRepl *repl = ago_repl_new();
    AGO_ASSERT_FATAL(ctx, repl != NULL);
    repl_exec_capture(repl, "var count = 0");
    repl_exec_capture(repl, "count = count + 1");
    repl_exec_capture(repl, "count = count + 1");
    int r = repl_exec_capture(repl, "print(count)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "2\n");
    ago_repl_free(repl);
}

/* ---- Error stack trace tests ---- */

AGO_TEST(test_error_trace_nested) {
    AgoCtx *c = ago_ctx_new();
    ago_run(
        "fn inner() -> int {\n"
        "    return 1 / 0\n"
        "}\n"
        "fn outer() -> int {\n"
        "    return inner()\n"
        "}\n"
        "outer()\n",
        "test.ago", c);
    AGO_ASSERT(ctx, ago_error_occurred(c));
    const AgoError *err = ago_error_get(c);
    AGO_ASSERT(ctx, err->trace_count >= 2);
    ago_ctx_free(c);
}

AGO_TEST(test_error_trace_top_level) {
    AgoCtx *c = ago_ctx_new();
    ago_run("let x = 1 / 0", "test.ago", c);
    AGO_ASSERT(ctx, ago_error_occurred(c));
    const AgoError *err = ago_error_get(c);
    AGO_ASSERT_INT_EQ(ctx, err->trace_count, 0);
    ago_ctx_free(c);
}

AGO_TEST(test_error_trace_deep) {
    AgoCtx *c = ago_ctx_new();
    ago_run(
        "fn c_fn() -> int { return 1 / 0 }\n"
        "fn b_fn() -> int { return c_fn() }\n"
        "fn a_fn() -> int { return b_fn() }\n"
        "a_fn()\n",
        "test.ago", c);
    AGO_ASSERT(ctx, ago_error_occurred(c));
    const AgoError *err = ago_error_get(c);
    AGO_ASSERT(ctx, err->trace_count >= 3);
    ago_ctx_free(c);
}

/* ---- Stdlib Tier 2: File I/O ---- */

AGO_TEST(test_read_file) {
    /* Create a temp file */
    const char *tmp = "/tmp/ago_test_read.txt";
    FILE *f = fopen(tmp, "w");
    fprintf(f, "hello ago");
    fclose(f);

    char src[256];
    snprintf(src, sizeof(src),
        "let r = read_file(\"%s\")\n"
        "let content = match r {\n"
        "    ok(s) -> s\n"
        "    err(e) -> e\n"
        "}\n"
        "print(content)\n", tmp);
    int r = run_and_capture(src);
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "hello ago\n");
    remove(tmp);
}

AGO_TEST(test_write_file) {
    const char *tmp = "/tmp/ago_test_write.txt";
    char src[256];
    snprintf(src, sizeof(src),
        "let r = write_file(\"%s\", \"ago output\")\n"
        "let ok_val = match r {\n"
        "    ok(v) -> v\n"
        "    err(e) -> false\n"
        "}\n"
        "print(ok_val)\n", tmp);
    int r = run_and_capture(src);
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\n");

    /* Verify file contents */
    FILE *f = fopen(tmp, "r");
    AGO_ASSERT_FATAL(ctx, f != NULL);
    char buf[64];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    AGO_ASSERT_STR_EQ(ctx, buf, "ago output");
    remove(tmp);
}

AGO_TEST(test_read_file_not_found) {
    int r = run_and_capture(
        "let r = read_file(\"/tmp/ago_nonexistent_12345.txt\")\n"
        "let msg = match r {\n"
        "    ok(s) -> \"ok\"\n"
        "    err(e) -> \"error\"\n"
        "}\n"
        "print(msg)\n");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "error\n");
}

AGO_TEST(test_file_exists) {
    const char *tmp = "/tmp/ago_test_exists.txt";
    FILE *f = fopen(tmp, "w");
    fprintf(f, "x");
    fclose(f);

    char src[256];
    snprintf(src, sizeof(src),
        "print(file_exists(\"%s\"))\n"
        "print(file_exists(\"/tmp/ago_no_such_file_999.txt\"))\n", tmp);
    int r = run_and_capture(src);
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\nfalse\n");
    remove(tmp);
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

    /* Arrays */
    AGO_RUN_TEST(&ctx, test_array_literal);
    AGO_RUN_TEST(&ctx, test_array_index);
    AGO_RUN_TEST(&ctx, test_array_length);
    AGO_RUN_TEST(&ctx, test_array_out_of_bounds);

    /* For-in */
    AGO_RUN_TEST(&ctx, test_for_in_array);
    AGO_RUN_TEST(&ctx, test_for_in_sum);
    AGO_RUN_TEST(&ctx, test_for_in_empty);

    /* Structs */
    AGO_RUN_TEST(&ctx, test_struct_create);
    AGO_RUN_TEST(&ctx, test_struct_in_function);

    /* Float arithmetic */
    AGO_RUN_TEST(&ctx, test_float_add);
    AGO_RUN_TEST(&ctx, test_float_mul);
    AGO_RUN_TEST(&ctx, test_int_float_mixed);

    /* Error paths */
    AGO_RUN_TEST(&ctx, test_err_div_by_zero);
    AGO_RUN_TEST(&ctx, test_err_mod_by_zero);
    AGO_RUN_TEST(&ctx, test_err_undefined_variable);
    AGO_RUN_TEST(&ctx, test_err_unknown_function);
    AGO_RUN_TEST(&ctx, test_err_wrong_arg_count);
    AGO_RUN_TEST(&ctx, test_err_type_mismatch);
    AGO_RUN_TEST(&ctx, test_err_unary_type);
    AGO_RUN_TEST(&ctx, test_err_index_non_array);
    AGO_RUN_TEST(&ctx, test_err_negative_index);
    AGO_RUN_TEST(&ctx, test_err_for_non_array);
    AGO_RUN_TEST(&ctx, test_err_len_wrong_type);

    /* Else-if */
    AGO_RUN_TEST(&ctx, test_else_if_chain);

    /* Lambda / Closures */
    AGO_RUN_TEST(&ctx, test_lambda_basic);
    AGO_RUN_TEST(&ctx, test_lambda_no_params);
    AGO_RUN_TEST(&ctx, test_lambda_higher_order);
    AGO_RUN_TEST(&ctx, test_lambda_closure);
    AGO_RUN_TEST(&ctx, test_lambda_closure_multiple);
    AGO_RUN_TEST(&ctx, test_lambda_inline_call);
    AGO_RUN_TEST(&ctx, test_lambda_as_value);
    AGO_RUN_TEST(&ctx, test_err_call_non_function);

    /* Result + Match */
    AGO_RUN_TEST(&ctx, test_match_ok);
    AGO_RUN_TEST(&ctx, test_match_err);
    AGO_RUN_TEST(&ctx, test_match_as_expression);
    AGO_RUN_TEST(&ctx, test_match_err_as_expression);
    AGO_RUN_TEST(&ctx, test_result_from_function);
    AGO_RUN_TEST(&ctx, test_print_result);
    AGO_RUN_TEST(&ctx, test_result_chaining);
    AGO_RUN_TEST(&ctx, test_err_match_non_result);
    AGO_RUN_TEST(&ctx, test_err_match_duplicate_arm);

    /* GC integration */
    AGO_RUN_TEST(&ctx, test_gc_loop_temp_arrays);
    AGO_RUN_TEST(&ctx, test_gc_reachable_survives);
    AGO_RUN_TEST(&ctx, test_gc_closure_survives);
    AGO_RUN_TEST(&ctx, test_gc_result_survives);

    /* Stdlib: String operations */
    AGO_RUN_TEST(&ctx, test_string_equality);
    AGO_RUN_TEST(&ctx, test_string_inequality);
    AGO_RUN_TEST(&ctx, test_string_eq_false);
    AGO_RUN_TEST(&ctx, test_string_concat);
    AGO_RUN_TEST(&ctx, test_string_concat_multi);
    AGO_RUN_TEST(&ctx, test_string_len);
    AGO_RUN_TEST(&ctx, test_string_less_than);
    AGO_RUN_TEST(&ctx, test_string_greater_than);
    AGO_RUN_TEST(&ctx, test_string_less_equal);
    AGO_RUN_TEST(&ctx, test_string_greater_equal);
    AGO_RUN_TEST(&ctx, test_string_order_prefix);
    AGO_RUN_TEST(&ctx, test_string_order_false);

    /* Stdlib: Type and conversion */
    AGO_RUN_TEST(&ctx, test_type_int);
    AGO_RUN_TEST(&ctx, test_type_string);
    AGO_RUN_TEST(&ctx, test_type_array);
    AGO_RUN_TEST(&ctx, test_type_fn);
    AGO_RUN_TEST(&ctx, test_type_bool);
    AGO_RUN_TEST(&ctx, test_str_int);
    AGO_RUN_TEST(&ctx, test_str_bool);
    AGO_RUN_TEST(&ctx, test_int_parse);
    AGO_RUN_TEST(&ctx, test_float_parse);

    /* Stdlib: Array functions */
    AGO_RUN_TEST(&ctx, test_push);
    AGO_RUN_TEST(&ctx, test_map);
    AGO_RUN_TEST(&ctx, test_filter);

    /* Stdlib: Math */
    AGO_RUN_TEST(&ctx, test_abs);
    AGO_RUN_TEST(&ctx, test_min_max);

    /* Import / Modules */
    AGO_RUN_TEST(&ctx, test_import_function);
    AGO_RUN_TEST(&ctx, test_import_variable);
    AGO_RUN_TEST(&ctx, test_import_multiple);
    AGO_RUN_TEST(&ctx, test_import_transitive);
    AGO_RUN_TEST(&ctx, test_import_not_found);
    AGO_RUN_TEST(&ctx, test_import_path_traversal);
    AGO_RUN_TEST(&ctx, test_import_dotdot);

    /* REPL */
    AGO_RUN_TEST(&ctx, test_repl_basic);
    AGO_RUN_TEST(&ctx, test_repl_persistent_var);
    AGO_RUN_TEST(&ctx, test_repl_persistent_fn);
    AGO_RUN_TEST(&ctx, test_repl_error_recovery);
    AGO_RUN_TEST(&ctx, test_repl_multiline);
    AGO_RUN_TEST(&ctx, test_repl_var_reassign);

    /* Error stack trace */
    AGO_RUN_TEST(&ctx, test_error_trace_nested);
    AGO_RUN_TEST(&ctx, test_error_trace_top_level);
    AGO_RUN_TEST(&ctx, test_error_trace_deep);

    /* Stdlib Tier 2: File I/O */
    AGO_RUN_TEST(&ctx, test_read_file);
    AGO_RUN_TEST(&ctx, test_write_file);
    AGO_RUN_TEST(&ctx, test_read_file_not_found);
    AGO_RUN_TEST(&ctx, test_file_exists);

    AGO_SUMMARY(&ctx);
}
