# Tacqor for Rust Programmers

Tacqor borrows Rust's error model and type discipline and leaves out the ownership system. If you know Rust, you know most of Tacqor already; this guide covers what is the same, what is deliberately different, and how the familiar idioms are written. Every Tacqor snippet compiles and runs as shown.

## 1. What carries over

| Rust | Tacqor |
|---|---|
| `Option<T>`, `Some`, `None` | `Option[T]`, `Some`, `None` |
| `Result<T, E>`, `Ok`, `Err` | `Result[T, E]`, `Ok`, `Err` |
| `?` | `?` (same semantics: early return of the `None`/`Err`) |
| `match` with exhaustiveness | `match` … `case` with exhaustiveness |
| `unwrap`, `unwrap_or`, `expect`, `is_some`, `is_ok` | same names, as functions or methods |
| `struct` with `impl` methods | `struct` with methods inside the struct |
| `trait` + `impl Trait for S` + `dyn Trait` | `trait` + `struct S(Trait)` + trait-typed values |
| generics `fn f<T: Trait>(x: T)` monomorphized | `def f[T: Trait](x: T)` monomorphized |
| closures `|x| x * 2` | `fn(x: i64) => x * 2` |
| `let` (immutable) / `let mut` | `let` / `var` |
| `const` | `const` |
| checked overflow (debug) | checked overflow, always |
| `i64`, `f64`, `bool`, `String`, `Vec<T>`, `HashMap<K, V>` | `i64`, `f64`, `bool`, `str`, `list[T]`, `dict[K, V]` |
| `#[test]` | `def test_*()` + `tacqor test` |
| `cargo new`, `cargo test`, `cargo build --release` | `tacqor new`, `tacqor test`, `tacqor build --native` |

## 2. What is different, and why

| Rust | Tacqor | Reason |
|---|---|---|
| Ownership, borrowing, lifetimes | None. Containers, structs and closures are reference values; scalars and strings are copied. | The largest source of Rust's learning curve; Tacqor pays a constant factor in refcounting instead (`WHITEPAPER.md` §2.4). |
| `&`, `&mut`, `Box`, `Rc`, `RefCell`, `Arc` | Not needed. Passing a list shares it; struct fields are always mutable. | One way to pass values. |
| `enum` with data | `Option`/`Result` only; model other sums as a struct with a `kind` field or as traits. | Keeps the type system small (see `ROADMAP.md` for user enums). |
| `impl` blocks, `self`/`&self`/`&mut self` | Methods are written inside the struct; the receiver is always `self`. | No borrow distinction to express. |
| Traits with default methods, associated types, generic traits | Traits list signatures only. | Simplicity; may grow. |
| `Vec<u8>`, `i32`, `u64`, `usize`, `char` | `i64`, `f64`, `bool`, `str` only; `bytes(s)` gives a `list[i64]`. | One integer type removes conversion bugs. |
| `String` / `&str` | One `str` type, immutable UTF-8 bytes. | No borrowing, so no view type. |
| `panic!` / `unwrap` | `fail(msg)` / `unwrap`; runtime errors print a traceback and exit 1 | Same semantics; `attempt(f)` catches at boundaries. |
| `std::thread`, `Mutex`, channels, `async` | `parallel_sum` / `parallel_map` only | No shared mutable state across threads (refcounts are not atomic). |
| `mod`, `use`, `pub` | `import a.b`; everything in a module is public; names stay qualified | Smaller surface. |
| Shadowing with `let x = …` twice | Not allowed in the same scope | Avoids accidental re-typing. |
| `loop`, `while let`, labelled breaks | `while True:`, `while is_some(x):`, innermost-loop `break` | Python's control flow. |
| `if let Some(x) = …` | `match … case Some(x): … case None:` | One form of destructuring. |
| `impl Display` / `{:?}` | `str(x)` and `f"{x}"` print every value structurally | Nothing to derive. |
| `#[derive(PartialEq, Clone)]` | `==` is structural for every type; no `clone` for containers (slice `xs[:]` copies a list) | |
| Macros | None | |
| Cargo registry | `tacqor add URL` from any git repository or folder | No central registry to run. |

