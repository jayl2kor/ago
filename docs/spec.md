# Agl Language Specification

## 1. Lexical Elements

### Comments

Line comments start with `//` and extend to the end of the line.

```ago
// This is a comment
let x = 42  // inline comment
```

There are no block comments.

### Identifiers

Identifiers start with a letter or underscore, followed by letters, digits, or underscores.

```
identifier = [a-zA-Z_][a-zA-Z0-9_]*
```

### Keywords

```
break  continue  else  err   false  fn     for
if     import    in    let   match  ok     return
struct true      var   while
```

### Literals

| Type    | Examples                     |
|---------|------------------------------|
| Integer | `0`, `42`, `1000`            |
| Float   | `3.14`, `0.5`                |
| String  | `"hello"`, `"line\nbreak"`   |
| Boolean | `true`, `false`              |

Integer literals are decimal digits. Float literals require a digit on both sides of the decimal point (`0.5`, not `.5`).

String literals are enclosed in double quotes. Escape sequences are supported with backslash (e.g., `\n`, `\\`, `\"`). Strings cannot span multiple lines.

### Operators and Delimiters

**Operators:**

```
+   -   *   /   %
=   ==  !=  <   >   <=  >=
&&  ||  !
->  .
```

**Delimiters:**

```
(  )  {  }  [  ]  ,  :
```

### Auto-Semicolon Insertion

Agl uses Go-style automatic semicolon insertion. A newline acts as a statement terminator when the preceding token is one of:

- Identifier, literal (`int`, `float`, `string`, `true`, `false`)
- `break`, `continue`, `return`
- `)`, `]`, `}`

Newlines inside parentheses `()`, brackets `[]`, or braces `{}` at nonzero nesting depth are suppressed (do not act as terminators). This allows multi-line expressions and argument lists.

```ago
let result = foo(
    arg1,
    arg2
)  // newline terminates here, after )
```

## 2. Types

| Type     | Description                        | Truthy when             |
|----------|------------------------------------|-------------------------|
| `int`    | 64-bit signed integer              | nonzero                 |
| `float`  | 64-bit double-precision float      | nonzero                 |
| `bool`   | `true` or `false`                  | `true`                  |
| `string` | Immutable byte sequence            | non-empty               |
| `array`  | Ordered collection of values       | non-empty               |
| `struct` | Named record with typed fields     | always truthy           |
| `result` | Tagged union: `ok(val)` or `err(val)` | `is_ok`              |
| `fn`     | Function value (named or lambda)   | always truthy           |
| `nil`    | Absence of value                   | always falsy            |

### Numeric Promotion

When an `int` and `float` are operands to an arithmetic or comparison operator, the `int` is promoted to `float` and the result is `float`.

## 3. Variables

### Immutable Binding (`let`)

```ago
let name = "Ago"
let pi = 3.14
```

`let` bindings cannot be reassigned. Attempting to assign to a `let` variable produces an error.

### Mutable Binding (`var`)

```ago
var count = 0
count = count + 1
```

`var` bindings can be reassigned with `=`.

### Optional Type Annotation

```ago
let x: int = 42
var name: string = "hello"
```

Type annotations are syntactically accepted but not enforced at compile time. Types are checked dynamically at runtime.

## 4. Expressions

### Arithmetic

```ago
1 + 2       // 3
10 - 3      // 7
4 * 5       // 20
10 / 3      // 3 (integer division)
10 % 3      // 1
2.0 * 3.0   // 6.0
1 + 2.5     // 3.5 (int promoted to float)
```

Integer division by zero is a runtime error. Float division by zero produces infinity.

### Comparison

```ago
1 == 1      // true
1 != 2      // true
3 < 5       // true
5 > 3       // true
3 <= 3      // true
4 >= 5      // false
```

Comparison operators work on `int`, `float`, and `string` values. String comparisons are lexicographic (byte-by-byte).

### Logical

```ago
true && false   // false
true || false   // true
!true           // false
```

`&&` and `||` operate on `bool` values only. `!` is a unary prefix operator.

### String Operations

```ago
"hello" + " " + "world"    // concatenation
"abc" == "abc"              // equality
"abc" < "abd"               // lexicographic comparison
"abc" <= "abc"              // true
len("hello")                // 5
```

