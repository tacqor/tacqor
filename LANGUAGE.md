# Tacqor 1.2 language reference

Tacqor is a statically typed language with Python's surface syntax, Rust-style checked arithmetic and `Option`/`Result` error handling, generics, traits, closures, modules, and two execution modes: a bytecode VM and native machine code through the system C++ compiler. This document is the complete reference. For a guided introduction read `TOUR.md`; for the library read `STDLIB.md`.

## 1. Source files and modules

A program is a UTF-8 `.tq` file. Indentation is four spaces per level, `#` starts a comment, brackets may span lines, and a trailing backslash continues a line. Top-level declarations are `import`, `const`, `struct`, and `def`. The file passed to `tacqor run` is the root module and must define `def main():`.

```text
import std.lists              # loads std/lists.tq; refer to it as lists
import geometry as geo        # loads geometry.tq next to this file; refer to it as geo

const LIMIT = 100

struct Point:
    x: f64
    y: f64

def main():
    print(lists.map([1, 2, 3], fn(n: i64) => n * LIMIT))
```

**Resolution.** `import a.b.c` loads `a/b/c.tq` (or `a/b/c/lib.tq`), searched in: the importing file's directory, the `packages/` folder next to the root program and in the working directory (see §10), the `std` directory shipped next to the `tacqor` executable, and each entry of `TACQOR_PATH` (colon separated). A module is loaded once however many times it is imported; circular imports are an error. Everything a module declares is public and reached through its alias: `alias.function(...)`, `alias.Type`, `alias.CONSTANT`. A module's own `main`, if any, is ignored when it is imported.

Names of functions, structs, traits, constants and import aliases share one namespace per module. Local variables may shadow any of them, and built-in function names too; inside the local's scope the local wins.

## 2. Types

| Type | Values |
|---|---|
| `i64` | 64-bit signed integers. Every operation is overflow-checked; literals may use `_` separators (`1_000_000`). |
| `f64` | IEEE doubles. Literals need digits on both sides of the point (`1.0`, `2.5e-3`). |
| `bool` | `True`, `False`. |
| `str` | Immutable UTF-8 byte strings. `len` counts bytes; `chars`/`char_count` work in code points. Indexing yields one byte. |
| `list[T]` | Mutable, growable lists. `list[i64]`, `list[f64]` and `list[bool]` are stored as flat native arrays. |
| `dict[K, V]` | Mutable, insertion-ordered dictionaries with `i64` or `str` keys. |
| `Option[T]` | `Some(value)` or `None`. |
| `Result[T, E]` | `Ok(value)` or `Err(error)`. |
| `fn(A, B) -> R`, `fn(A)` | Function values (closures). Omit `-> R` for a procedure. |
| *struct* | User types with named, typed, mutable fields and methods; may be generic. |
| *trait* | An interface. A value of trait type is any struct that implements it; calls dispatch at runtime. |

Lists, dicts, structs and closures are reference values: binding or passing them shares the object (`let` makes the *binding* immutable, not the object). Everything else is a value. `==` and `!=` compare structurally for every type. There are no implicit conversions: use `float(i)`, `int(f)`, `str(x)`.

Type expressions: `i64`, `list[str]`, `dict[str, list[i64]]`, `Option[Point]`, `Result[i64, str]`, `fn(i64, i64) -> bool`, `fn(str)`, `Pair[i64, str]`, `geo.Point`.

## 3. Declarations

### Bindings

```text
let name = expr              # immutable binding, type inferred
var count = 0                # reassignable
var items: list[i64] = []    # annotation needed when the value does not determine the type
let none: Option[str] = None
```

### Functions

```text
def area(width: f64, height: f64) -> f64:
    return width * height

def log(message: str):       # a procedure: no result; `return` leaves early
    print(message)

def first[T](items: list[T]) -> Option[T]:    # generic
    if len(items) == 0:
        return None
    return Some(items[0])
```

Every path of a value-returning function must `return`. A statement after `return`, `break`, `continue`, an exhaustive `match` whose arms all exit, or a `while True:` loop without `break` is an error (unreachable). Calls to `exit(...)` and `fail(...)` also end a path.

