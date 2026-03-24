# Agl Standard Library Reference

## 1. I/O

### print

```
print(args...) -> nil
```

Prints each argument to stdout, separated by nothing, followed by a newline. Accepts any number of arguments of any type. Arrays are printed as `[a, b, c]`, structs as `<struct TypeName>`, results as `ok(...)` or `err(...)`.

```ago
print("hello")          // hello
print(42)               // 42
print("x =", 10)        // x =10
print([1, 2, 3])        // [1, 2, 3]
```

**Errors:** None.

---

## 2. Type Inspection

### type

```
type(val: any) -> string
```

Returns the type name of `val` as a string.

```ago
type(42)            // "int"
type(3.14)          // "float"
type(true)          // "bool"
type("hello")       // "string"
type([1, 2])        // "array"
type(fn() {})       // "fn"
type(ok(1))         // "result"
type(nil)           // "nil"
```

**Errors:** Requires exactly 1 argument.

### len

```
len(x: array | string) -> int
```

Returns the number of elements in an array or the length of a string.

```ago
len([1, 2, 3])      // 3
len("hello")         // 5
len([])              // 0
```

**Errors:**
- Requires exactly 1 argument.
- TypeError if argument is not an array or string.

---

## 3. Type Conversion

### str

```
str(val: any) -> string
```

Converts any value to its string representation. If the value is already a string, returns it unchanged.

```ago
str(42)             // "42"
str(3.14)           // "3.14"
str(true)           // "true"
str(nil)            // "nil"
str([1, 2])         // "<array[2]>"
```

**Errors:** Requires exactly 1 argument.

### int

```
int(x: string | float | int) -> int
```

Converts a value to an integer. Strings are parsed as base-10 integers. Floats are truncated toward zero. Integers are returned unchanged.

```ago
int("123")          // 123
int(3.7)            // 3
int(42)             // 42
```

**Errors:**
- Requires exactly 1 argument.
- RuntimeError if string is not a valid integer (`"abc"`, `""`).
- TypeError if argument is not a string, float, or int.

### float

```
float(x: string | int | float) -> float
```

Converts a value to a float. Strings are parsed as floating-point numbers. Integers are widened. Floats are returned unchanged.

```ago
float("2.5")        // 2.5
float(42)           // 42.0
float(3.14)         // 3.14
```

**Errors:**
- Requires exactly 1 argument.
- RuntimeError if string is not a valid number.
- TypeError if argument is not a string, int, or float.

---

## 4. Arrays

### push

```
push(arr: array, val: any) -> array
```

Returns a new array with `val` appended to the end of `arr`. The original array is not modified.

```ago
let a = [1, 2, 3]
let b = push(a, 4)     // [1, 2, 3, 4]
print(a)                // [1, 2, 3]  (unchanged)
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if first argument is not an array.
- RuntimeError if array size would exceed 1024 elements.

### map

```
map(arr: array, fn: fn) -> array
```

Returns a new array where each element is the result of calling `fn` on the corresponding element of `arr`.

```ago
let nums = [1, 2, 3]
let doubled = map(nums, fn(x: int) -> int { return x * 2 })
print(doubled)          // [2, 4, 6]
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if first argument is not an array or second is not a function.
- Propagates any error raised by `fn`.

### filter

```
filter(arr: array, fn: fn) -> array
```

Returns a new array containing only the elements of `arr` for which `fn` returns a truthy value.

```ago
let nums = [1, 2, 3, 4, 5]
let evens = filter(nums, fn(x: int) -> bool { return x % 2 == 0 })
print(evens)            // [2, 4]
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if first argument is not an array or second is not a function.
- Propagates any error raised by `fn`.

---

## 5. Math

### abs

```
abs(n: int | float) -> int | float
```

Returns the absolute value of a number. The return type matches the input type.

```ago
abs(-42)            // 42
abs(3.14)           // 3.14
abs(-2.5)           // 2.5
```

**Errors:**
- Requires exactly 1 argument.
- TypeError if argument is not a number.

### min

```
min(a: int | float, b: int | float) -> int | float
```

Returns the smaller of two numbers. Both arguments must be the same numeric type.

```ago
min(10, 3)          // 3
min(2.5, 1.0)       // 1.0
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if arguments are not two numbers of the same type (no int/float mixing).

### max

```
max(a: int | float, b: int | float) -> int | float
```

Returns the larger of two numbers. Both arguments must be the same numeric type.

```ago
max(10, 3)          // 10
max(2.5, 1.0)       // 2.5
```

**Errors:**
- Requires exactly 2 arguments.
- TypeError if arguments are not two numbers of the same type (no int/float mixing).

---

## 6. Result Constructors

`ok` and `err` are syntax forms, not functions. They wrap a value into a `result` type for structured error handling.

### ok

```
ok(expr) -> result
```

Wraps a value as a successful result.

### err

```
err(expr) -> result
```

Wraps a value as an error result.

### match

Results are unwrapped with the `match` expression:

```ago
fn safe_div(a: int, b: int) -> result {
    if b == 0 {
        return err("division by zero")
    }
    return ok(a / b)
}

match safe_div(10, 3) {
    ok(v) -> print(v)       // 3
    err(e) -> print(e)
}

match safe_div(10, 0) {
    ok(v) -> print(v)
    err(e) -> print(e)      // division by zero
}
```

**Errors:**
- `match` requires the subject to be a `result` value (TypeError otherwise).

---

## 7. File I/O

### read_file

```
read_file(path: string) -> result
```

Reads the entire contents of a file. Returns `ok(content)` on success or `err(message)` on failure.

```ago
match read_file("data.txt") {
    ok(content) -> print(content)
    err(e) -> print("Error: " + e)
}
```

**Errors (returned as `err`):**
- `"cannot read file"` -- file does not exist or is not readable.
- `"file too large"` -- file exceeds 10 MB.

**Errors (raised as runtime errors):**
- TypeError if argument is not a string.
- Requires exactly 1 argument.

### write_file

```
write_file(path: string, content: string) -> result
```

Writes `content` to a file, overwriting any existing content. Returns `ok(true)` on success or `err(message)` on failure.

```ago
match write_file("out.txt", "hello world") {
    ok(_) -> print("written")
    err(e) -> print("Error: " + e)
}
```

**Errors (returned as `err`):**
- `"cannot write file"` -- path is not writable.

**Errors (raised as runtime errors):**
- TypeError if either argument is not a string.
- Requires exactly 2 arguments.

### file_exists

```
file_exists(path: string) -> bool
```

Returns `true` if the file at `path` exists and is readable, `false` otherwise.

```ago
if file_exists("config.agl") {
    print("found config")
}
```

**Errors:**
- Requires exactly 1 argument.
- TypeError if argument is not a string.