The `+` operator concatenates strings. All six comparison operators (`==`, `!=`, `<`, `>`, `<=`, `>=`) work on strings.

### Unary

```ago
-42         // negation (int or float)
!true       // logical not (bool only)
```

### Array Literal and Index

```ago
let nums = [1, 2, 3, 4, 5]
nums[0]     // 1
nums[4]     // 5
```

Array indices must be integers. Out-of-bounds access is a runtime error.

### Struct Literal and Field Access

```ago
struct Point {
    x: int
    y: int
}

let p = Point { x: 10, y: 20 }
p.x     // 10
p.y     // 20
```

Field access uses dot notation. Accessing a nonexistent field is a runtime error.

### Grouped Expressions

Parentheses override precedence:

```ago
(1 + 2) * 3    // 9
```

## 5. Statements

### Variable Declaration

```ago
let x = 42
var y = 0
```

### Assignment

```ago
y = y + 1
```

Only `var` variables can be assigned. Only simple identifiers are valid assignment targets.

### `if` / `else`

```ago
if x > 0 {
    print("positive")
} else if x == 0 {
    print("zero")
} else {
    print("negative")
}
```

Braces are required. There is no ternary operator.

### `while`

```ago
var i = 0
while i < 10 {
    print(i)
    i = i + 1
}
```

### `for`-`in`

Iterates over array elements:

```ago
let items = [10, 20, 30]
for item in items {
    print(item)
}
```

The loop variable is scoped to the loop body and is not mutable.

### `return`

Returns a value from the enclosing function:

```ago
fn add(a: int, b: int) -> int {
    return a + b
}
```

Bare `return` (without a value) returns `nil`. `return` at the top level is permitted.

### `break` / `continue`

Reserved keywords recognized by the lexer. (Loop control within `while` and `for`.)

## 6. Functions

### Declaration

```ago
fn greet(name: string) {
    print("Hello, " + name)
}

fn add(a: int, b: int) -> int {
    return a + b
}
```

Parameters require type annotations: `name: type`. The return type follows `->` and is optional (defaults to `nil`/void).

Functions are first-class values and are bound with `let` (immutable).

### Calling

```ago
greet("world")
let sum = add(1, 2)
```

Argument count must match parameter count at runtime. Maximum 64 arguments per call.

### Recursion

Functions can call themselves. Maximum call depth is 512.

## 7. Lambdas / Closures

Anonymous functions use the `fn` keyword without a name:

```ago
let double = fn(x: int) -> int {
    return x * 2
}
double(5)   // 10
```

### Closures

Lambdas capture the enclosing environment by value (snapshot at creation time):

```ago
fn make_counter(start: int) -> fn {
    var n = start
    return fn() -> int {
        return n
    }
}
let count = make_counter(10)
count()     // 10
```

The return type for functions returning a closure is `fn`.

### As Arguments

Lambdas are commonly passed to higher-order functions:

```ago
let doubled = map([1, 2, 3], fn(x: int) -> int { return x * 2 })
// [2, 4, 6]
```

## 8. Result Type

The `result` type represents success or failure.

### Constructors

```ago
ok(42)              // success with value 42
err("not found")    // error with message
```

### Match Expression

Pattern match on a result value:

```ago
fn safe_div(a: int, b: int) -> result {
    if b == 0 {
        return err("division by zero")
    }
    return ok(a / b)
}

match safe_div(10, 3) {
    ok(v) -> print(v)
    err(e) -> print(e)
}
```

Both `ok` and `err` arms are required. Each arm binds the inner value to a name. The arms can appear in any order. The match body is an expression (not a block).

### Truthiness

A `result` value is truthy if it is `ok`, falsy if `err`.

## 9. Module System

### Import

```ago
import "math"       // imports math.agl from same directory
import "lib/utils"  // imports lib/utils.agl relative to current file
```

The import path is a string literal (without the `.agl` extension). Paths are resolved relative to the importing file's directory.

**Restrictions:**
- Path traversal (`..`) is rejected.
- The resolved path must stay within the base directory.
- Circular imports are prevented (each module loaded once).
- Maximum 64 modules.

Imported modules execute in the same environment -- their top-level `fn` and variable declarations become available to the importer.

## 10. Operator Precedence

From lowest to highest:

