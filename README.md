# AGL

### A programming language designed for AI agents.

AGL (Agent Language) is a statically-scoped, dynamically-typed language built in
C11. It compiles to bytecode and runs on a stack-based virtual machine, with
built-in support for JSON, HTTP, process execution, and environment variables --
the primitives an AI agent needs to interact with the outside world.

---

## Features

- **Bytecode VM** -- 46-opcode stack machine with mark-and-sweep GC
- **Maps** -- `{}` literals with bracket access, ideal for JSON-shaped data
- **JSON** -- `json_parse` / `json_stringify` built in, no external dependencies
- **HTTP** -- `http_get` / `http_post` via libcurl
- **Result type** -- `ok`/`err` with `match` expressions and `?` error propagation
- **String interpolation** -- `f"Hello, {name}!"`
- **Closures** -- first-class functions, lambdas, `map`/`filter`
- **Modules** -- file-based `import` system with cycle detection
- **Process execution** -- `exec` for running subprocesses
- **671+ tests** -- AddressSanitizer and UBSan clean

---

## Quick Start

```bash
make                        # build (debug with ASan + UBSan)
./agl examples/hello.agl    # run a program
./agl                       # interactive REPL
```

---

## Hello World

```agl
print("Hello, World!")
```

```
$ ./agl examples/hello.agl
Hello, World!
```

---

## AI Agent Example

Call an LLM API, parse the response, and print the result:

```agl
let api_key = env("ANTHROPIC_API_KEY")?
let url = "https://api.anthropic.com/v1/messages"

let body = json_stringify({
    "model": "claude-sonnet-4-20250514",
    "max_tokens": 256,
    "messages": [
        {"role": "user", "content": "Say hello in one sentence."}
    ]
})

var headers = map_set({}, "content-type", "application/json")
let headers = map_set(headers, "x-api-key", api_key)
let headers = map_set(headers, "anthropic-version", "2023-06-01")

let resp = http_post(url, headers, body)?
let data = json_parse(resp["body"])?
print(data["content"][0]["text"])
```

---

## Language Overview

### Variables

```agl
let name = "AGL"           // immutable binding
var count = 0              // mutable binding
count = count + 1
```

### Functions

```agl
fn greet(name: string) {
    print(f"Hello, {name}!")
}

fn add(a: int, b: int) -> int {
    return a + b
}
```

### Closures and Higher-Order Functions

```agl
let double = fn(x: int) -> int { return x * 2 }
let doubled = map([1, 2, 3], double)   // [2, 4, 6]

let evens = filter([1, 2, 3, 4], fn(x: int) -> bool {
    return x % 2 == 0
})
// [2, 4]
```

### Control Flow

```agl
if x > 0 {
    print("positive")
} else if x == 0 {
    print("zero")
} else {
    print("negative")
}

for item in [10, 20, 30] {
    print(item)
}

var i = 0
while i < 10 {
    i = i + 1
}
```

### Result Type and Error Handling

```agl
fn safe_div(a: int, b: int) -> result {
    if b == 0 {
        return err("division by zero")
    }
    return ok(a / b)
}

// Pattern matching
match safe_div(10, 0) {
    ok(v) -> print(v)
    err(e) -> print(e)          // division by zero
}

// Error propagation with ?
let value = safe_div(10, 3)?    // unwraps ok, propagates err
```

### Maps

```agl
let config = {"host": "localhost", "port": 8080}
let host = config["host"]
let keys = map_keys(config)             // ["host", "port"]
let updated = map_set(config, "debug", true)
```

### String Interpolation

```agl
let name = "world"
print(f"Hello, {name}!")
print(f"2 + 2 = {2 + 2}")
```

### Structs

```agl
struct Point {
    x: int
    y: int
}

let p = Point { x: 10, y: 20 }
print(p.x + p.y)               // 30
```

---

## Standard Library

40 built-in functions grouped by category.

### I/O

| Function | Description |
|----------|-------------|
| `print(args...)` | Print values to stdout with newline |

### Type Inspection and Conversion

| Function | Description |
|----------|-------------|
| `type(val)` | Type name as string (`"int"`, `"string"`, ...) |
| `len(x)` | Length of string or array |
| `str(val)` | Convert any value to string |
| `int(x)` | Convert string or float to integer |
| `float(x)` | Convert string or int to float |

### Arrays

| Function | Description |
|----------|-------------|
| `push(arr, val)` | New array with val appended |
| `map(arr, fn)` | Apply fn to each element, return new array |
| `filter(arr, fn)` | Keep elements where fn returns truthy |

