# Agl Error Catalog

## Error Output Format

Errors are printed to stderr in the following format:

```
file:line:col: error: message
```

When no file context is available (e.g., runtime errors):

```
error: message
```

### Stack Traces

Errors occurring inside function calls include a stack trace, printed immediately after the error line. Frames are listed innermost-first (most recent call at the top):

```
error: division by zero
  in inner() (line 5)
  in outer() (line 10)
```

Anonymous functions (lambdas) appear as `<lambda>`:

```
error: index 5 out of bounds (length 3)
  in <lambda> (line 8)
  in process() (line 12)
```

Maximum trace depth is 16 frames.

---

## Error Categories

### AGL_ERR_SYNTAX -- Syntax Errors

Produced by the lexer and parser during source analysis.

#### Lexer Errors

| Message | Cause | Fix |
|---------|-------|-----|
| `unterminated string literal` | String opened with `"` but never closed, or newline inside string. | Close the string with `"` on the same line. |
| `unexpected character` | Unrecognized character in source. | Remove the character or replace with valid syntax. |
| `unexpected character '&', did you mean '&&'?` | Single `&` used instead of logical AND. | Use `&&` for logical AND. |
| `unexpected character '\|', did you mean '\|\|'?` | Single `\|` used instead of logical OR. | Use `\|\|` for logical OR. |

#### Parser Errors

| Message | Cause | Fix |
|---------|-------|-----|
| `expected TYPE, got TOKEN` | A required token was not found. Examples: `expected ')'`, `expected '{'`, `expected ':'`. | Add the missing token at the indicated location. |
| `expected type name, got TOKEN` | A type annotation position has a non-type token. | Provide a valid type name (`int`, `string`, `fn`, etc.). |
| `expected parameter name` | Non-identifier in function parameter list. | Use an identifier for the parameter name. |
| `expected variable name` | `let` or `var` not followed by an identifier. | Provide a variable name after `let`/`var`. |
| `expected function name` | `fn` at top level not followed by an identifier. | Provide a function name after `fn`. |
| `expected struct name` | `struct` not followed by an identifier. | Provide a struct name after `struct`. |
| `expected field name` | Non-identifier inside struct literal or declaration. | Use an identifier for field names. |
| `expected field name after '.'` | Dot operator not followed by an identifier. | Provide a field name after `.`. |
| `expected module path after 'import'` | `import` not followed by a string literal. | Use `import "path"` syntax. |
| `expected binding name` | Match arm `ok(...)` or `err(...)` missing an identifier. | Provide a binding name: `ok(v) ->`. |
| `expected 'ok' or 'err' arm in match` | Match expression body contains something other than ok/err arms. | Ensure match has exactly one `ok` and one `err` arm. |
| `duplicate 'ok' arm in match` | Two `ok` arms in the same match expression. | Remove the duplicate arm. |
| `duplicate 'err' arm in match` | Two `err` arms in the same match expression. | Remove the duplicate arm. |
| `invalid assignment target` | Left side of `=` is not a simple identifier. | Assign only to variable names (not expressions). |
| `unexpected token 'TOKEN'` | Token found where an expression was expected. | Check for missing operands or misplaced keywords. |
| `too many parameters (max 64)` | Function declaration exceeds parameter limit. | Reduce the number of parameters. |
| `too many arguments (max 128)` | Function call exceeds argument limit. | Reduce the number of arguments. |
| `too many array elements (max 128)` | Array literal exceeds element limit. | Split into multiple arrays. |
| `too many struct fields (max 64)` | Struct declaration or literal exceeds field limit. | Reduce the number of fields. |
| `too many statements in block (max 256)` | Block contains too many statements. | Extract code into separate functions. |
| `too many top-level declarations (max 512)` | Source file exceeds declaration limit. | Split into multiple modules. |

---

### AGL_ERR_NAME -- Name Errors

Produced by semantic analysis and the interpreter for undefined or duplicate names.

| Message | Cause | Fix |
|---------|-------|-----|
| `undefined variable 'x'` | Variable `x` used but never declared. | Declare with `let` or `var` before use, or fix the spelling. |
| `unknown function 'foo'` | Called `foo()` but no function with that name exists. | Define the function or check the name spelling. |
| `no field 'f'` | Accessed `.f` on a struct that has no field named `f`. | Check the struct definition for valid field names. |

---

### AGL_ERR_TYPE -- Type Errors

Produced by semantic analysis and the interpreter for type mismatches.

