#include "test_harness.h"
#include "../src/interpreter.h"
#include "../src/compiler.h"
#include "../src/vm.h"
#include "../src/parser.h"
#include "../src/sema.h"
#include "../src/runtime.h"
#include <unistd.h>

/* ---- Helper: compile+run via VM and capture stdout ---- */

#define MAX_OUTPUT 4096
static char captured_output[MAX_OUTPUT];

static int vm_run_and_capture(const char *source) {
    fflush(stdout);

    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    int saved_stdout = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    AgoCtx *ctx = ago_ctx_new();
    AgoArena *arena = ago_arena_new();
    AgoGc *gc = ago_gc_new();

    AgoParser parser;
    ago_parser_init(&parser, source, "test.ago", arena, ctx);
    AgoNode *program = ago_parser_parse(&parser);

    int result = -1;
    if (program && !ago_error_occurred(ctx)) {
        AgoChunk *chunk = ago_compile(program, ctx, arena, gc);
        if (chunk && !ago_error_occurred(ctx)) {
            result = ago_vm_run(chunk, "test.ago", ctx);
            ago_chunk_free(chunk);
        }
    }

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    ssize_t n = read(pipefd[0], captured_output, MAX_OUTPUT - 1);
    close(pipefd[0]);
    if (n < 0) n = 0;
    captured_output[n] = '\0';

    if (ago_error_occurred(ctx)) {
        ago_error_print(ago_error_get(ctx));
        result = -1;
    }

    ago_gc_free(gc);
    ago_arena_free(arena);
    ago_ctx_free(ctx);
    return result;
}

/* ---- Tests ---- */