## 3. Side by side

### Errors and `?`

Rust:

```rust
fn parse_pair(s: &str) -> Result<(i64, i64), String> {
    let mut it = s.split(',');
    let a = it.next().ok_or("missing a")?.trim().parse::<i64>().map_err(|e| e.to_string())?;
    let b = it.next().ok_or("missing b")?.trim().parse::<i64>().map_err(|e| e.to_string())?;
    Ok((a, b))
}
```

Tacqor:

```text
struct Pair:
    a: i64
    b: i64

def number(text: str, what: str) -> Result[i64, str]:
    match parse_int(strip(text)):
        case Some(n):
            return Ok(n)
        case None:
            return Err(f"{what} is not a number: {text}")

def parse_pair(s: str) -> Result[Pair, str]:
    let parts = split(s, ",")
    if len(parts) != 2:
        return Err("expected two numbers")
    let a = number(parts[0], "a")?
    let b = number(parts[1], "b")?
    return Ok(Pair(a, b))

def main():
    print(parse_pair("3, 4"))        # Ok(Pair(a=3, b=4))
    print(parse_pair("3, x"))        # Err("b is not a number:  x")
```

There are no tuples; a two-field struct is the idiom. `parse_int` returns `Option[i64]` rather than a `Result` with a typed error, and the conversion from `Option` to `Result` is a `match` rather than `ok_or`.

### Traits and dynamic dispatch

Rust:

```rust
trait Shape { fn area(&self) -> f64; }
struct Circle { r: f64 }
impl Shape for Circle { fn area(&self) -> f64 { 3.14159 * self.r * self.r } }
fn total(shapes: &[Box<dyn Shape>]) -> f64 { shapes.iter().map(|s| s.area()).sum() }
```

Tacqor:

```text
trait Shape:
    def area(self) -> f64

struct Circle(Shape):
    r: f64

    def area(self) -> f64:
        return 3.14159 * self.r * self.r

struct Square(Shape):
    side: f64

    def area(self) -> f64:
        return self.side * self.side

def total(shapes: list[Shape]) -> f64:
    var sum = 0.0
    for s in shapes:
        sum += s.area()
    return sum

def main():
    let shapes: list[Shape] = [Circle(1.0), Square(2.0)]
    print(total(shapes))             # 7.14159
```

A struct names its traits in parentheses and the compiler checks the method set. A `list[Shape]` holds any implementing struct; no `Box`, no `dyn`. Static dispatch is the generic form: `def total[T: Shape](shapes: list[T]) -> f64` compiles one copy per concrete `T`, like Rust's `impl Trait` parameters.

### Generics with bounds

Rust:

```rust
fn largest<T: PartialOrd + Copy>(items: &[T]) -> Option<T> { … }
```

Tacqor has no `PartialOrd`; comparisons are built in for `i64`, `f64` and `str`, so generic ordering goes through a key function:

```text
import std.lists

struct Item:
    name: str
    weight: i64

def main():
    let items = [Item("a", 5), Item("b", 9), Item("c", 2)]
    match lists.max_by(items, fn(i: Item) => i.weight):
        case Some(heaviest):
            print(heaviest.name)    # b
        case None:
            print("empty")
```

### Closures and capture

Rust closures borrow or move their environment; Tacqor closures copy scalars and strings and share containers at creation time. There is no `move` and no `FnMut`: a closure cannot assign to a captured variable. To accumulate, share a container:

```text
import std.lists

def main():
    let seen: list[str] = []
    let record = fn(word: str):
        push(seen, word)
    lists.each(["x", "y"], record)
    print(seen)                      # ["x", "y"]
```

### Iterators