| Message | Cause | Fix |
|---------|-------|-----|
| `cannot assign to immutable variable 'x'` | Assignment to a `let`-bound variable. | Change the declaration to `var` if mutation is needed. |
| `invalid unary operator` | Unary `-` on non-number, or `!` on non-bool. | Apply `-` to `int`/`float` and `!` to `bool` only. |
| `invalid binary operation` | Binary operator applied to incompatible types. | Ensure both operands have compatible types for the operator. |
| `cannot access field on non-struct value` | Dot access on a value that is not a struct. | Only use `.field` on struct values. |
| `cannot index non-array value` | Index `[n]` on a value that is not an array. | Only use `[index]` on array values. |
| `array index must be an integer` | Non-integer used as array index. | Use an `int` value as the index. |
| `match requires a result value` | `match` applied to a non-result value. | Only match on values produced by `ok()` or `err()`. |
| `expression is not callable` | Attempted to call a non-function value with `()`. | Ensure the callee is a function. |
| `for-in requires an array` | `for x in val` where `val` is not an array. | Only iterate over array values. |
| `expected N arguments, got M` | Function call arity mismatch (detected by sema). | Pass the correct number of arguments. |
| `len() requires an array or string` | `len()` called on an incompatible type. | Pass an array or string to `len()`. |
| `int() cannot convert this type` | `int()` called on a non-convertible type. | Pass a `string`, `float`, or `int`. |
| `float() cannot convert this type` | `float()` called on a non-convertible type. | Pass a `string`, `int`, or `float`. |
| `abs() requires a number` | `abs()` called on a non-numeric value. | Pass an `int` or `float`. |
| `min() requires two numbers of the same type` | `min()` called with mismatched or non-numeric types. | Pass two `int` values or two `float` values. |
| `max() requires two numbers of the same type` | `max()` called with mismatched or non-numeric types. | Pass two `int` values or two `float` values. |
| `push() first argument must be an array` | `push()` called with non-array first argument. | Pass an array as the first argument. |
| `map() requires (array, fn)` | `map()` called with wrong argument types. | Pass `(array, fn)`. |
| `filter() requires (array, fn)` | `filter()` called with wrong argument types. | Pass `(array, fn)`. |
| `read_file() requires a string path` | `read_file()` called with non-string argument. | Pass a string path. |
| `file_exists() requires a string path` | `file_exists()` called with non-string argument. | Pass a string path. |
| `write_file() requires (string, string)` | `write_file()` called with wrong argument types. | Pass `(path_string, content_string)`. |

---

### AGL_ERR_RUNTIME -- Runtime Errors

Produced by the interpreter during execution.

| Message | Cause | Fix |
|---------|-------|-----|
| `division by zero` | Integer division or modulo by zero. | Check the divisor before dividing. |
| `index N out of bounds (length M)` | Array index `N` is negative or >= `M`. | Ensure `0 <= index < len(array)`. |
| `cannot assign to immutable variable 'x'` | Runtime assignment to a `let` binding. | Change to `var` if mutation is needed. |
| `expected N arguments, got M` | Function called with wrong number of arguments. | Match the function's parameter count. |
| `maximum call depth exceeded (limit 512)` | Infinite recursion or deep call chain. | Add a base case to recursive functions. |
| `too many variables (max 256)` | Environment exceeded variable slot limit. | Reduce variable count or restructure code. |
| `too many modules (max 64)` | Import limit exceeded. | Reduce the number of imported modules. |
| `string too large` | String concatenation result exceeds size limit. | Reduce string sizes. |
| `out of memory` | Arena or heap allocation failed. | Reduce program memory usage. |
| `array size limit exceeded (max 1024)` | `push()` would exceed maximum array size. | Use smaller arrays or restructure data. |
| `int() invalid integer string` | `int("abc")` -- string cannot be parsed as integer. | Pass a valid numeric string. |
| `float() invalid number string` | `float("abc")` -- string cannot be parsed as float. | Pass a valid numeric string. |
| `unsupported expression type` | Internal: AST node not handled by evaluator. | Report as a bug. |
| `unsupported statement type` | Internal: AST node not handled by executor. | Report as a bug. |

#### Built-in Arity Errors

Each built-in function validates its argument count:

| Message | Fix |
|---------|-----|
| `len() takes exactly 1 argument` | Pass exactly 1 argument. |
| `type() takes exactly 1 argument` | Pass exactly 1 argument. |
| `str() takes exactly 1 argument` | Pass exactly 1 argument. |
| `int() takes exactly 1 argument` | Pass exactly 1 argument. |
| `float() takes exactly 1 argument` | Pass exactly 1 argument. |
| `push() takes exactly 2 arguments` | Pass exactly 2 arguments. |
| `abs() takes exactly 1 argument` | Pass exactly 1 argument. |
| `min() takes exactly 2 arguments` | Pass exactly 2 arguments. |
| `max() takes exactly 2 arguments` | Pass exactly 2 arguments. |
| `read_file() takes exactly 1 argument` | Pass exactly 1 argument. |
| `write_file() takes exactly 2 arguments` | Pass exactly 2 arguments. |
| `file_exists() takes exactly 1 argument` | Pass exactly 1 argument. |

---

### AGL_ERR_IO -- I/O Errors

Produced by the module system during import resolution.

| Message | Cause | Fix |
|---------|-------|-----|
| `invalid import path 'path'` | Path contains `..`, resolves outside base directory, or is too long. | Use a relative path without `..` that stays within the project. |
| `cannot open module 'path'` | Module file does not exist or cannot be read. | Verify the file exists at the expected location (`.agl` extension is added automatically). |

---

## Error Context

### Error Object Structure

Each error contains:

- **Code**: One of `AGL_ERR_SYNTAX`, `AGL_ERR_TYPE`, `AGL_ERR_NAME`, `AGL_ERR_RUNTIME`, `AGL_ERR_IO`.
- **Location**: File path, line number, column number.
- **Message**: Human-readable description (max 256 characters).
- **Stack trace**: Up to 16 call frames (populated for runtime errors inside functions).

### Error Propagation

Agl uses single-error semantics. When an error occurs:

1. The error is set on the shared `AglCtx` context.
2. All subsequent evaluation and execution short-circuits (checks `agl_error_occurred`).
3. The error is printed to stderr and the program exits with a nonzero status.

Only one error is reported per run. The first error encountered halts execution.