| Precedence | Operators          | Associativity | Description     |
|------------|--------------------|---------------|-----------------|
| 1          | `=`                | right         | Assignment      |
| 2          | `\|\|`             | left          | Logical OR      |
| 3          | `&&`               | left          | Logical AND     |
| 4          | `==` `!=`          | left          | Equality        |
| 5          | `<` `>` `<=` `>=`  | left          | Comparison      |
| 6          | `+` `-`            | left          | Addition        |
| 7          | `*` `/` `%`        | left          | Multiplication  |
| 8          | `!` `-` (prefix)   | right         | Unary           |
| 9          | `()` `.` `[]`      | left          | Call / Access   |

Assignment is parsed as a statement (not an expression), so it does not participate in the Pratt precedence table. All binary operators are left-associative.

## 11. Built-in Functions

| Function                      | Signature                          | Description                         |
|-------------------------------|------------------------------------|-------------------------------------|
| `print(args...)`              | variadic                           | Print values to stdout, one per call |
| `len(x)`                      | `(string\|array) -> int`           | Length of string or array           |
| `type(x)`                     | `(any) -> string`                  | Type name as string                 |
| `str(x)`                      | `(any) -> string`                  | Convert to string                   |
| `int(x)`                      | `(string\|float\|int) -> int`      | Convert to integer                  |
| `float(x)`                    | `(string\|int\|float) -> float`    | Convert to float                    |
| `push(arr, val)`              | `(array, any) -> array`            | Return new array with val appended  |
| `map(arr, fn)`                | `(array, fn) -> array`             | Apply fn to each element            |
| `filter(arr, fn)`             | `(array, fn) -> array`             | Keep elements where fn returns true |
| `abs(n)`                      | `(int\|float) -> int\|float`       | Absolute value                      |
| `min(a, b)`                   | `(number, number) -> number`       | Minimum of two same-typed numbers   |
| `max(a, b)`                   | `(number, number) -> number`       | Maximum of two same-typed numbers   |
| `read_file(path)`             | `(string) -> result`               | Read file contents (max 10MB)       |
| `write_file(path, content)`   | `(string, string) -> result`       | Write string to file                |
| `file_exists(path)`           | `(string) -> bool`                 | Check if file exists                |

`push`, `map`, and `filter` return new arrays (immutable semantics).

## 12. Grammar Summary (EBNF)

```ebnf
program     = { statement } ;

statement   = let_stmt | var_stmt | assign_stmt | return_stmt
            | if_stmt | while_stmt | for_stmt
            | fn_decl | struct_decl | import_stmt
            | expr_stmt ;

let_stmt    = "let" IDENT [ ":" type ] "=" expression NEWLINE ;
var_stmt    = "var" IDENT [ ":" type ] "=" expression NEWLINE ;
assign_stmt = IDENT "=" expression NEWLINE ;
return_stmt = "return" [ expression ] NEWLINE ;
if_stmt     = "if" expression block [ "else" ( if_stmt | block ) ] ;
while_stmt  = "while" expression block ;
for_stmt    = "for" IDENT "in" expression block ;
fn_decl     = "fn" IDENT "(" params ")" [ "->" type ] block ;
struct_decl = "struct" IDENT "{" { IDENT ":" type NEWLINE } "}" ;
import_stmt = "import" STRING NEWLINE ;
expr_stmt   = expression NEWLINE ;

block       = "{" { statement } "}" ;

expression  = unary | binary | call | index | field_access
            | match_expr | lambda | ok_expr | err_expr
            | literal | IDENT | "(" expression ")" ;

literal     = INT | FLOAT | STRING | "true" | "false"
            | array_lit | struct_lit ;
array_lit   = "[" [ expression { "," expression } ] "]" ;
struct_lit  = IDENT "{" [ IDENT ":" expression { "," IDENT ":" expression } ] "}" ;

lambda      = "fn" "(" params ")" [ "->" type ] block ;
ok_expr     = "ok" "(" expression ")" ;
err_expr    = "err" "(" expression ")" ;
match_expr  = "match" expression "{" ok_arm err_arm "}" ;
ok_arm      = "ok" "(" IDENT ")" "->" expression NEWLINE ;
err_arm     = "err" "(" IDENT ")" "->" expression NEWLINE ;

params      = [ IDENT ":" type { "," IDENT ":" type } ] ;
type        = IDENT | "fn" ;
```
