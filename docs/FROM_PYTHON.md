# Tacqor for Python Programmers

Tacqor looks like Python and is meant to feel like it. This guide lists what carries over unchanged, what changes, and how to translate the Python idioms you use every day. Every Tacqor snippet below compiles and runs as shown.

## 1. What stays the same

- Indentation-based blocks, `def`, `if`/`elif`/`else`, `while`, `for … in`, `break`, `continue`, `return`, `assert`, `and`/`or`/`not`, `in`, `#` comments.
- `print`, `len`, `str`, `int`, `float`, `abs`, `min`, `max`, `range`, `sum`, `sorted`-style helpers in `std.lists`.
- `True`/`False`, `None`, `[1, 2]`, `{"k": 1}`, slices `a[1:3]`, `a[:2]`, `a[2:]`, f-strings `f"{x}"`.
- Operator precedence, `//` floor division, `%` sign rule, `**`-style power via `pow`.
- Lists and dicts are mutable reference values shared on assignment, exactly as in Python.

## 2. What changes

| Python | Tacqor | Why |
|---|---|---|
| `x = 1` | `let x = 1` (immutable) or `var x = 1` | Bindings are declared; most never change. |
| `def f(a, b):` | `def f(a: i64, b: str) -> str:` | Parameters and results are typed; locals are inferred. |
| `class` | `struct` + methods; `trait` for shared behaviour | No inheritance. |
| `None` for "no value" | `Option[T]`: `Some(x)` / `None`, handled with `match` or `?` | The compiler makes you handle the missing case. |
| `raise` / `try` / `except` | `Result[T, E]`, `?`, `attempt` | Failures are values in the type; see §7. |
| `int` of any size | `i64` with overflow checked at runtime | No silent wrap, no bignums. |
| `str` of code points | `str` of UTF-8 bytes; `chars(s)` for code points | Matches files and sockets. |
| `a[-1]` | `a[len(a) - 1]` | Negative indexes are errors, not wraparound. |
| `for i, x in enumerate(xs)` | `for i in range(len(xs)):` then `xs[i]`, or `lists.enumerate(xs)` | |
| `x if c else y` | `if`/`else` statement, or a small function | No conditional expression. |
| list comprehensions | `lists.map`, `lists.filter`, or a loop with `push` | |
| `lambda x: x * 2` | `fn(x: i64) => x * 2` | Parameter types are written; result inferred. |
| `import os`, `from m import f` | `import std.lists` then `lists.map(...)` | No `from … import`; names are always qualified. |
| `if __name__ == "__main__":` | `def main():` | The entry point is `main`. |
| `global` | not available | Pass values in and out, or use a struct. |
| `is`, `id()` | not available | Compare with `==` (structural). |
| `with open(p) as f:` | `read_text(p)`, `write_text(p, s)`, `read_lines(p)` | Files are read and written whole. |
| `**kwargs`, default arguments | not available | Pass a struct for optional settings. |
| `while x := f():` | `var x = f()` then `while …:` | No walrus. |
| `a < b < c` | `a < b and b < c` | Comparisons do not chain. |
| `dict.get(k, d)` | `unwrap_or(lookup(d, k), default)` | |
| `d[k] = d.get(k, 0) + 1` | `if k in d: d[k] += 1 else: d[k] = 1` | |

## 3. Side by side

### Word count

Python:

```python
import sys
counts = {}
for line in open(sys.argv[1]):
    for word in line.split():
        counts[word] = counts.get(word, 0) + 1
for word, n in sorted(counts.items(), key=lambda kv: -kv[1])[:10]:
    print(word, n)
```

Tacqor:

```text
import std.lists
import std.strings

def main():
    var counts: dict[str, i64] = {}
    for line in read_lines(args()[0]):
        for word in strings.words(line):
            if word in counts:
                counts[word] += 1
            else:
                counts[word] = 1
    let ranked = lists.sorted_by(keys(counts), fn(w: str) => -counts[w])
    for word in lists.take(ranked, 10):
        print(f"{word} {counts[word]}")
```