AGO_TEST(test_vm_hello_world) {
    int r = vm_run_and_capture("print(\"hello world\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "hello world\n");
}

AGO_TEST(test_vm_print_integer) {
    int r = vm_run_and_capture("print(42)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "42\n");
}

AGO_TEST(test_vm_print_bool) {
    int r = vm_run_and_capture("print(true)\nprint(false)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\nfalse\n");
}

AGO_TEST(test_vm_arithmetic) {
    int r = vm_run_and_capture("print(2 + 3 * 4)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "14\n");
}

AGO_TEST(test_vm_comparison) {
    int r = vm_run_and_capture("print(1 < 2)\nprint(3 > 4)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "true\nfalse\n");
}

AGO_TEST(test_vm_let_binding) {
    int r = vm_run_and_capture("let x = 10\nprint(x)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "10\n");
}

AGO_TEST(test_vm_var_reassign) {
    int r = vm_run_and_capture("var x = 1\nx = 2\nprint(x)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "2\n");
}

AGO_TEST(test_vm_if_else) {
    int r = vm_run_and_capture("if true { print(\"yes\") } else { print(\"no\") }");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "yes\n");
}

AGO_TEST(test_vm_while_loop) {
    int r = vm_run_and_capture(
        "var i = 0\n"
        "while i < 3 {\n"
        "    print(i)\n"
        "    i = i + 1\n"
        "}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "0\n1\n2\n");
}

AGO_TEST(test_vm_for_in) {
    int r = vm_run_and_capture(
        "let arr = [10, 20, 30]\n"
        "for x in arr {\n"
        "    print(x)\n"
        "}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "10\n20\n30\n");
}

AGO_TEST(test_vm_function) {
    int r = vm_run_and_capture(
        "fn add(a: int, b: int) -> int {\n"
        "    return a + b\n"
        "}\n"
        "print(add(3, 4))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "7\n");
}

AGO_TEST(test_vm_recursion) {
    int r = vm_run_and_capture(
        "fn fib(n: int) -> int {\n"
        "    if n <= 1 { return n }\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "}\n"
        "print(fib(10))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "55\n");
}

AGO_TEST(test_vm_closure) {
    int r = vm_run_and_capture(
        "fn make_adder(n: int) -> fn {\n"
        "    return fn(x: int) -> int { return n + x }\n"
        "}\n"
        "let add5 = make_adder(5)\n"
        "print(add5(3))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "8\n");
}

AGO_TEST(test_vm_array_ops) {
    int r = vm_run_and_capture(
        "let arr = [1, 2, 3]\n"
        "print(arr[1])\n"
        "print(len(arr))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "2\n3\n");
}

AGO_TEST(test_vm_struct) {
    int r = vm_run_and_capture(
        "struct Point {\n"
        "    x: int\n"
        "    y: int\n"
        "}\n"
        "let p = Point { x: 10, y: 20 }\n"
        "print(p.x)\nprint(p.y)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "10\n20\n");
}

AGO_TEST(test_vm_result_match) {
    int r = vm_run_and_capture(
        "let r = ok(42)\n"
        "let v = match r {\n"
        "    ok(n) -> n\n"
        "    err(e) -> 0\n"
        "}\n"
        "print(v)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "42\n");
}

AGO_TEST(test_vm_string_ops) {
    int r = vm_run_and_capture(
        "print(\"hello\" + \" world\")\n"
        "print(\"a\" < \"b\")");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "hello world\ntrue\n");
}

AGO_TEST(test_vm_float) {
    int r = vm_run_and_capture("print(1.5 + 2.5)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "4\n");
}

AGO_TEST(test_vm_stdlib) {
    int r = vm_run_and_capture(
        "print(type(42))\n"
        "print(str(100))\n"
        "print(abs(-5))\n"
        "print(min(3, 7))");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "int\n100\n5\n3\n");
}

AGO_TEST(test_vm_map_filter) {
    int r = vm_run_and_capture(
        "let arr = [1, 2, 3, 4]\n"
        "let doubled = map(arr, fn(x: int) -> int { return x * 2 })\n"
        "print(doubled)\n"
        "let evens = filter(arr, fn(x: int) -> bool { return x % 2 == 0 })\n"
        "print(evens)");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output, "[2, 4, 6, 8]\n[2, 4]\n");
}

AGO_TEST(test_vm_fizzbuzz) {
    int r = vm_run_and_capture(
        "var i = 1\n"
        "while i <= 15 {\n"
        "    if i % 15 == 0 { print(\"FizzBuzz\") }\n"
        "    else if i % 3 == 0 { print(\"Fizz\") }\n"
        "    else if i % 5 == 0 { print(\"Buzz\") }\n"
        "    else { print(i) }\n"
        "    i = i + 1\n"
        "}");
    AGO_ASSERT_INT_EQ(ctx, r, 0);
    AGO_ASSERT_STR_EQ(ctx, captured_output,
        "1\n2\nFizz\n4\nBuzz\nFizz\n7\n8\nFizz\nBuzz\n11\nFizz\n13\n14\nFizzBuzz\n");
}

/* ---- Main ---- */

int main(void) {
    AgoTestCtx ctx = {0, 0};
    printf("\n=== VM Tests ===\n");

    AGO_RUN_TEST(&ctx, test_vm_hello_world);
    AGO_RUN_TEST(&ctx, test_vm_print_integer);
    AGO_RUN_TEST(&ctx, test_vm_print_bool);
    AGO_RUN_TEST(&ctx, test_vm_arithmetic);
    AGO_RUN_TEST(&ctx, test_vm_comparison);
    AGO_RUN_TEST(&ctx, test_vm_let_binding);
    AGO_RUN_TEST(&ctx, test_vm_var_reassign);
    AGO_RUN_TEST(&ctx, test_vm_if_else);
    AGO_RUN_TEST(&ctx, test_vm_while_loop);
    AGO_RUN_TEST(&ctx, test_vm_for_in);
    AGO_RUN_TEST(&ctx, test_vm_function);
    AGO_RUN_TEST(&ctx, test_vm_recursion);
    AGO_RUN_TEST(&ctx, test_vm_closure);
    AGO_RUN_TEST(&ctx, test_vm_array_ops);
    AGO_RUN_TEST(&ctx, test_vm_struct);
    AGO_RUN_TEST(&ctx, test_vm_result_match);
    AGO_RUN_TEST(&ctx, test_vm_string_ops);
    AGO_RUN_TEST(&ctx, test_vm_float);
    AGO_RUN_TEST(&ctx, test_vm_stdlib);
    AGO_RUN_TEST(&ctx, test_vm_map_filter);
    AGO_RUN_TEST(&ctx, test_vm_fizzbuzz);

    AGO_SUMMARY(&ctx);
}
