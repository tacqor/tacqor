# A tour of Tacqor

Ten minutes, one feature at a time. Every snippet is a complete program you can save as `tour.tq` and run with `./linux-x64/tacqor run tour.tq`.

## 1. Hello, types

```text
def main():
    let name = "Ada"             # str, immutable binding
    var count = 0                # i64, reassignable
    count += 2
    let ratio = float(count) / 3.0
    print(f"{name} has {count} items, ratio {ratio}")
```

Types are inferred but static: `count = "two"` is a compile error. Integer arithmetic is checked — `9223372036854775807 + 1` is a runtime error, never silent wraparound.

## 2. Collections

```text
def main():
    var scores = [90, 72, 85]
    push(scores, 100)
    sort(scores)
    print(scores[1:3])                     # [85, 90]
    var ages: dict[str, i64] = {}
    ages["ada"] = 36
    for key in keys(ages):
        print(f"{key}: {ages[key]}")
    print("ada" in ages)
```

## 3. Structs and methods

```text
struct Account:
    owner: str
    balance: i64

    def deposit(self, amount: i64):
        self.balance += amount

def main():
    let acct = Account("Ada", 10)
    acct.deposit(5)
    print(acct)                            # Account(owner="Ada", balance=15)
```

Structs are reference values: passing `acct` to a function shares it.

## 4. Option and Result instead of exceptions

```text
def find_user(id: i64) -> Option[str]:
    if id == 1:
        return Some("Ada")
    return None

def parse_age(text: str) -> Result[i64, str]:
    match parse_int(text):
        case Some(n):
            if n < 0:
                return Err("negative age")
            return Ok(n)
        case None:
            return Err(f"not a number: {text}")

def describe(id: i64, age_text: str) -> Result[str, str]:
    let age = parse_age(age_text)?         # returns the Err early
    let name = unwrap_or(find_user(id), "stranger")
    return Ok(f"{name}, {age}")

def main():
    print(describe(1, "36"))               # Ok("Ada, 36")
    print(describe(2, "x"))                # Err("not a number: x")
    match describe(1, "-5"):
        case Ok(text):
            print(text)
        case Err(problem):
            print("problem: " + problem)
```

`match` must handle both arms. `?` propagates failures; the compiler checks that the enclosing function returns a compatible `Option`/`Result`.

## 5. Closures and generics

```text
import std.lists

def apply_twice[T](value: T, f: fn(T) -> T) -> T:
    return f(f(value))

def main():
    let bonus = 10
    let add_bonus = fn(x: i64) => x + bonus          # captures bonus by value
    print(apply_twice(5, add_bonus))                  # 25
    print(apply_twice("ha", fn(s: str) => s + s))     # hahahaha
    let evens = lists.filter(lists.range_list(0, 10), fn(n: i64) => n % 2 == 0)
    print(lists.map(evens, fn(n: i64) => n * n))
```

Generic functions are instantiated per concrete type, so `apply_twice[i64]` and `apply_twice[str]` are two ordinary functions in the output — no boxing, no dynamic dispatch.

## 6. Generic structs

```text
struct Stack[T]:
    items: list[T]

    def push(self, item: T):
        push(self.items, item)

    def pop(self) -> Option[T]:
        if len(self.items) == 0:
            return None
        return Some(pop(self.items))

def main():
    let s = Stack[str]([])
    s.push("a")
    s.push("b")
    print(s.pop())                         # Some("b")
```

## 7. Traits

```text
trait Animal:
    def sound(self) -> str

struct Dog(Animal):
    name: str

    def sound(self) -> str:
        return self.name + " says woof"

struct Cat(Animal):
    def sound(self) -> str:
        return "meow"

def chorus(animals: list[Animal]):
    for a in animals:
        print(a.sound())

def loudest[T: Animal](a: T) -> str:
    return upper(a.sound())

def main():
    chorus([Dog("Rex"), Cat()])
    print(loudest(Dog("Fido")))
```

`list[Animal]` holds different structs and dispatches each call at runtime; `T: Animal` is a bound checked when the generic function is instantiated and resolved statically inside.

## 8. Modules

`shapes.tq`:
```text
const PI = 3.141592653589793

struct Circle:
    radius: f64

    def area(self) -> f64:
        return PI * self.radius * self.radius
```

`main.tq`:
```text
import shapes
import std.strings

def main():
    let c = shapes.Circle(2.0)
    print(strings.pad_left(str(round(c.area())), 5, " "))
```

The `std` folder next to the executable is on the import path; your own modules are found next to the importing file or through `TACQOR_PATH`.

## 9. Tests

`tests.tq`:
```text
import std.testing
import shapes

def test_area():
    testing.assert_eq(round(shapes.Circle(1.0).area()), 3)
```

`tacqor test tests.tq` runs every `test_*` function and reports PASS/FAIL.

## 10. Ship it

```text
./linux-x64/tacqor build --native main.tq -o main     # machine code, needs c++ on PATH
./linux-x64/tacqor build main.tq -o main_vm           # bytecode + embedded VM, no compiler needed
./main
```

Both executables run the same program with the same checks; the native one is 1.5–18× faster depending on the workload (most on arithmetic and loops, least on allocation-heavy code; see `docs/PERFORMANCE.md`).

## 11. A web service in ten lines

```text
import std.http

def handler(request: http.Request) -> http.Response:
    if request.path == "/":
        return http.text(200, "hello from Tacqor")
    return http.not_found()

def main():
    http.serve(8080, handler)
```

Run it, open `http://localhost:8080/`, and try `http.get(...)` from another program. A handler that fails answers 500 and the server keeps going (`attempt` does that for you).

## 12. And the rest

`tacqor repl` for a scratchpad, `tacqor add ../somelib` (or a git URL) to install a package into `packages/`, `parallel_map(n, 8, fn(i: i64) => work(i))` for data parallelism, and a cycle collector that quietly frees self-referential structures. See `LANGUAGE.md` and `STDLIB.md` for everything.