There is no iterator trait or lazy adaptor chain. `std.lists` provides eager `map`, `filter`, `reduce`, `zip`, `enumerate`, `take`, `drop`, `flatten`, `unique`, `sorted_by`; each returns a new list. For large pipelines, write one loop.

```text
import std.lists

def main():
    let xs = lists.range_list(1, 11)
    let odd_squares = lists.map(lists.filter(xs, fn(x: i64) => x % 2 == 1), fn(x: i64) => x * x)
    print(lists.reduce(odd_squares, 0, fn(acc: i64, x: i64) => acc + x))   # 165
```

### Mutation through parameters

Rust distinguishes `&T` from `&mut T`. In Tacqor a list, dict or struct parameter is always the shared object; a function that modifies it modifies the caller's value. Scalars and strings are copies.

```text
def bump(counts: dict[str, i64], key: str):
    if key in counts:
        counts[key] += 1
    else:
        counts[key] = 1

def main():
    var c: dict[str, i64] = {}
    bump(c, "a")
    bump(c, "a")
    print(c)                         # {"a": 2}
```

If you need a copy, make one: `let copy = xs[:]` for lists; for dicts and structs, build a new value.

### Panics and recovery

Rust's `catch_unwind` is rarely used; Tacqor's `attempt` fills the same niche:

```text
def risky(n: i64) -> i64:
    let xs = [1, 2, 3]
    return xs[n]

def main():
    print(attempt(fn() => risky(1)))     # Ok(2)
    print(attempt(fn() => risky(7)))     # Err("list index out of bounds")
```

## 4. Memory model

Reference counting plus a cycle collector. Objects are freed when the last reference disappears; cycles are reclaimed by a trial-deletion collector that runs after a few hundred thousand container allocations. Cost: one increment/decrement per reference copy. You never write a destructor, there is no `Drop`, and there is no `unsafe`. Performance consequences are in `PERFORMANCE.md` §5: native Tacqor is 1.4–3× slower than Rust on compute-bound code.

## 5. Concurrency

`parallel_sum(count, workers, f)` and `parallel_map(count, workers, f)` run `f: fn(i64) -> i64` over `0..count` on `workers` threads and return exactly the same result as the sequential loop. `f` may capture only `i64`, `f64` and `bool` values, enforced at compile time, so no thread ever shares a mutable object — the Send/Sync guarantee by construction rather than by trait.

## 6. Tooling map

| Cargo | Tacqor |
|---|---|
| `cargo new app` | `tacqor new app` |
| `cargo run -- args` | `tacqor run main.tq -- args` or `tacqor main.tq args` |
| `cargo check` | `tacqor check main.tq` |
| `cargo test` | `tacqor test tests.tq` |
| `cargo build --release` | `tacqor build --native main.tq -o app` |
| `cargo add crate` | `tacqor add URL` |
| `Cargo.toml` / `Cargo.lock` | `tacqor.pkg` |
| `rustfmt` | none yet (see `STYLE.md`) |
| `clippy` | warnings in `tacqor check` |
| `cargo expand` | `tacqor dump` (bytecode) and `tacqor emit` (C++) |

## 7. What you will miss, and what to do instead

| Missing | Alternative |
|---|---|
| User-defined enums | A struct with a `kind: str` field plus payload fields, or one struct per case implementing a trait |
| Tuples | A small struct, or `Pair[A, B]` from `std.lists` |
| Pattern matching on structs and nested patterns | `match` on the `Option`/`Result`, then `if` on fields |
| `impl Trait` return types | Return the trait type: `def make() -> Shape` |
| Slices and views | Slicing copies (`xs[a:b]`); pass the list and indexes for a view |
| Integer types other than `i64` | Mask with `& 0xFF` and friends; `to_hex`/`from_hex`, `bytes`/`from_bytes` for byte work |
| `Iterator` | Eager `std.lists` functions, or a struct with a `next() -> Option[T]` method |
| Lifetimes | Nothing to write; references are never dangling |