Generic type parameters are written in brackets after the name, optionally with a trait bound (`[T: Shape]`). They are inferred from the arguments at each call (`first([1, 2])` gives `T = i64`), or given explicitly (`first[i64]([])`). A bound is checked at every instantiation and lets the body call the trait's methods on values of type `T` — statically, since each instantiation knows the concrete type. Each distinct instantiation is compiled separately (monomorphization), so generic code runs at full speed in native builds. A generic function cannot be used as a value without explicit type arguments.

### Structs and methods

```text
struct Stack[T]:
    items: list[T]

    def push(self, item: T):
        push(self.items, item)

    def pop(self) -> Option[T]:
        if len(self.items) == 0:
            return None
        return Some(pop(self.items))

let s = Stack[i64]([])      # or Stack([1, 2]) with T inferred from the fields
s.push(3)
print(s.pop())              # Some(3)
print(s.items)              # [1, 2]
```

Construct a struct positionally. Methods are `def`s inside the struct whose first parameter is `self`; call them with `value.method(args)`. Fields are read and written with `value.field`. A struct may refer to itself (`next: Option[Node[T]]`).

### Traits

```text
trait Shape:
    def area(self) -> f64
    def name(self) -> str

struct Circle(Shape):            # the traits a struct implements, in parentheses
    radius: f64

    def area(self) -> f64:
        return 3.141592653589793 * self.radius * self.radius

    def name(self) -> str:
        return "circle"

def describe(s: Shape) -> str:   # any implementing struct can be passed
    return f"{s.name()}: {s.area()}"

def total[T: Shape](shapes: list[T]) -> f64:   # or a bound: static calls per instantiation
    var sum = 0.0
    for s in shapes:
        sum += s.area()
    return sum

let shapes: list[Shape] = [Circle(1.0), Square(2.0)]   # heterogeneous: dynamic dispatch
```

A trait lists method signatures. A struct declares the traits it implements after its name; the compiler checks that every method exists with exactly the trait's signature. A struct value converts implicitly wherever one of its traits is expected (parameters, annotated bindings, list elements with an annotated type, returns), and `s.method()` on a trait-typed value dispatches on the actual struct. Trait values compare with `==` structurally and print as the underlying struct. Generic structs may implement traits too (`struct Box[T](Shape)`); traits can be qualified across modules (`geometry.Named`).

### Constants

`const NAME = literal` declares a module-level constant: an integer, float, bool, string, or another constant. Constants are inlined where used.

### Function values and closures

```text
let double = fn(x: i64) => x * 2            # result type inferred
let greet = fn(name: str) -> str => "hi " + name
let log_all = fn(items: list[str]):         # block body; must be the last thing on the line
    for item in items:
        print(item)
let f = area                                # a named function as a value
```

A lambda captures the enclosing variables it mentions **by value** at the moment it is created: integers and strings are copied, lists/dicts/structs are shared. Captured names cannot be assigned inside the lambda (use a shared container or struct to communicate out). Block-bodied lambdas are allowed where a statement ends (`let`, `var`, assignment, `return`), not inside brackets.

Call a function value like any function: `double(4)`. Pass it as `fn(i64) -> i64`. The method-call form `x.f(args)` on a value whose type has no such method calls the built-in or module function `f(x, args)` instead: `items.push(1)`, `name.upper()`, `s.len()`.

## 4. Statements

| Statement | Notes |
|---|---|
| `x = e`, `a[i] = e`, `d[k] = e`, `p.field = e` | Assignment. `+=` `-=` `*=` `//=` `%=` `&=` `|=` `^=` `<<=` `>>=` on `i64`; `+=` `-=` `*=` `/=` `%=` on `f64`; `+=` on `str`. |
| `if c:` / `elif c:` / `else:` | Conditions are `bool`. |
| `while c:` | |
| `for i in range(n):`, `for i in range(a, b):` | Counts `0`/`a` up to but excluding `n`/`b`; bounds evaluated once. |
| `for item in some_list:` | Iterates elements in order. |
| `break`, `continue` | Innermost loop. |
| `return [e]` | |
| `assert c` | Runtime error `assertion failed` when false. |
| `match e:` | See below. |
| any call | A call can stand alone; its result is discarded. |

### match

