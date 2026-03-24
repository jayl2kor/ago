# Getting Started with Ago

Agl is a medium-level programming language designed for AI agents. This tutorial
walks you through the basics, from building the interpreter to writing real programs.

## 1. Building Ago

Agl is written in C11 with no external dependencies. Build it with `make`:

```bash
git clone <repo-url>
cd ago
make
```

This produces the `ago` executable in the project root. To verify:

```bash
./agl --version
```

To clean build artifacts and rebuild from scratch:

```bash
make clean
make
```

## 2. Running Programs

Save your code in a `.agl` file and pass it to the interpreter:

```bash
./agl examples/hello.agl
```

## 3. The REPL

Launch an interactive session by running `ago` with no arguments:

```bash
./agl
```

```
ago 0.1.0 — interactive REPL
Type expressions or statements. Ctrl+D to exit.

agl> print("Hello from the REPL!")
Hello from the REPL!
agl> let x = 10 + 20
agl> print(x)
30
```

Multi-line input is supported. The REPL waits for matching braces before
executing:

```
agl> fn square(n: int) -> int {
...>     return n * n
...> }
agl> print(square(7))
49
```

Press `Ctrl+D` to exit.

## 4. Hello World

Create a file called `hello.agl`:

```ago
print("Hello, world!")
```

Run it:

```bash
./agl hello.agl
```

Output:

```
Hello, world!
```

`print` is a built-in function that accepts any value type.

## 5. Variables: let vs var

Agl has two ways to declare variables:

- `let` creates an **immutable** binding (cannot be reassigned).
- `var` creates a **mutable** binding (can be reassigned).

```ago
let name = "Ago"
var counter = 0

// name = "Other"   // ERROR: cannot assign to immutable variable
counter = counter + 1  // OK
print(counter)         // 1
```

Prefer `let` by default. Use `var` only when you need to reassign.

## 6. Types

Agl has these built-in types:

| Type     | Examples               |
|----------|------------------------|
| `int`    | `42`, `-7`, `0`        |
| `float`  | `3.14`, `-0.5`, `1.0`  |
| `bool`   | `true`, `false`        |
| `string` | `"hello"`, `""`        |

```ago
let age = 30           // int
let pi = 3.14159       // float
let active = true      // bool
let greeting = "hi"    // string

// Type introspection
print(type(age))       // int
print(type(pi))        // float
print(type(active))    // bool
print(type(greeting))  // string

// Type conversions
let n = int("123")     // string -> int
let f = float("2.5")   // string -> float
let s = str(42)        // int -> string
print(s + " is a number")  // "42 is a number"
```

### Operators

Arithmetic: `+`, `-`, `*`, `/`, `%`

Comparison: `==`, `!=`, `<`, `>`, `<=`, `>=`

Logical: `&&`, `||`, `!`

String concatenation uses `+`:

```ago
let full = "Hello" + " " + "World"
print(full)     // Hello World
print(len(full))  // 11
```

## 7. Arrays and For Loops

Arrays are ordered collections of values created with square brackets:

```ago
let fruits = ["apple", "banana", "cherry"]
print(fruits)       // [apple, banana, cherry]
print(fruits[0])    // apple
print(len(fruits))  // 3
```

Iterate over arrays with `for-in`:

```ago
let nums = [10, 20, 30, 40, 50]
var total = 0
for n in nums {
    total = total + n
}
print(total)  // 150
```

Arrays are immutable. `push` returns a **new** array with the element appended:

```ago
let a = [1, 2, 3]
let b = push(a, 4)
print(a)  // [1, 2, 3]   (unchanged)
print(b)  // [1, 2, 3, 4]
```

### Functional Array Operations

`map` and `filter` take an array and a function, returning a new array:

```ago
let nums = [1, 2, 3, 4, 5]

let doubled = map(nums, fn(x: int) -> int { return x * 2 })
print(doubled)  // [2, 4, 6, 8, 10]

let evens = filter(nums, fn(x: int) -> bool { return x % 2 == 0 })
print(evens)  // [2, 4]
```

## 8. Functions

Define functions with the `fn` keyword. Parameters require type annotations.
Use `->` to declare the return type:

```ago
fn greet(name: string) {
    print("Hello, " + name + "!")
}

fn add(a: int, b: int) -> int {
    return a + b
}

greet("World")        // Hello, World!
print(add(3, 4))      // 7
```

### Control Flow

`if`/`else` and `while` work as expected:

```ago
fn fizzbuzz(n: int) {
    var i = 1
    while i <= n {
        if i % 15 == 0 {
            print("FizzBuzz")
        } else if i % 3 == 0 {
            print("Fizz")
        } else if i % 5 == 0 {
            print("Buzz")
        } else {
            print(i)
        }
        i = i + 1
    }
}

fizzbuzz(20)
```

