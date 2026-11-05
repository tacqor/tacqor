# The Tacqor Manual

The complete reference for the Tacqor programming language and toolchain, version 1.3.0. Every language construct, every built-in function, every standard-library module, every command, and every limit is documented here, derived directly from the implementation. For the formal grammar see [`SPECIFICATION.md`](SPECIFICATION.md); for the bytecode see [`BYTECODE.md`](BYTECODE.md); for runnable programs see [`COOKBOOK.md`](COOKBOOK.md).

## Contents

1. [Overview](#1-overview)
2. [The toolchain and execution modes](#2-the-toolchain-and-execution-modes)
3. [Lexical structure](#3-lexical-structure)
4. [Types](#4-types)
5. [Bindings: `let`, `var`, `const`](#5-bindings-let-var-const)
6. [Operators](#6-operators)
7. [Expressions](#7-expressions)
8. [Statements](#8-statements)
9. [Functions and closures](#9-functions-and-closures)
10. [Structs and methods](#10-structs-and-methods)
11. [Traits](#11-traits)
12. [Generics](#12-generics)
13. [Pattern matching](#13-pattern-matching)
14. [Error handling](#14-error-handling)
15. [Modules and packages](#15-modules-and-packages)
16. [Concurrency](#16-concurrency)
17. [C foreign function interface](#17-c-foreign-function-interface)
18. [Memory model](#18-memory-model)
19. [Built-in function reference](#19-built-in-function-reference)
20. [Standard library reference](#20-standard-library-reference)
21. [Diagnostics](#21-diagnostics)
22. [Limits](#22-limits)
23. [Appendix: keywords and operators](#23-appendix-keywords-and-operators)

---

## 1. Overview

Tacqor is a statically typed language with Python's syntax and Rust's error discipline. One source program can run two ways: on a bytecode virtual machine, or as native machine code generated through the system C++ compiler. A single executable checks, runs, compiles, tests, packages, and serves as a REPL and a language server.

A minimal program:

```python
def main():
    print("Hello, world!")
```

Every program's entry point is `def main():` — no parameters, no result. Top-level items are functions (`def`), foreign functions (`extern def`), structures (`struct`), traits (`trait`), constants (`const`), and imports (`import`). Statements may not appear at the top level; they live inside function bodies.

The design is defined as much by what it omits as by what it includes. There is no inheritance (traits, generics and composition replace it), no exceptions (`Result`, `?` and `attempt`), no `null` (`Option`), no implicit numeric conversions, no operator overloading, no macros, and no ownership system. Each omission keeps reasoning local: you can understand a line without chasing non-local behavior.

---

## 2. The toolchain and execution modes

The whole toolchain is the single `tacqor` executable.

| Command | Purpose |
|---|---|
| `tacqor FILE.tq [ARG ...]` | Run a program (same as `run`). |
| `tacqor run FILE.tq \| FILE.tqb [-- ARG ...]` | Run a source file or compiled bytecode. Arguments after `--` reach `args()`. |
| `tacqor check FILE.tq` | Type-check without running; prints `OK` or the first error. |
| `tacqor diagnose FILE.tq \| --stdin [FILE.tq]` | Emit all diagnostics as JSON (for editors). `--stdin` reads the source from standard input. |
| `tacqor test FILE.tq` | Run every `def test_*()` in the file; report pass/fail counts. |
| `tacqor dump FILE.tq \| FILE.tqb` | Disassemble to human-readable bytecode. |
| `tacqor compile FILE.tq -o FILE.tqb` | Compile to a portable bytecode file. |
| `tacqor build FILE.tq -o EXE` | Produce a standalone executable with the bytecode and an embedded VM. |
| `tacqor build --native FILE.tq -o EXE` | Produce native machine code via the system C++ compiler. |
| `tacqor emit FILE.tq -o FILE.cpp` | Emit the C++ that `build --native` would compile. |
| `tacqor new NAME` | Create a project folder (`main.tq`, `tests.tq`). |
| `tacqor repl` | Interactive scratchpad. |
| `tacqor lsp` | Language server over stdio, for any LSP-capable editor. |
| `tacqor add SOURCE [NAME]` | Install a package from a folder or git URL into `packages/`. |
| `tacqor install` | Restore every package listed in `tacqor.pkg`. |
| `tacqor remove NAME` | Uninstall a package. |
| `tacqor --version` · `tacqor --help` | Version string; usage. |

**The four execution modes.** `run` (and `FILE.tq`) interpret on the VM — the fastest edit/run loop. `compile` produces `.tqb` bytecode that `run` executes anywhere. `build` embeds that bytecode plus the VM into one executable that needs no `tacqor` present. `build --native` transpiles to C++ and compiles to machine code at `-O3` for production speed. All four produce identical observable behavior; the differential test suite checks this byte for byte.

**Environment variables.** `TACQOR_CXX` or `CXX` select the C++ compiler for native builds; `TACQOR_HOME` overrides where the standard library is found. **Exit codes:** `0` success; `1` a runtime error or a failed check/test; `2` a usage error; `3+` whatever `exit(n)` requested.

---

## 3. Lexical structure

**Encoding and layout.** Source is UTF-8. Indentation is significant, as in Python: a block is introduced by `:` and a newline, then an indented group of statements. A `#` begins a comment to end of line.

**Identifiers** are letters, digits and underscore, not starting with a digit. **Reserved words** may not be used as identifiers:

```
def  struct  let  var  if  elif  else  while  for  in  break  continue
return  assert  and  or  not  True  False  None  range  fn  match  case
import  as  const  Some  Ok  Err  Option  Result  trait  extern
i64  f64  bool  str  list  dict
```

**Literals.**

- **Integer** — decimal digits with optional `_` separators, e.g. `1_000_000`. No leading zeroes. Range is the signed 64-bit range; `-9223372036854775808` is accepted as a unary-minus literal.
- **Float** — digits with a `.` and/or exponent, e.g. `3.14`, `6.022e23`.
- **Boolean** — `True`, `False`.
- **String** — `"..."` double-quoted, UTF-8 bytes, with escapes `\n \t \r \\ \" \0` and others.
- **Interpolated string (f-string)** — `f"text {expr} more"`. Each `{expr}` is evaluated and converted with `str()`, then concatenated. `{{` and `}}` are literal braces.

---

## 4. Types

Tacqor is statically typed with inference; annotations are needed only where inference cannot decide (an empty literal, a bare `None`).

**Primitive types**

| Type | Values |
|---|---|
| `i64` | Signed 64-bit integers. Arithmetic is overflow-checked (a runtime error on overflow). |
| `f64` | IEEE-754 double-precision floats. |
| `bool` | `True` / `False`. |
| `str` | Immutable byte strings (UTF-8 by convention). `chars`, `char_count`, `chr`, `ord` are the code-point-aware operations; indexing and `len` are byte-based. |

**Composite types**

| Type | Meaning |
|---|---|
| `list[T]` | A growable ordered sequence of `T`. |
| `dict[K, V]` | A hash map; `K` must be `i64` or `str`. |
| `Option[T]` | `Some(T)` or `None` — a value that may be absent. |
| `Result[T, E]` | `Ok(T)` or `Err(E)` — a success or a failure. |
| `fn(A, B, ...) -> R` | A function value; `-> R` omitted means it returns nothing (unit). |
| a struct name | A value of that structure. |
| a trait name | A trait object (dynamic dispatch) when used as a type. |

Numbers never convert implicitly: mixing `i64` and `f64` in one operation is an error; use `int(x)` or `float(x)`. `i64` divides with `//`; `f64` divides with `/`.

---

## 5. Bindings: `let`, `var`, `const`

```python
let x = 10                     # immutable binding, type inferred
let y: f64 = 2.0               # with an annotation
var count = 0                  # mutable; may be reassigned and compound-assigned
count += 1
const LIMIT = 100              # top-level compile-time constant
```

`let` binds a value that cannot be reassigned; `var` binds one that can. `const` is a top-level constant initialized by a constant expression. A binding captured by a closure is read-only inside that closure — closures capture by value. Shadowing in the same scope is an error; so is redeclaring a captured name.

---

## 6. Operators

| Category | Operators | Notes |
|---|---|---|
| Arithmetic (`i64`) | `+` `-` `*` `//` `%` | Overflow-checked; `//` is integer division, `%` is modulo. |
| Arithmetic (`f64`) | `+` `-` `*` `/` `%` | `/` is float division. |
| Comparison | `==` `!=` `<` `<=` `>` `>=` | Order comparisons apply to `i64`, `f64`, `str`. |
| Logical | `and` `or` `not` | Short-circuiting; operate on `bool`. |
| Bitwise (`i64`) | `&` `\|` `^` `<<` `>>` `~` | Shift counts must be 0–63. |
| Membership | `in` | Tests a `list`, `dict` (keys), or `str` (substring). |
| Concatenation | `+` | On `str`; `str` supports only `+`. |
| Compound assignment | `=` `+=` `-=` `*=` `/=` `//=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | On a `var` binding or a list/dict element or a struct field. |

Comparisons do not chain (`a < b < c` is an error; use `a < b and b < c`). `not` must be parenthesized where precedence would be ambiguous.

---

## 7. Expressions

**Calls** — `f(a, b)`. **Indexing** — `xs[i]` on a list, dict or string (returns a one-byte string for `str`). **Slicing** — `xs[a:b]`, `xs[a:]`, `xs[:b]` on lists and strings. **Field access** — `value.field` on a struct. **Method calls** — `value.method(args)`.

**Lambdas.** Expression-bodied: `fn(x: i64) => x * x`. Block-bodied (bound to a name or passed where a block is allowed):

```python
let classify = fn(n: i64) -> str:
    if n < 0:
        return "negative"
    return "non-negative"
```

A lambda captures the surrounding bindings **by value**; captured names are read-only inside it.

**f-strings** — `f"{name} scored {score}"`; each hole is `str()`-converted.

**The `?` operator** — postfix on an `Option` or `Result`. `value?` evaluates to the inner value on `Some`/`Ok`, and otherwise returns `None`/`Err(e)` from the enclosing function, whose return type must match.

---

## 8. Statements

**Assignment** — `target = expr` and the compound forms, where `target` is a `var`, a list/dict element, or a struct field.

**Conditionals**

```python
if condition:
    ...
elif other:
    ...
else:
    ...
```

**Loops**

```python
for i in range(10):            # 0..9
    ...
for i in range(2, 5):          # 2,3,4
    ...
for item in collection:        # over a list
    ...
while condition:
    ...
```

**`match`** — on `Option`, `Result`, `i64`, `f64`, `bool`, or `str` (see §13). **`break`** and **`continue`** control the nearest loop. **`return [expr]`** leaves a function. **`assert condition`** raises a runtime error if the condition is false. A statement after a guaranteed `return` is a compile error (unreachable code).

---

## 9. Functions and closures

```python
def add(a: i64, b: i64) -> i64:
    return a + b

def greet(name: str):          # no -> means it returns nothing
    print("hi " + name)
```

Every parameter is annotated; the result type follows `->` or is absent. A non-unit function must return on every path. Functions are values: a bare name is a function value, and lambdas create them inline. A closure is a function value that captured surrounding bindings by value. `main` cannot be called or used as a value.

---

## 10. Structs and methods

```python
struct Account:
    owner: str
    balance: i64

    def withdraw(self, amount: i64) -> Result[i64, str]:
        if amount > self.balance:
            return Err("insufficient funds")
        self.balance -= amount
        return Ok(self.balance)

let a = Account("Ada", 100)    # positional construction, fields in order
print(a.balance)
```

A method's first parameter is `self`. Fields are assigned with `self.field = ...` inside methods and `value.field = ...` elsewhere (on a `var`-held or mutable struct value). Construction is positional: `Name(field0, field1, ...)`.

---

## 11. Traits

A trait is a set of method signatures a struct can implement, giving both static (generic bound) and dynamic (trait object) dispatch.

```python
trait Shape:
    def area(self) -> f64

struct Circle(Shape):
    r: f64
    def area(self) -> f64:
        return 3.14159 * self.r * self.r

def total_area(shapes: list[Shape]) -> f64:   # trait object -> dynamic dispatch
    var sum = 0.0
    for s in shapes:
        sum += s.area()
    return sum
```

A struct declares the traits it implements in parentheses after its name and must provide every method with the exact signature. Using the trait name as a type gives a trait object (dynamic dispatch); using it as a generic bound (`[T: Shape]`) gives static dispatch.

---

## 12. Generics

Functions and structs may take type parameters in `[...]`; they are **monomorphized** (a specialized copy is generated per concrete type), so generics cost nothing at runtime.

```python
def first[T](items: list[T]) -> Option[T]:
    if len(items) == 0:
        return None
    return Some(items[0])

struct Pair[A, B]:
    left: A
    right: B
```

Type parameters may carry a trait bound: `def largest[T: Ord](...)`. Type arguments are usually inferred from the call; where they cannot be, write them explicitly: `first[i64]([])`.

---

## 13. Pattern matching

`match` inspects a value and binds parts of it. The cases must cover the value.

```python
match result:
    case Ok(value):
        print(value)
    case Err(problem):
        print(problem)

match option:
    case Some(x):
        use(x)
    case None:
        pass

match code:                    # on i64/str/bool: literal cases + wildcard
    case 200:
        print("ok")
    case 404:
        print("missing")
    case _:
        print("other")
```

Case forms: `Some(name)` / `Some(_)`, `None`, `Ok(name)` / `Err(name)`, a literal (`i64`, `str`, `bool`), and `_` (wildcard). For `Option`/`Result` both arms are required; for scalar matches a `_` makes it exhaustive. A case after `case _` is unreachable (an error).

---

## 14. Error handling

Tacqor has no exceptions. Absence is `Option[T]`; failure is `Result[T, E]`.

- **Construct** — `Some(x)`, `None`, `Ok(x)`, `Err(e)`.
- **Inspect** — `is_some`, `is_none`, `is_ok`, `is_err`; or `match`.
- **Extract** — `unwrap(x)` (runtime error on `None`/`Err`), `unwrap_or(x, default)`, `expect(x, message)`.
- **Propagate** — the `?` operator returns the failure from the enclosing function.
- **Boundaries** — `attempt(fn() -> T)` runs a no-argument function and turns any runtime error into `Result[T, str]`, so you can recover at a boundary.
- **Abort** — `fail(message)` raises a runtime error; `exit(code)` ends the process.

```python
def parse_config(path: str) -> Result[i64, str]:
    let text = read_text(path)          # runtime error if missing -> wrap at a boundary
    return parse_int(strip(text))?      # Option/Result short-circuit
```

A runtime error prints `FILE:LINE:COLUMN: runtime error: MESSAGE` with a traceback and exits non-zero — unless it happened inside `attempt`.

---

## 15. Modules and packages

**Modules.** Every `.tq` file is a module. `import std.http` makes its public items available as `http.serve(...)`; `import mymod as m` renames it. The standard library modules are imported as `std.NAME`. Circular imports are rejected.

**Packages.** A package is a folder with a `lib.tq` (and optional submodules). `tacqor add SOURCE [NAME]` copies a local folder or `git clone`s a URL into `packages/NAME` and records it in `tacqor.pkg`; `import NAME` loads `packages/NAME/lib.tq`, `import NAME.module` loads `packages/NAME/module.tq`. `tacqor install` restores every manifest entry; `tacqor remove NAME` deletes one.

---

## 16. Concurrency

Concurrency is **share-nothing**: a thread owns every object it allocates and no object on the heap is touched by two threads at once. That invariant is what lets the reference counting stay non-atomic, and it is enforced by the capture rules below.

**Data-parallel reductions.** `parallel_sum(count, workers, mapper)` evaluates `mapper(i)` for `0 ≤ i < count` across up to 64 threads and returns the exact `i64` sum; `parallel_map(count, workers, mapper)` returns the results as a `list[i64]` in order. The mapper is `fn(i64) -> i64`; if it is a closure it may capture only `i64`, `f64` and `bool` values. The result never depends on the worker count.

**Worker pools.** `parallel_run(workers, task)` runs `task(worker_index)` on `workers` threads at once (1–64), each with its own VM, returning once every task returns (for a server loop, never). The task may capture only scalars and function values — checked at compile time for an inline lambda, and by a runtime check otherwise. Shared state goes through a channel, a file, or a database.

**Channels.** `channel()` returns an integer handle to a thread-safe queue of string messages (a scalar, so a task can capture it). `channel_send(ch, message)` enqueues a copy; `channel_recv(ch) -> Option[str]` blocks until a message arrives (`Some`) or the channel is closed and drained (`None`); `channel_try_recv(ch) -> Option[str]` never blocks; `channel_close(ch)` ends the stream. Messages cross as copies, so nothing on the heap is shared. The `std.channels` module wraps these as `make`/`send`/`recv`/`try_recv`/`close`.

**Concurrent HTTP.** `http.serve_concurrent(port, workers, handler)` serves on a worker pool so one slow handler does not block the rest; the handler must be a plain top-level function.

```python
import std.channels

def worker(w: i64):
    var running = True
    while running:
        match channels.recv(0):          # channel handle 0
            case Some(job): handle(job)
            case None: running = False

def main():
    let jobs = channels.make()
    for i in range(100):
        channels.send(jobs, "job-" + str(i))
    channels.close(jobs)
    parallel_run(4, worker)
```

---

## 17. C foreign function interface

An `extern def` declaration names a C function; a native build (`tacqor build --native`) compiles a call to it into a direct C call.

```python
extern def sqrt(x: f64) -> f64 from "m"      # links libm, calls C's sqrt
extern def llabs(n: i64) -> i64              # libc; no library to name
extern def getenv(name: str) -> str          # char* result -> a Tacqor string
extern def to_int(s: str) -> i64 = "atoll"   # = renames the C symbol
```

`from "lib"` names a library to link (`-llib`, or `lib.lib` on MSVC); `= "symbol"` gives the C symbol when it differs from the declared name. Marshalling:

| Tacqor | C |
|---|---|
| `i64` | `long long` |
| `f64` | `double` |
| `bool` | `int` (0 or 1) |
| `str` argument | `const char*` (NUL-terminated, valid only for the call) |
| `str` result | `char*`, copied into a new string (a null pointer becomes `""`) |

Only scalars and strings cross the boundary; any other parameter or result type is a compile-time error. The boundary is unchecked — a wrong declaration is undefined behavior, exactly as in C. Under the bytecode VM a foreign call reports that a native build is required.

---

## 18. Memory model

Memory is managed by **non-atomic reference counting** with a **cycle collector** for the rare reference cycles. Destruction is deterministic: an object is freed as soon as its last reference goes away — no garbage-collector pauses. `gc_collect()` runs a cycle-collection pass on demand; `gc_count()` returns the number of tracked objects. String constants are immortal. The non-atomic counts are sound because the concurrency model never shares a mutable object between threads (§16). Call depth is bounded (native builds run on a 512 MB stack); deep recursion past the frame limit is a runtime error rather than a crash.

---

## 19. Built-in function reference

Built-ins are always in scope (no import). Grouped by purpose; `T`/`U` denote any type, `K` a key type.

**Output, input, process**

| Signature | Description |
|---|---|
| `print(value)` · `write(value)` | Write a value (with / without a trailing newline) to standard output. |
| `eprint(value)` | Write to standard error. |
| `flush()` | Flush standard output. |
| `input() -> str` | Read a line from standard input. |
| `args() -> list[str]` · `argc() -> i64` · `arg(i) -> str` | Program arguments after the name. |
| `run(command: str) -> i64` | Run a shell command; return its exit status. |
| `run_capture(command: str) -> str` | Run a command; return its output. |
| `exit(code: i64)` | End the process with an exit code. |
| `fail(message: str)` | Raise a runtime error. |

**Conversion and formatting**

| Signature | Description |
|---|---|
| `str(value) -> str` | Convert any value to its text form. |
| `int(x) -> i64` | Convert an `f64`, `str` or `bool` to `i64`. |
| `float(x) -> f64` | Convert an `i64`, `f64` or `str` to `f64`. |
| `parse_int(s) -> Option[i64]` · `parse_float(s) -> Option[f64]` | Parse, returning `None` on failure. |
| `chr(code: i64) -> str` · `ord(ch: str) -> i64` | Code point ↔ character. |
| `bytes(s: str) -> list[i64]` · `from_bytes(list[i64]) -> str` | String ↔ byte values. |
| `to_hex(n: i64) -> str` · `from_hex(s) -> Option[i64]` | Hexadecimal. |
| `fixed(x: f64, places: i64) -> str` | Fixed-point formatting. |
| `hash(s: str) -> i64` | 64-bit FNV-1a hash, folded non-negative. |

**Strings** (byte-oriented unless noted)

| Signature | Description |
|---|---|
| `len(s)` | Length in bytes (also lists and dicts). |
| `upper(s)` · `lower(s)` · `strip(s)` | Case folding; trim whitespace. |
| `split(s, sep) -> list[str]` · `join(list[str], sep) -> str` | Split / join. |
| `find(s, part) -> i64` | First byte index, or −1. |
| `replace(s, from, to) -> str` | Replace all occurrences. |
| `starts_with(s, p) -> bool` · `ends_with(s, p) -> bool` | Prefix / suffix test. |
| `repeat(s, n) -> str` | Repeat a string. |
| `chars(s) -> list[str]` · `char_count(s) -> i64` | Code-point-aware split / count. |

**Lists**

| Signature | Description |
|---|---|
| `len(xs)` | Element count. |
| `push(xs, x)` · `pop(xs) -> T` | Append / remove the last (runtime error if empty). |
| `insert(xs, i, x)` · `remove_at(xs, i)` | Insert at / remove at an index. |
| `index_of(xs, x) -> i64` | First index of a value, or −1. |
| `sort(xs)` · `reverse(xs)` · `clear(xs)` | In place; `sort` on `i64`/`f64`/`str`/`bool`. |
| `fill(xs, x)` · `extend(xs, ys)` | Overwrite all / append another list. |
| `sum(xs)` | Sum of a list of `i64` or `f64` (reads flat storage directly). |
| `min_of(xs)` · `max_of(xs)` | Minimum / maximum of a list of `i64`/`f64`/`str` (empty is a runtime error). |

**Bulk numeric (1.4)** — over `list[i64]` or `list[f64]`; both operands the same element type; a length mismatch is a runtime error. The `f64` paths walk the flat backing buffer and auto-vectorize (see `PERFORMANCE.md` §4b); the `i64` paths stay overflow-checked and exact.

| Signature | Description |
|---|---|
| `dot(a, b) -> T` | Inner product `Σ aᵢ·bᵢ` (the `f64` form uses a blocked, associative reduction). |
| `vadd(a, b) -> list[T]` · `vmul(a, b) -> list[T]` | Element-wise sum / product into a new list. |
| `vscale(xs, k) -> list[T]` | Every element times the scalar `k` (same type as the elements), into a new list. |

**Dicts**

| Signature | Description |
|---|---|
| `len(d)` | Entry count. |
| `keys(d) -> list[K]` · `values(d) -> list[V]` | Keys / values. |
| `lookup(d, key) -> Option[V]` | Value for a key, or `None`. |
| `remove_key(d, key)` | Remove an entry. |

**Option and Result**

| Signature | Description |
|---|---|
| `is_some(o)` · `is_none(o)` · `is_ok(r)` · `is_err(r)` | Predicates. |
| `unwrap(x) -> T` | Inner value; runtime error on `None`/`Err`. |
| `unwrap_or(x, default) -> T` | Inner value, or a default. |
| `expect(x, message) -> T` | Inner value, or fail with a message. |

**Math**

| Signature | Description |
|---|---|
| `abs(n)` · `min(a, b)` · `max(a, b)` | On `i64`/`f64` (`min`/`max` also `str`). |
| `wrapping_add(a, b)` · `wrapping_sub(a, b)` · `wrapping_mul(a, b)` | `i64`, two's-complement wraparound, **no overflow check** (1.4) — for hot loops where wraparound is intended. |
| `sqrt(x)` · `pow(a, b)` | Square root; power (`f64`). |
| `floor(x)` · `ceil(x)` · `round(x)` | Rounding to `i64`. |
| `sin cos tan asin acos atan` · `atan2(y, x)` | Trigonometry (`f64`). |
| `exp(x)` · `log(x)` · `log2(x)` · `log10(x)` | Exponential and logarithms. |
| `random_float() -> f64` | A float in [0, 1). |
| `random_int(lo, hi) -> i64` | An integer in [lo, hi], inclusive. |
| `seed(n: i64)` | Seed the generator for a reproducible stream. |

**Files, environment, time**

| Signature | Description |
|---|---|
| `read_text(path) -> str` · `write_text(path, text)` · `append_text(path, text)` | Whole-file I/O. |
| `read_lines(path) -> list[str]` | Read a file as lines. |
| `file_exists(path) -> bool` · `delete_file(path)` | Existence; delete. |
| `list_dir(path) -> list[str]` · `make_dir(path)` | Directory listing; create. |
| `env(name) -> Option[str]` | Environment variable. |
| `unix_time() -> i64` · `time_ms() -> i64` · `now_text() -> str` | Epoch seconds; milliseconds; a timestamp string. |
| `sleep_ms(n: i64)` | Sleep. |

**Memory and concurrency** — `gc_collect()`, `gc_count() -> i64`; `parallel_sum`, `parallel_map`, `parallel_run`; `channel`, `channel_send`, `channel_recv`, `channel_try_recv`, `channel_close` (§16).

**Networking** — `tcp_listen(port) -> Result[i64, str]`, `tcp_accept(server) -> Result[i64, str]`, `tcp_connect(host, port) -> Result[i64, str]`, `tcp_read(socket, limit) -> Result[str, str]`, `tcp_write(socket, data) -> Result[i64, str]`, `tcp_close(socket)`.

---

## 20. Standard library reference

Each module is `import std.NAME`.

**`std.lists`** — higher-order list tools: `map`, `filter`, `reduce`, `any`, `all`, `zip`, `find_first`, `position`, `count_if`, `take`, `drop`, `flatten`, `unique`, `range_list`, `sorted_by`, `max_by`, `each`, `enumerate`; the `Pair[A, B]` struct.

**`std.strings`** — `pad_left`, `pad_right`, `center`, `is_digit`, `is_alpha`, `is_space`, `lines`, `words`, `count`, `capitalize`, `reverse_text`, `strip_prefix`, `strip_suffix`.

**`std.math`** — constants `PI`, `TAU`, `E`, `INT_MAX`, `INT_MIN`; `clamp`, `clamp_f`, `gcd`, `lcm`, `is_prime`, `factorial`, `isqrt`, `lerp`, `to_degrees`, `to_radians`, `mean`.

**`std.collections`** — `Stack[T]`, `Queue[T]`, `Counter`, with `new_stack`, `new_queue`, `new_counter`.

**`std.json`** — a `Json` value with `null`, `boolean`, `number`, `integer`, `string`, `array`, `object`, `set`, `get`; `encode(doc) -> str` and `parse(text) -> Result[Json, str]`.

**`std.http`** — `Request` and `Response` structs; builders `response`, `text`, `html`, `json`, `not_found`; servers `serve`, `serve_count`, `serve_concurrent`; clients `get`, `post`, `request`. HTTP/1.1, plain `http://`, request bodies up to 8 MB.

**`std.testing`** — for `tacqor test`: `assert_eq`, `assert_ne`, `assert_true`, `assert_some`, `assert_ok`.

**`std.time`** — UTC dates on top of `unix_time()`: the `DateTime` record, `from_unix`, `now`, `to_unix`, `iso`, `date_string`, `time_string`, `weekday_name`, `month_name`, `is_leap`, `days_in_month`.

**`std.random`** — `fraction`, `integer`, `below`, `dice`, `boolean`, and on lists `choice` (→ `Option`), `shuffle`, `sample`.

**`std.os`** — `env_or`, and path helpers `join`, `basename`, `dirname`, `extname`, `stem`.

**`std.channels`** — `make`, `send`, `recv`, `try_recv`, `close` (§16).

Full signatures are in [`STDLIB.md`](../STDLIB.md).

---

## 21. Diagnostics

**Compile-time errors** are reported with file, line and column and a caret under the offending text. They cover: type mismatches, mixing `i64` and `f64`, calling with the wrong arity, assigning to an immutable or captured binding, non-exhaustive or unreachable `match` cases, unreachable statements, a path that fails to return, an untyped empty literal or bare `None`, unknown bindings/fields/methods/modules, invalid `dict` key types, a trait a struct fails to implement, and invalid foreign-function signatures.

**Warnings** (from `tacqor check`/`diagnose`, non-fatal) carry a code: `zero-divisor`, `integer-overflow`, `invalid-shift`, `invalid-workers`, `index-out-of-bounds`, `empty-range`, `failing-assert`. See [`ERRORS.md`](ERRORS.md) for the full catalog with fixes.

**Runtime errors** terminate with status 1 (unless inside `attempt`), printing `FILE:LINE:COLUMN: runtime error: MESSAGE` and a traceback. They include: integer overflow, division or modulo by zero, invalid shift count, index out of bounds, missing dictionary key, `unwrap`/`expect` on `None`/`Err`, `pop`/`min_of`/`max_of` on an empty list, assertion failure, `fail(message)`, conversion failures, invalid worker count or parallel capture, an invalid channel handle, `channel_send` on a closed channel, function nesting beyond the limit, value nesting beyond the limit, resource-limit breaches, and I/O failures from builtins that do not return a `Result`.

---

## 22. Limits

| Limit | Value |
|---|---|
| Source file size | 4 MB |
| Bytecode size | 64 MB |
| Locals per function / list-literal size | 65,536 |
| VM value stack | 4,000,000 values |
| Call nesting | 100,000 frames |
| One string or file | 256 MB |
| Parallel workers | 64 |
| Value nesting (print/compare) | 2,000 levels |
| Dictionary key types | `i64` or `str` |

---

## 23. Appendix: keywords and operators

**Keywords** — `def struct let var if elif else while for in break continue return assert and or not True False None range fn match case import as const Some Ok Err Option Result trait extern i64 f64 bool str list dict`.

**Operators** — `+ - * / // % == != < <= > >= and or not & | ^ << >> ~ in ? =` and the compound assignments `+= -= *= /= //= %= &= |= ^= <<= >>=`.

**Built-in constructors and constants** — `Some Ok Err None True False`, and the type names usable in annotations: `i64 f64 bool str list dict Option Result fn`.

---

*This manual documents Tacqor 1.3.0. The language reference ([`../LANGUAGE.md`](../LANGUAGE.md)), standard-library reference ([`../STDLIB.md`](../STDLIB.md)), formal specification ([`SPECIFICATION.md`](SPECIFICATION.md)), diagnostics catalog ([`ERRORS.md`](ERRORS.md)) and tooling guide ([`TOOLING.md`](TOOLING.md)) go deeper on each area.*