```text
match lookup(ages, name):          # Option[i64]
    case Some(age):
        print(age)
    case None:
        print("unknown")

match parse(text):                 # Result[Json, str]
    case Ok(doc):
        use(doc)
    case Err(problem):
        print(problem)

match command:                     # i64, f64, bool or str: literal cases, then `case _`
    case "add":
        add()
    case "list":
        show()
    case _:
        print("?")
```

Option and Result matches must cover both arms (or end with `case _`); matches on plain values must end with `case _`. `case Some(_)` ignores the payload.

## 5. Expressions

Precedence, highest first (same as Python): calls, indexing `a[i]`, slicing `a[i:j]`, field access and method calls, `?`; unary `-` and `~`; `*` `/` `//` `%`; `+` `-`; shifts `<<` `>>`; `&`; `^`; `|`; comparisons `==` `!=` `<` `<=` `>` `>=` `in`; `not`; `and`; `or`. Comparisons do not chain (`a < b and b < c`). `and`/`or` short-circuit.

- Integer division `//` floors; `%` takes the divisor's sign. `/` is for `f64` only. Division by zero and overflow are runtime errors for `i64`; `f64` follows IEEE (`inf`, `nan`).
- Bit operations on `i64`: `&` `|` `^` `~`, `<<` (wrapping, like Rust's `wrapping_shl`) and `>>` (arithmetic, keeps the sign). Shift counts must be 0–63 (runtime error otherwise; a warning when constant). Compound forms `&=` `|=` `^=` `<<=` `>>=` exist.
- `str + str` concatenates. Ordering comparisons work on `i64`, `f64`, `str`.
- `x in list`, `key in dict`, `sub in text`.
- Slices clamp like Python: `items[1:]`, `text[:3]`, `items[-1:]` is empty (indices are not negative-wrapped).
- Literals: `[1, 2]`, `["a"]`, `{"k": 1}`, `[]`/`{}` (need a type from context), `Some(x)`, `None`, `Ok(x)`, `Err(e)`. `None`, `Ok` and `Err` need the full type from context (an annotation, a parameter type, or the function's return type).
- **f-strings:** `f"{name} is {age + 1} years"` formats any expression with `str()`. `{{` and `}}` are literal braces; quotes may be nested inside the braces (`f"{d["key"]}"`).
- **`?`:** `value?` unwraps `Some`/`Ok` or returns the `None`/`Err` from the enclosing function, which must return the same `Option`/`Result` error shape.

## 6. Built-in functions

The complete list with signatures is in `STDLIB.md`. Highlights: `print`, `write`, `eprint`, `input`, `args`, `str`, `int`, `float`, `len`, `push`, `pop`, `sort`, `keys`, `lookup`, `split`, `join`, `find`, `replace`, `chars`, `unwrap`, `unwrap_or`, `is_some`, `parse_int`, `read_text`, `write_text`, `read_lines`, `file_exists`, `env`, `random_int`, `time_ms`, `sleep_ms`, `run`, `exit`, `fail`, and the math set `sqrt pow sin cos ... log`. `len(x)` and `x.len()` are the same call.

`parallel_sum(count, workers, mapper)` maps a `fn(i64) -> i64` over `range(count)` on 1–64 threads and returns the exact sum; `parallel_map(count, workers, mapper)` returns every result as a `list[i64]`, in order. The mapper may be a named function or a closure, and the result never depends on the worker count. Because workers run truly in parallel, a closure mapper may only capture `i64`, `f64` and `bool` values (checked at compile time for lambdas and at runtime for other function values); read any larger data through the index, e.g. by computing it from `i` or by capturing a constant.

`parallel_run(workers, task)` runs `task(worker_index)` on `workers` threads at once (1–64), each thread with its own VM and value stack, and returns once every task has returned — for a server accept-loop, that is never, so it blocks like a server should. It is the share-nothing counterpart to the two reductions above. The model is what Tacqor's non-atomic reference counting allows and no more: **each worker owns every object it allocates, and no object on the heap is ever touched by two threads at once.** Two rules follow and are enforced. The task may capture only scalars and function values — the compiler checks an inline `fn(...)` task, and a runtime check backs it up for a task bound to a name first; anything else is refused. And state shared *between* workers does not live in a captured value: it lives in a file, a database, or another process, where each worker coordinates its own reads and writes. Within those rules the speed-up is close to linear in the worker count for work that is not allocation-bound (object allocation and freeing share one lock). This is the primitive behind `http.serve_concurrent`:

```python
import std.http

def handle(request: http.Request) -> http.Response:    # a plain function: no captures
    return http.text(200, "hello " + request.path)

def main():
    http.serve_concurrent(8080, 8, handle)              # eight workers, share-nothing
```

When workers need to hand data to one another rather than go through a file, use a **channel** — a thread-safe queue of string messages. `channel()` returns a handle (a scalar, so a task may capture it); `channel_send(ch, message)` enqueues a copy; `channel_recv(ch) -> Option[str]` blocks until a message arrives (`Some`) or the channel is closed and drained (`None`); `channel_try_recv(ch)` is the non-blocking form; `channel_close(ch)` wakes receivers and ends the stream. Messages cross as copies, so the share-nothing rule still holds — structure a message with `std.json` when it is more than text. `std.channels` wraps these as `make`, `send`, `recv`, `try_recv`, `close`.

```python
import std.channels

def main():
    let jobs = channels.make()
    let worker = fn(w: i64):
        var running = True
        while running:
            match channels.recv(jobs):
                case Some(job):
                    print("worker " + str(w) + " took " + job)
                case None:
                    running = False              # closed and drained
    for i in range(20):
        channels.send(jobs, "job-" + str(i))
    channels.close(jobs)
    parallel_run(4, worker)                      # four workers drain the queue
```

## 7. Errors and checking

### Recovering from runtime errors

Runtime errors (a failed index, `unwrap` on `None`, `fail(...)`, overflow, an assertion) abort the program with a traceback — unless they happen inside `attempt`:

```text
let outcome = attempt(fn() => risky(input))      # Result[i64, str]
match outcome:
    case Ok(value):
        use(value)
    case Err(message):
        log("recovered: " + message)
```

`attempt(f)` runs a zero-argument function value and returns `Ok(result)` or `Err(message)` (for a procedure, `Ok(True)`). It is how a server keeps running when one request fails (see `std.http`), and the only form of catching in the language: there is no `try`/`except`, so a function that can fail should return a `Result`, and `attempt` is reserved for boundaries. `exit(...)` is never caught.

The compiler rejects type mismatches, unknown names, assignment to immutable or captured bindings, missing returns, unreachable code, non-exhaustive matches, misuse of `?`, wrong arities and bad generic inference. Errors name the file for modules: `std/json.tq:12:5: error: ...`.

The scanner (`tacqor check`, `tacqor diagnose`, the VS Code extension) warns about operations that would certainly fail or do nothing: constant division by zero, constant overflow, indexes outside lists of known length, empty loops, assertions that always fail, invalid worker counts.

At runtime every integer operation, index, dictionary lookup, conversion, `unwrap` and `assert` is checked. A failure prints `file:line:column: runtime error: message` followed by the call stack and exits with status 1. `exit(code)` ends the program with that status. Native executables behave identically.

## 8. Tooling

| Command | Purpose |
|---|---|
| `tacqor FILE.tq [args]`, `tacqor run FILE.tq [-- args]` | Compile and run in the VM (also runs `.tqb`). A file starting with `#!/usr/bin/env tacqor` runs as a script. |
| `tacqor test FILE.tq` | Run every `def test_*()` in the file, report PASS/FAIL, exit 1 on failure. |
| `tacqor build FILE.tq -o EXE` | Standalone executable: bytecode + embedded VM. Runs anywhere compatible. |
| `tacqor build --native FILE.tq -o EXE` | Machine code through the system C++ compiler (`CXX`, default `c++`; `cl` on Windows). |
| `tacqor emit FILE.tq -o FILE.cpp` | The C++ that `--native` compiles. |
| `tacqor compile FILE.tq -o FILE.tqb` | Portable bytecode. |
| `tacqor check FILE.tq` / `diagnose FILE.tq` | Warnings as text / JSON (`diagnose --stdin` for editors). |
| `tacqor dump FILE.tq` | Disassemble the compiled program. |
| `tacqor new NAME` | Create a project folder with `main.tq`, `tests.tq`, `README.md`. |
| `tacqor repl` | Interactive scratchpad (§11). |
| `tacqor add SOURCE [NAME]`, `install`, `remove NAME` | Package management (§10). |

## 9. Memory

Values are reference counted, so most objects are freed the moment they become unreachable. Cycles (a struct that reaches itself through a field or a list) are reclaimed by a cycle collector that runs automatically after a few hundred thousand container allocations, in both the VM and native executables. `gc_collect()` runs it now and returns the number of objects freed; `gc_count()` reports how many containers are currently tracked. Lists of numbers and booleans, strings, and immediates are never tracked, so they cost nothing extra.

## 10. Packages

`tacqor add SOURCE [NAME]` installs a package into `packages/NAME` from a local folder or a git URL (via `git clone`) and records it in `tacqor.pkg`; `tacqor install` restores every package listed there; `tacqor remove NAME` uninstalls. A package is a folder of `.tq` modules: `import NAME` loads `packages/NAME/lib.tq` and `import NAME.part` loads `packages/NAME/part.tq`. There is no central registry: a package is any git repository or folder, which keeps the mechanism simple and auditable.

## 11. REPL

`tacqor repl` is a scratchpad: definitions (`def`, `struct`, `trait`, `import`, `const`) and statements accumulate, an expression on its own is printed, and a line ending in `:` opens a block that an empty line closes. Because Tacqor compiles whole programs, the session is recompiled and re-run after each entry and only new output is shown; side effects such as file writes therefore repeat. `:show` prints the session, `:reset` clears it, `:quit` leaves.

## 12. Networking and processes

`tcp_listen`, `tcp_accept`, `tcp_connect`, `tcp_read`, `tcp_write`, `tcp_close` are blocking TCP primitives that return `Result`s; `std.http` builds an HTTP/1.1 server (`serve`, `serve_count`) and client (`get`, `post`) on them in Tacqor. `run(command)` runs a shell command and returns its status; `run_capture(command)` returns its output. `unix_time()` is seconds since the epoch; `hash(text)` is a 64-bit FNV-1a hash folded to a non-negative `i64`.

## 13. Calling C (FFI)

An `extern def` declaration names a C function. In a native build (`tacqor build --native`) a call to it compiles to a direct C call; the declaration says how the arguments and the result cross the boundary.

```python
extern def sqrt(x: f64) -> f64 from "m"            # links libm, calls C's sqrt
extern def llabs(n: i64) -> i64                     # libc; no library to name
extern def getenv(name: str) -> str                 # str crosses as const char*
extern def to_int(s: str) -> i64 = "atoll"          # = renames the C symbol
```

`from "name"` names a library to link (`-lname`, or `name.lib` on MSVC); omit it for libc and libm symbols that link by default. `= "symbol"` gives the C symbol when it differs from the Tacqor name. Only scalars and strings cross the boundary: `i64` as `long long`, `f64` as `double`, `bool` as `int` (0 or 1), and `str` as a NUL-terminated `const char*` — a string argument is passed by pointer into the C function, and a `const char*` result is copied into a Tacqor string (a NULL result becomes `""`). A parameter or result of any other type is a compile-time error. Pointers and structs are not supported yet.

The boundary is unchecked: Tacqor cannot verify that the C signature you declared matches the real one, and a wrong declaration is undefined behaviour, exactly as in C. This is the one place the language trades its guarantees for reach, and it is explicit at the declaration. Under the bytecode VM (`tacqor run`, or a `tacqor build` embedded-VM binary) a foreign call reports that a native build is required, so develop foreign code with `build --native`.

## 14. Limits and non-features

No inheritance: traits plus generics and closures cover the same ground without an object hierarchy. No `try`/`except`: use `Result` and `?`, and `attempt` at boundaries. Parallelism is share-nothing rather than shared-memory: `parallel_sum`/`parallel_map` for data-parallel work, `parallel_run` (and `http.serve_concurrent` on top of it) for request-parallel work, and channels for passing messages between workers — state is shared between threads through a channel, a file, or a database, never a mutable value on the heap. Strings are bytes; only `chars`, `char_count`, `chr`, `ord` are code-point aware. Call depth is limited to 100,000 frames (in native builds too, with a 512 MB stack); value nesting (lists inside lists...) is limited to 2,000 levels for printing and comparison.