Differences worth noticing: the dictionary's types are declared once; `read_lines` returns the file as a list; the closure passed to `sorted_by` captures `counts` (shared, not copied, because dicts are reference values); `args()` are the program's arguments without the script name.

### A class with methods

Python:

```python
class Account:
    def __init__(self, owner, balance=0):
        self.owner = owner
        self.balance = balance
    def withdraw(self, amount):
        if amount > self.balance:
            raise ValueError("insufficient funds")
        self.balance -= amount
        return self.balance
```

Tacqor:

```text
struct Account:
    owner: str
    balance: i64

    def withdraw(self, amount: i64) -> Result[i64, str]:
        if amount > self.balance:
            return Err("insufficient funds")
        self.balance -= amount
        return Ok(self.balance)

def main():
    let acct = Account("ann", 100)
    match acct.withdraw(30):
        case Ok(left):
            print(f"left: {left}")
        case Err(problem):
            print(problem)
    print(acct.withdraw(500))
```

Output: `left: 70` then `Err("insufficient funds")`. There is no constructor to write — `Account("ann", 100)` fills the fields in order — and the failure is part of `withdraw`'s type, so a caller cannot forget it.

### Optional values

Python:

```python
def find_user(users, name):
    for u in users:
        if u["name"] == name:
            return u
    return None

u = find_user(users, "bob")
if u is not None:
    print(u["age"])
```

Tacqor:

```text
struct User:
    name: str
    age: i64

def find_user(users: list[User], name: str) -> Option[User]:
    for u in users:
        if u.name == name:
            return Some(u)
    return None

def main():
    let users = [User("ann", 31), User("bob", 27)]
    match find_user(users, "bob"):
        case Some(u):
            print(u.age)
        case None:
            print("no such user")
```

### Exceptions to `Result` and `?`

Python:

```python
def load(path):
    try:
        with open(path) as f:
            return int(f.read())
    except (OSError, ValueError) as e:
        raise RuntimeError(f"bad config: {e}")
```

Tacqor:

```text
def read_number(path: str) -> Result[i64, str]:
    if not file_exists(path):
        return Err(f"missing file: {path}")
    match parse_int(strip(read_text(path))):
        case Some(n):
            return Ok(n)
        case None:
            return Err(f"not a number in {path}")

def load(path: str) -> Result[i64, str]:
    let n = read_number(path)?      # returns the Err to our caller if there is one
    return Ok(n * 2)

def main():
    write_text("config.txt", "21")
    print(load("config.txt"))       # Ok(42)
    print(load("missing.txt"))      # Err("missing file: missing.txt")
```

`?` is the equivalent of letting an exception propagate, except that the function's signature shows it can happen.

### List comprehensions

Python:

```python
squares = [x * x for x in nums if x % 2 == 0]
total = sum(squares)
```

Tacqor:

```text
import std.lists

def main():
    let nums = [1, 2, 3, 4, 5, 6]
    let evens = lists.filter(nums, fn(x: i64) => x % 2 == 0)
    let squares = lists.map(evens, fn(x: i64) => x * x)
    print(sum(squares))             # 56
```

### Generators and iteration

Python generators have no direct equivalent. Build the list (`lists.range_list`, a loop with `push`) or write a struct with a `next()` method returning `Option[T]`:

```text
struct Countdown:
    n: i64

    def next(self) -> Option[i64]:
        if self.n <= 0:
            return None
        self.n -= 1
        return Some(self.n + 1)

def main():
    let c = Countdown(3)
    var item = c.next()
    while is_some(item):
        print(unwrap(item))
        item = c.next()
```

## 4. Types, briefly

You write types on function parameters, results and struct fields; the compiler infers the rest. The common ones: `i64`, `f64`, `bool`, `str`, `list[T]`, `dict[K, V]`, `Option[T]`, `Result[T, E]`, `fn(A) -> R`, struct names. There are no implicit conversions: `float(i)`, `int(f)`, `str(x)` are explicit, and `"n=" + str(n)` or `f"n={n}"` is how you build strings from numbers.

Empty literals need a type the compiler can see: `var xs: list[i64] = []`.