### Math

| Function | Description |
|----------|-------------|
| `abs(n)` | Absolute value |
| `min(a, b)` | Minimum of two numbers |
| `max(a, b)` | Maximum of two numbers |

### Strings

| Function | Description |
|----------|-------------|
| `split(s, sep)` | Split string into array by separator |
| `trim(s)` | Remove leading/trailing whitespace |
| `contains(s, sub)` | Check if string contains substring |
| `replace(s, old, new)` | Replace all occurrences |
| `starts_with(s, prefix)` | Check string prefix |
| `ends_with(s, suffix)` | Check string suffix |
| `to_upper(s)` | Convert to uppercase |
| `to_lower(s)` | Convert to lowercase |
| `join(arr, sep)` | Join array elements into string |
| `substr(s, start, len)` | Extract substring |
| `count(s, sub)` | Count occurrences of substring |

### Maps

| Function | Description |
|----------|-------------|
| `map_get(m, key)` | Get value by key |
| `map_set(m, key, val)` | New map with key set |
| `map_keys(m)` | Array of all keys |
| `map_has(m, key)` | Check if key exists |
| `map_del(m, key)` | New map with key removed |

### JSON

| Function | Description |
|----------|-------------|
| `json_parse(s)` | Parse JSON string into AGL value (result) |
| `json_stringify(val)` | Serialize AGL value to JSON string |

### Environment

| Function | Description |
|----------|-------------|
| `env(name)` | Get environment variable (result) |
| `env_default(name, fallback)` | Get env var with fallback value |

### HTTP

| Function | Description |
|----------|-------------|
| `http_get(url, headers)` | HTTP GET request (result) |
| `http_post(url, headers, body)` | HTTP POST request (result) |

### File I/O

| Function | Description |
|----------|-------------|
| `read_file(path)` | Read file contents (result) |
| `write_file(path, content)` | Write string to file (result) |
| `file_exists(path)` | Check if file exists |

### Process

| Function | Description |
|----------|-------------|
| `exec(cmd, args)` | Execute subprocess (result) |

### Time

| Function | Description |
|----------|-------------|
| `now()` | Current time in milliseconds (epoch) |
| `sleep(ms)` | Pause execution for ms milliseconds |

### Result Constructors

| Form | Description |
|------|-------------|
| `ok(val)` | Wrap value as success |
| `err(val)` | Wrap value as error |

---

## Architecture

```
Source (.agl)
    |
  Lexer         Tokenize with Go-style auto-semicolon insertion
    |
  Parser        Recursive-descent parser producing an AST
    |
  Sema          Semantic analysis -- scope checks and validation
    |
  Compiler      AST to bytecode (46 opcodes)
    |
  VM            Stack-based execution with mark-and-sweep GC
```

The compiler and runtime share no mutable state. AST nodes and tokens are
arena-allocated; runtime heap objects are tracked by the garbage collector.

---

## Building

### Requirements

- **C11 compiler** -- Clang or GCC
- **POSIX environment** -- macOS or Linux
- **libcurl** (optional) -- enables `http_get` and `http_post`

If libcurl is not installed, AGL builds without HTTP support and the HTTP
builtins return a runtime error.

### Make Targets

| Target | Description |
|--------|-------------|
| `make` | Build the `agl` binary (debug, ASan + UBSan enabled) |
| `make test` | Run all 671+ tests |
| `make clean` | Remove build artifacts |

---

## Testing

```bash
make test
```

Runs 671+ tests across six modules:

| Module | Coverage |
|--------|----------|
| Lexer | Tokenization, auto-semicolons, edge cases |
| Parser | AST construction, error recovery |
| Sema | Scope analysis, semantic validation |
| Interpreter | Tree-walk execution (reference implementation) |
| GC | Mark-and-sweep correctness, cycle handling |
| VM | Bytecode execution, builtins, all language features |

All tests run under AddressSanitizer and UndefinedBehaviorSanitizer.

---

## Documentation

| Document | Description |
|----------|-------------|
| [Language Specification](docs/spec.md) | Complete grammar, types, and semantics |
| [Getting Started Tutorial](docs/tutorial.md) | Build, run, and write your first programs |
| [Standard Library Reference](docs/stdlib.md) | Full API for all built-in functions |
| [Error Catalog](docs/errors.md) | Error codes, messages, and troubleshooting |

---

## License

MIT License. See [LICENSE](LICENSE) for details.
