#include "test_harness.h"
#include "../src/interpreter.h"
#include <unistd.h>

/* ---- Helper: run via VM (agl_run now uses VM) and capture stdout ---- */

#define MAX_OUTPUT 4096
static char captured_output[MAX_OUTPUT];

static int vm_run_and_capture(const char *source) {
    fflush(stdout);

    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;
    int saved_stdout = dup(STDOUT_FILENO);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);

    AglCtx *ctx = agl_ctx_new();
    int result = agl_run(source, "test.agl", ctx);

    fflush(stdout);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);

    ssize_t n = read(pipefd[0], captured_output, MAX_OUTPUT - 1);
    close(pipefd[0]);
    if (n < 0) n = 0;
    captured_output[n] = '\0';

    if (agl_error_occurred(ctx)) {
        agl_error_print(agl_error_get(ctx));
        result = -1;
    }

    agl_ctx_free(ctx);
    return result;
}

/* ---- Tests ---- */

AGL_TEST(test_vm_hello_world) {
    int r = vm_run_and_capture("print(\"hello world\")");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "hello world\n");
}

AGL_TEST(test_vm_print_integer) {
    int r = vm_run_and_capture("print(42)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "42\n");
}

AGL_TEST(test_vm_print_bool) {
    int r = vm_run_and_capture("print(true)\nprint(false)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "true\nfalse\n");
}

AGL_TEST(test_vm_arithmetic) {
    int r = vm_run_and_capture("print(2 + 3 * 4)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "14\n");
}

AGL_TEST(test_vm_comparison) {
    int r = vm_run_and_capture("print(1 < 2)\nprint(3 > 4)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "true\nfalse\n");
}

AGL_TEST(test_vm_let_binding) {
    int r = vm_run_and_capture("let x = 10\nprint(x)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "10\n");
}

AGL_TEST(test_vm_var_reassign) {
    int r = vm_run_and_capture("var x = 1\nx = 2\nprint(x)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "2\n");
}

AGL_TEST(test_vm_if_else) {
    int r = vm_run_and_capture("if true { print(\"yes\") } else { print(\"no\") }");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "yes\n");
}

AGL_TEST(test_vm_while_loop) {
    int r = vm_run_and_capture(
        "var i = 0\n"
        "while i < 3 {\n"
        "    print(i)\n"
        "    i = i + 1\n"
        "}");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "0\n1\n2\n");
}

AGL_TEST(test_vm_for_in) {
    int r = vm_run_and_capture(
        "let arr = [10, 20, 30]\n"
        "for x in arr {\n"
        "    print(x)\n"
        "}");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "10\n20\n30\n");
}

AGL_TEST(test_vm_function) {
    int r = vm_run_and_capture(
        "fn add(a: int, b: int) -> int {\n"
        "    return a + b\n"
        "}\n"
        "print(add(3, 4))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "7\n");
}

AGL_TEST(test_vm_recursion) {
    int r = vm_run_and_capture(
        "fn fib(n: int) -> int {\n"
        "    if n <= 1 { return n }\n"
        "    return fib(n - 1) + fib(n - 2)\n"
        "}\n"
        "print(fib(10))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "55\n");
}

AGL_TEST(test_vm_closure) {
    int r = vm_run_and_capture(
        "fn make_adder(n: int) -> fn {\n"
        "    return fn(x: int) -> int { return n + x }\n"
        "}\n"
        "let add5 = make_adder(5)\n"
        "print(add5(3))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "8\n");
}

AGL_TEST(test_vm_array_ops) {
    int r = vm_run_and_capture(
        "let arr = [1, 2, 3]\n"
        "print(arr[1])\n"
        "print(len(arr))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "2\n3\n");
}

AGL_TEST(test_vm_struct) {
    int r = vm_run_and_capture(
        "struct Point {\n"
        "    x: int\n"
        "    y: int\n"
        "}\n"
        "let p = Point { x: 10, y: 20 }\n"
        "print(p.x)\nprint(p.y)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "10\n20\n");
}

AGL_TEST(test_vm_result_match) {
    int r = vm_run_and_capture(
        "let r = ok(42)\n"
        "let v = match r {\n"
        "    ok(n) -> n\n"
        "    err(e) -> 0\n"
        "}\n"
        "print(v)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "42\n");
}

AGL_TEST(test_vm_string_ops) {
    int r = vm_run_and_capture(
        "print(\"hello\" + \" world\")\n"
        "print(\"a\" < \"b\")");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "hello world\ntrue\n");
}

AGL_TEST(test_vm_float) {
    int r = vm_run_and_capture("print(1.5 + 2.5)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "4\n");
}

AGL_TEST(test_vm_stdlib) {
    int r = vm_run_and_capture(
        "print(type(42))\n"
        "print(str(100))\n"
        "print(abs(-5))\n"
        "print(min(3, 7))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "int\n100\n5\n3\n");
}

AGL_TEST(test_vm_map_filter) {
    int r = vm_run_and_capture(
        "let arr = [1, 2, 3, 4]\n"
        "let doubled = map(arr, fn(x: int) -> int { return x * 2 })\n"
        "print(doubled)\n"
        "let evens = filter(arr, fn(x: int) -> bool { return x % 2 == 0 })\n"
        "print(evens)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "[2, 4, 6, 8]\n[2, 4]\n");
}

AGL_TEST(test_vm_fizzbuzz) {
    int r = vm_run_and_capture(
        "var i = 1\n"
        "while i <= 15 {\n"
        "    if i % 15 == 0 { print(\"FizzBuzz\") }\n"
        "    else if i % 3 == 0 { print(\"Fizz\") }\n"
        "    else if i % 5 == 0 { print(\"Buzz\") }\n"
        "    else { print(i) }\n"
        "    i = i + 1\n"
        "}");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output,
        "1\n2\nFizz\n4\nBuzz\nFizz\n7\n8\nFizz\nBuzz\n11\nFizz\n13\n14\nFizzBuzz\n");
}

/* ---- Map type tests ---- */

AGL_TEST(test_vm_map_literal) {
    int r = vm_run_and_capture(
        "let m = {\"name\": \"ago\", \"version\": 1}\n"
        "print(m[\"name\"])");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "ago\n");
}

AGL_TEST(test_vm_map_empty) {
    int r = vm_run_and_capture(
        "let m = {}\n"
        "print(len(m))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "0\n");
}

AGL_TEST(test_vm_map_set) {
    int r = vm_run_and_capture(
        "let m = map_set({}, \"key\", 42)\n"
        "print(m[\"key\"])");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "42\n");
}

AGL_TEST(test_vm_map_keys) {
    int r = vm_run_and_capture(
        "print(map_keys({\"a\": 1, \"b\": 2}))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "[\"a\", \"b\"]\n");
}

AGL_TEST(test_vm_map_has) {
    int r = vm_run_and_capture(
        "print(map_has({\"x\": 1}, \"x\"))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGL_TEST(test_vm_map_nested) {
    int r = vm_run_and_capture(
        "let m = {\"inner\": {\"value\": 99}}\n"
        "print(m[\"inner\"][\"value\"])");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "99\n");
}

/* ---- String builtin tests ---- */

AGL_TEST(test_vm_split) {
    int r = vm_run_and_capture(
        "print(split(\"a,b,c\", \",\"))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "[\"a\", \"b\", \"c\"]\n");
}

AGL_TEST(test_vm_trim) {
    int r = vm_run_and_capture(
        "print(trim(\"  hello  \"))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "hello\n");
}

AGL_TEST(test_vm_contains) {
    int r = vm_run_and_capture(
        "print(contains(\"hello world\", \"world\"))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGL_TEST(test_vm_replace) {
    int r = vm_run_and_capture(
        "print(replace(\"hello world\", \"world\", \"ago\"))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "hello ago\n");
}

AGL_TEST(test_vm_starts_ends) {
    int r = vm_run_and_capture(
        "print(starts_with(\"hello\", \"hel\"))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGL_TEST(test_vm_join) {
    int r = vm_run_and_capture(
        "print(join([\"a\", \"b\", \"c\"], \"-\"))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "a-b-c\n");
}

AGL_TEST(test_vm_substr) {
    int r = vm_run_and_capture(
        "print(substr(\"hello\", 1, 3))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "ell\n");
}

/* ---- Main ---- */

/* ---- JSON & env tests ---- */

AGL_TEST(test_vm_json_parse_object) {
    int r = vm_run_and_capture(
        "let data = match json_parse(\"{\\\"name\\\": \\\"ago\\\", \\\"version\\\": 1}\") {\n"
        "    ok(d) -> d\n"
        "    err(e) -> {}\n"
        "}\n"
        "print(data[\"name\"])\n"
        "print(data[\"version\"])");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "ago\n1\n");
}

AGL_TEST(test_vm_json_parse_array) {
    int r = vm_run_and_capture(
        "let arr = match json_parse(\"[1, 2, 3]\") {\n"
        "    ok(d) -> d\n"
        "    err(e) -> []\n"
        "}\n"
        "print(arr)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "[1, 2, 3]\n");
}

AGL_TEST(test_vm_json_parse_nested) {
    int r = vm_run_and_capture(
        "let data = match json_parse(\"{\\\"a\\\": {\\\"b\\\": [1, 2]}}\") {\n"
        "    ok(d) -> d\n"
        "    err(e) -> {}\n"
        "}\n"
        "print(data[\"a\"][\"b\"][1])");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "2\n");
}

AGL_TEST(test_vm_json_stringify) {
    int r = vm_run_and_capture(
        "let m = {\"name\": \"ago\", \"nums\": [1, 2]}\n"
        "print(json_stringify(m))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "{\"name\":\"ago\",\"nums\":[1,2]}\n");
}

AGL_TEST(test_vm_json_roundtrip) {
    int r = vm_run_and_capture(
        "let original = \"{\\\"x\\\": 42, \\\"y\\\": true, \\\"z\\\": null}\"\n"
        "let parsed = match json_parse(original) {\n"
        "    ok(d) -> d\n"
        "    err(e) -> {}\n"
        "}\n"
        "print(parsed[\"x\"])\n"
        "print(parsed[\"y\"])\n"
        "print(parsed[\"z\"])");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "42\ntrue\nnil\n");
}

AGL_TEST(test_vm_json_parse_error) {
    int r = vm_run_and_capture(
        "let result = json_parse(\"invalid json\")\n"
        "let msg = match result {\n"
        "    ok(d) -> \"ok\"\n"
        "    err(e) -> \"error\"\n"
        "}\n"
        "print(msg)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "error\n");
}

AGL_TEST(test_vm_env) {
    /* HOME should always be set */
    int r = vm_run_and_capture(
        "let h = match env(\"HOME\") {\n"
        "    ok(v) -> \"found\"\n"
        "    err(e) -> \"missing\"\n"
        "}\n"
        "print(h)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "found\n");
}

AGL_TEST(test_vm_env_default) {
    int r = vm_run_and_capture(
        "print(env_default(\"AGL_NONEXISTENT_VAR_12345\", \"fallback\"))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "fallback\n");
}

/* ---- exec tests ---- */

AGL_TEST(test_vm_exec) {
    int r = vm_run_and_capture(
        "let result = exec(\"echo\", [\"hello ago\"])\n"
        "let out = match result {\n"
        "    ok(r) -> r[\"stdout\"]\n"
        "    err(e) -> e\n"
        "}\n"
        "print(trim(out))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "hello ago\n");
}

AGL_TEST(test_vm_exec_status) {
    int r = vm_run_and_capture(
        "let result = exec(\"test\", [\"-f\", \"/nonexistent\"])\n"
        "let status = match result {\n"
        "    ok(r) -> r[\"status\"]\n"
        "    err(e) -> -1\n"
        "}\n"
        "print(status != 0)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGL_TEST(test_vm_exec_stderr) {
    int r = vm_run_and_capture(
        "let result = exec(\"ls\", [\"/nonexistent_path_12345\"])\n"
        "let has_stderr = match result {\n"
        "    ok(r) -> len(r[\"stderr\"]) > 0\n"
        "    err(e) -> false\n"
        "}\n"
        "print(has_stderr)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGL_TEST(test_vm_exec_result_type) {
    int r = vm_run_and_capture(
        "let result = exec(\"echo\", [\"test\"])\n"
        "print(type(result))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "result\n");
}

/* ---- time tests ---- */

AGL_TEST(test_vm_now) {
    int r = vm_run_and_capture(
        "let t = now()\n"
        "print(t > 0)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGL_TEST(test_vm_sleep) {
    int r = vm_run_and_capture(
        "let t1 = now()\n"
        "sleep(10)\n"
        "let t2 = now()\n"
        "print(t2 >= t1)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

AGL_TEST(test_vm_now_type) {
    int r = vm_run_and_capture(
        "print(type(now()))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "int\n");
}

/* ---- HTTP tests ---- */

AGL_TEST(test_vm_http_get) {
    /* Test that http_get returns a result type, even on connection failure.
     * Use a non-routable address to avoid hanging on real network. */
    int r = vm_run_and_capture(
        "let result = http_get(\"http://127.0.0.1:1/test\", {})\n"
        "let t = type(result)\n"
        "print(t)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "result\n");
}

AGL_TEST(test_vm_http_get_error) {
    /* Test that a failed http_get returns an err Result */
    int r = vm_run_and_capture(
        "let result = http_get(\"http://127.0.0.1:1/test\", {})\n"
        "let msg = match result {\n"
        "    ok(r) -> \"ok\"\n"
        "    err(e) -> \"err\"\n"
        "}\n"
        "print(msg)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "err\n");
}

AGL_TEST(test_vm_http_post) {
    /* Test that http_post returns a result type */
    int r = vm_run_and_capture(
        "let result = http_post(\"http://127.0.0.1:1/test\", {}, \"body\")\n"
        "let t = type(result)\n"
        "print(t)");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "result\n");
}

/* ---- break/continue tests ---- */

AGL_TEST(test_vm_break) {
    int r = vm_run_and_capture(
        "var i = 0\n"
        "while i < 10 {\n"
        "    if i == 3 { break }\n"
        "    print(i)\n"
        "    i = i + 1\n"
        "}");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "0\n1\n2\n");
}

AGL_TEST(test_vm_continue) {
    int r = vm_run_and_capture(
        "var i = 0\n"
        "while i < 5 {\n"
        "    i = i + 1\n"
        "    if i % 2 == 0 { continue }\n"
        "    print(i)\n"
        "}");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "1\n3\n5\n");
}

AGL_TEST(test_vm_break_for) {
    int r = vm_run_and_capture(
        "for x in [1, 2, 3, 4, 5] {\n"
        "    if x == 3 { break }\n"
        "    print(x)\n"
        "}");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "1\n2\n");
}

AGL_TEST(test_vm_continue_for) {
    int r = vm_run_and_capture(
        "for x in [1, 2, 3, 4, 5] {\n"
        "    if x % 2 == 0 { continue }\n"
        "    print(x)\n"
        "}");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "1\n3\n5\n");
}

AGL_TEST(test_vm_nested_break) {
    int r = vm_run_and_capture(
        "for x in [1, 2, 3] {\n"
        "    var j = 0\n"
        "    while j < 3 {\n"
        "        if j == 1 { break }\n"
        "        j = j + 1\n"
        "    }\n"
        "    print(x)\n"
        "}");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "1\n2\n3\n");
}

/* ---- forward reference tests ---- */

AGL_TEST(test_vm_forward_ref) {
    int r = vm_run_and_capture(
        "fn a() -> int { return b() }\n"
        "fn b() -> int { return 42 }\n"
        "print(a())");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "42\n");
}

AGL_TEST(test_vm_mutual_recursion) {
    int r = vm_run_and_capture(
        "fn is_even(n: int) -> bool {\n"
        "    if n == 0 { return true }\n"
        "    return is_odd(n - 1)\n"
        "}\n"
        "fn is_odd(n: int) -> bool {\n"
        "    if n == 0 { return false }\n"
        "    return is_even(n - 1)\n"
        "}\n"
        "print(is_even(4))");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "true\n");
}

/* ---- string interpolation tests ---- */

AGL_TEST(test_vm_interpolation_basic) {
    int r = vm_run_and_capture(
        "let name = \"world\"\n"
        "print(f\"hello {name}\")");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "hello world\n");
}

AGL_TEST(test_vm_interpolation_expr) {
    int r = vm_run_and_capture(
        "let x = 10\n"
        "print(f\"x = {x + 1}\")");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "x = 11\n");
}

AGL_TEST(test_vm_interpolation_multiple) {
    int r = vm_run_and_capture(
        "let a = 1\nlet b = 2\n"
        "print(f\"{a} + {b} = {a + b}\")");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "1 + 2 = 3\n");
}

AGL_TEST(test_vm_interpolation_no_expr) {
    int r = vm_run_and_capture("print(f\"plain string\")");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "plain string\n");
}

AGL_TEST(test_vm_interpolation_nested_call) {
    int r = vm_run_and_capture(
        "let arr = [1, 2, 3]\n"
        "print(f\"length is {len(arr)}\")");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "length is 3\n");
}

AGL_TEST(test_vm_interpolation_empty) {
    int r = vm_run_and_capture("print(f\"\")");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "\n");
}

AGL_TEST(test_vm_interpolation_only_expr) {
    int r = vm_run_and_capture(
        "let x = 42\n"
        "print(f\"{x}\")");
    AGL_ASSERT_INT_EQ(ctx, r, 0);
    AGL_ASSERT_STR_EQ(ctx, captured_output, "42\n");
}

int main(void) {
    AglTestCtx ctx = {0, 0};
    printf("\n=== VM Tests ===\n");

    AGL_RUN_TEST(&ctx, test_vm_hello_world);
    AGL_RUN_TEST(&ctx, test_vm_print_integer);
    AGL_RUN_TEST(&ctx, test_vm_print_bool);
    AGL_RUN_TEST(&ctx, test_vm_arithmetic);
    AGL_RUN_TEST(&ctx, test_vm_comparison);
    AGL_RUN_TEST(&ctx, test_vm_let_binding);
    AGL_RUN_TEST(&ctx, test_vm_var_reassign);
    AGL_RUN_TEST(&ctx, test_vm_if_else);
    AGL_RUN_TEST(&ctx, test_vm_while_loop);
    AGL_RUN_TEST(&ctx, test_vm_for_in);
    AGL_RUN_TEST(&ctx, test_vm_function);
    AGL_RUN_TEST(&ctx, test_vm_recursion);
    AGL_RUN_TEST(&ctx, test_vm_closure);
    AGL_RUN_TEST(&ctx, test_vm_array_ops);
    AGL_RUN_TEST(&ctx, test_vm_struct);
    AGL_RUN_TEST(&ctx, test_vm_result_match);
    AGL_RUN_TEST(&ctx, test_vm_string_ops);
    AGL_RUN_TEST(&ctx, test_vm_float);
    AGL_RUN_TEST(&ctx, test_vm_stdlib);
    AGL_RUN_TEST(&ctx, test_vm_map_filter);
    AGL_RUN_TEST(&ctx, test_vm_fizzbuzz);

    /* Map type tests */
    AGL_RUN_TEST(&ctx, test_vm_map_literal);
    AGL_RUN_TEST(&ctx, test_vm_map_empty);
    AGL_RUN_TEST(&ctx, test_vm_map_set);
    AGL_RUN_TEST(&ctx, test_vm_map_keys);
    AGL_RUN_TEST(&ctx, test_vm_map_has);
    AGL_RUN_TEST(&ctx, test_vm_map_nested);

    /* String builtin tests */
    AGL_RUN_TEST(&ctx, test_vm_split);
    AGL_RUN_TEST(&ctx, test_vm_trim);
    AGL_RUN_TEST(&ctx, test_vm_contains);
    AGL_RUN_TEST(&ctx, test_vm_replace);
    AGL_RUN_TEST(&ctx, test_vm_starts_ends);
    AGL_RUN_TEST(&ctx, test_vm_join);
    AGL_RUN_TEST(&ctx, test_vm_substr);

    /* JSON & env tests */
    AGL_RUN_TEST(&ctx, test_vm_json_parse_object);
    AGL_RUN_TEST(&ctx, test_vm_json_parse_array);
    AGL_RUN_TEST(&ctx, test_vm_json_parse_nested);
    AGL_RUN_TEST(&ctx, test_vm_json_stringify);
    AGL_RUN_TEST(&ctx, test_vm_json_roundtrip);
    AGL_RUN_TEST(&ctx, test_vm_json_parse_error);
    AGL_RUN_TEST(&ctx, test_vm_env);
    AGL_RUN_TEST(&ctx, test_vm_env_default);

    /* Process execution tests */
    AGL_RUN_TEST(&ctx, test_vm_exec);
    AGL_RUN_TEST(&ctx, test_vm_exec_status);
    AGL_RUN_TEST(&ctx, test_vm_exec_stderr);
    AGL_RUN_TEST(&ctx, test_vm_exec_result_type);

    /* Time tests */
    AGL_RUN_TEST(&ctx, test_vm_now);
    AGL_RUN_TEST(&ctx, test_vm_sleep);
    AGL_RUN_TEST(&ctx, test_vm_now_type);

    /* HTTP tests */
    AGL_RUN_TEST(&ctx, test_vm_http_get);
    AGL_RUN_TEST(&ctx, test_vm_http_get_error);
    AGL_RUN_TEST(&ctx, test_vm_http_post);

    /* break/continue tests */
    AGL_RUN_TEST(&ctx, test_vm_break);
    AGL_RUN_TEST(&ctx, test_vm_continue);
    AGL_RUN_TEST(&ctx, test_vm_break_for);
    AGL_RUN_TEST(&ctx, test_vm_continue_for);
    AGL_RUN_TEST(&ctx, test_vm_nested_break);

    /* Forward reference tests */
    AGL_RUN_TEST(&ctx, test_vm_forward_ref);
    AGL_RUN_TEST(&ctx, test_vm_mutual_recursion);

    /* String interpolation tests */
    AGL_RUN_TEST(&ctx, test_vm_interpolation_basic);
    AGL_RUN_TEST(&ctx, test_vm_interpolation_expr);
    AGL_RUN_TEST(&ctx, test_vm_interpolation_multiple);
    AGL_RUN_TEST(&ctx, test_vm_interpolation_no_expr);
    AGL_RUN_TEST(&ctx, test_vm_interpolation_nested_call);
    AGL_RUN_TEST(&ctx, test_vm_interpolation_empty);
    AGL_RUN_TEST(&ctx, test_vm_interpolation_only_expr);

    AGL_SUMMARY(&ctx);
}