## 5. Mutability

`let` bindings cannot be reassigned; `var` ones can. Neither freezes the object: `let xs = [1]` then `push(xs, 2)` is fine, `xs = [3]` is not. Struct fields are always assignable (`p.x = 3`). Strings are immutable.

## 6. Modules

A file is a module. `import std.strings` makes its functions available as `strings.words(...)`; your own files import by path relative to the importing file (`import util.text` → `util/text.tq`). There is no package-level `__init__`; a folder module is `name/lib.tq`.

## 7. Errors in detail

- Expected failures return `Result[T, E]` (any `E`, usually `str`). Handle with `match`, forward with `?`, default with `unwrap_or`.
- Bugs (out-of-range index, `unwrap` on `None`, overflow, failed `assert`) are runtime errors that stop the program with a traceback, like an uncaught exception.
- `attempt(fn() => work())` turns a runtime error into `Err(message)`. Use it only where the program must continue no matter what (a server loop); it is not a general `try`.
- `fail("message")` raises a runtime error deliberately.

## 8. Standard library map

| Python | Tacqor |
|---|---|
| `str.split`, `join`, `strip`, `upper`, `replace`, `find`, `startswith` | builtins `split`, `join`, `strip`, `upper`, `replace`, `find`, `starts_with` (also callable as `s.upper()`) |
| `str.splitlines`, `.title()`, `.center()`, `.isdigit()` | `std.strings`: `lines`, `capitalize`, `center`, `is_digit`, … |
| `list.append/pop/insert/remove/index/sort/reverse` | `push`, `pop`, `insert`, `remove_at`, `index_of`, `sort`, `reverse` |
| `map`, `filter`, `functools.reduce`, `any`, `all`, `zip`, `enumerate` | `std.lists`: `map`, `filter`, `reduce`, `any`, `all`, `zip`, `enumerate` |
| `sorted(xs, key=f)`, `max(xs, key=f)` | `lists.sorted_by(xs, f)`, `lists.max_by(xs, f)` |
| `dict.keys/values/get/pop` | `keys`, `values`, `lookup`, `remove_key` |
| `collections.Counter`, `deque` | `std.collections`: `Counter`, `Queue[T]`, `Stack[T]` |
| `math` | builtins `sqrt`, `pow`, `sin`, `cos`, `log`, … and `std.math` |
| `json.loads/dumps` | `std.json`: `parse`, `encode` |
| `random` | `random_int(lo, hi)`, `random_float()`, `seed(n)` |
| `time.time()`, `time.sleep()` | `unix_time()`, `time_ms()`, `sleep_ms(n)` |
| `os.path.exists`, `os.listdir`, `os.makedirs`, `os.remove` | `file_exists`, `list_dir`, `make_dir`, `delete_file` |
| `os.environ.get` | `env(name)` → `Option[str]` |
| `subprocess.run` | `run(cmd)` → status, `run_capture(cmd)` → output |
| `sys.argv[1:]`, `sys.exit` | `args()`, `exit(code)` |
| `input()` | `input()` → `str` (`""` at end of input) |
| `http.server`, `urllib` | `std.http`: `serve`, `get`, `post` |
| `unittest` | `def test_*()` + `tacqor test`; `std.testing` |

Full signatures: `STDLIB.md`.

## 9. Running and shipping

| Python | Tacqor |
|---|---|
| `python app.py` | `tacqor app.tq` |
| `python -m pytest` | `tacqor test tests.tq` |
| `pyflakes`, `mypy` | `tacqor check app.tq` (built in) |
| `pip install`, `requirements.txt` | `tacqor add URL`, `tacqor.pkg`, `tacqor install` |
| PyInstaller | `tacqor build app.tq -o app` (or `--native`) |
| `python -i` | `tacqor repl` |

## 10. Performance expectations

The VM is 1.1–5× faster than CPython on typical code; native builds are 1.6–90× faster, most on arithmetic and loops. Code written the Python way (lists, dicts, strings) benefits least; code with numeric loops benefits most. `PERFORMANCE.md` has the numbers and the habits that matter.