## 9. Structs

Define a struct type with `struct`, then create instances with struct literals:

```ago
struct Point {
    x: int
    y: int
}

let p = Point { x: 10, y: 20 }
print(p.x)     // 10
print(p.y)     // 20
```

Structs are useful for grouping related data:

```ago
struct User {
    name: string
    age: int
    active: bool
}

let alice = User { name: "Alice", age: 30, active: true }
print(alice.name)    // Alice
print(alice.active)  // true
```

## 10. Error Handling with Result and match

Agl uses `Result` types instead of exceptions. Functions return `ok(value)`
on success or `err(message)` on failure. Use `match` to handle both cases:

```ago
fn safe_divide(a: int, b: int) -> result {
    if b == 0 {
        return err("division by zero")
    }
    return ok(a / b)
}

match safe_divide(10, 3) {
    ok(v) -> print(v)       // 3
    err(e) -> print(e)
}

match safe_divide(10, 0) {
    ok(v) -> print(v)
    err(e) -> print(e)      // division by zero
}
```

This pattern makes error handling explicit and impossible to forget. Every
`result` must be matched before its value can be used.

## 11. Lambdas and Closures

Anonymous functions (lambdas) are created with `fn(...) { ... }`.
They capture variables from their enclosing scope (closures):

```ago
fn make_adder(n: int) -> fn {
    return fn(x: int) -> int {
        return x + n
    }
}

let add10 = make_adder(10)
print(add10(5))    // 15
print(add10(20))   // 30
```

Lambdas work as first-class values. Pass them to higher-order functions:

```ago
let nums = [1, 2, 3, 4, 5]

let squares = map(nums, fn(x: int) -> int {
    return x * x
})
print(squares)  // [1, 4, 9, 16, 25]
```

## 12. File I/O

Agl provides built-in functions for file operations. They return `result`
types so errors are handled explicitly:

### Writing a File

```ago
match write_file("output.txt", "Hello from Ago!") {
    ok(v) -> print("File written successfully")
    err(e) -> print("Write failed: " + e)
}
```

### Reading a File

```ago
match read_file("output.txt") {
    ok(content) -> print(content)
    err(e) -> print("Read failed: " + e)
}
```

### Checking if a File Exists

```ago
if file_exists("output.txt") {
    print("File exists")
} else {
    print("File not found")
}
```

| Function                          | Returns              | Description            |
|-----------------------------------|----------------------|------------------------|
| `read_file(path)`                 | `result<string>`     | Read entire file       |
| `write_file(path, content)`       | `result<bool>`       | Write string to file   |
| `file_exists(path)`               | `bool`               | Check file existence   |

## 13. Modules and Imports

Split your code into multiple files with `import`. Given this project layout:

```
project/
  main.agl
  math.agl
```

Define shared functions in `math.agl`:

```ago
// math.agl

fn square(n: int) -> int {
    return n * n
}

fn clamp(val: int, lo: int, hi: int) -> int {
    if val < lo {
        return lo
    }
    if val > hi {
        return hi
    }
    return val
}
```

Import and use them in `main.agl`:

```ago
// main.agl

import "math"

print(square(5))        // 25
print(clamp(15, 0, 10)) // 10
```

Run the program from the project directory:

```bash
./agl project/main.agl
```

Key rules for imports:
- The `.agl` extension is added automatically -- write `import "math"`, not `import "math.agl"`.
- Paths are relative to the importing file's directory.
- Circular imports are handled safely (each module loads at most once).
- Directory traversal with `..` is not allowed for security.
- Imported functions and variables are available in the importing file's scope.

## Built-in Functions Reference

| Function          | Description                          |
|-------------------|--------------------------------------|
| `print(args...)`  | Print values to stdout               |
| `len(x)`          | Length of an array or string          |
| `type(x)`         | Type name as a string                |
| `str(x)`          | Convert any value to a string        |
| `int(x)`          | Convert string or float to int       |
| `float(x)`        | Convert string or int to float       |
| `push(arr, val)`  | Return new array with val appended   |
| `map(arr, fn)`    | Apply fn to each element             |
| `filter(arr, fn)` | Keep elements where fn returns true  |
| `abs(n)`          | Absolute value                       |
| `min(a, b)`       | Minimum of two numbers               |
| `max(a, b)`       | Maximum of two numbers               |
| `read_file(path)` | Read file contents (returns result)  |
| `write_file(p,c)` | Write string to file (returns result)|
| `file_exists(p)`  | Check if file exists (returns bool)  |
