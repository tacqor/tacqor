# Tacqor Cookbook

Working recipes for everyday tasks. Every snippet is a complete program unless it says otherwise; save it as `recipe.tq` and run `tacqor recipe.tq`. Recipes assume the standard library is installed (`sh install.sh`) or that you run from the distribution folder.

## 1. Command-line programs

### Arguments, exit codes, usage text

```text
def usage():
    eprint("usage: tool [--verbose] FILE...")
    exit(2)

def main():
    var verbose = False
    var files: list[str] = []
    for argument in args():
        if argument == "--verbose":
            verbose = True
        elif starts_with(argument, "--"):
            usage()
        else:
            push(files, argument)
    if len(files) == 0:
        usage()
    for path in files:
        if verbose:
            eprint("reading " + path)
        print(f"{path}: {len(read_lines(path))} lines")
```

### Reading standard input line by line

```text
def main():
    var count = 0
    while True:
        let line = input()
        if line == "":
            break
        count += len(strip(line))
    print(count)
```

(`input()` returns `""` at end of input; a genuinely empty line also stops this loop — compare against a sentinel if that matters.)

### A script with a shebang

```text
#!/usr/bin/env tacqor
def main():
    print("hello from " + join(args(), " "))
```

`chmod +x script.tq && ./script.tq a b` works once `tacqor` is on `PATH`.

## 2. Files

### Read, transform, write

```text
import std.strings

def main():
    let lines = read_lines("input.txt")
    var out: list[str] = []
    for line in lines:
        if strip(line) != "" and not starts_with(line, "#"):
            push(out, strings.capitalize(line))
    write_text("output.txt", join(out, "\n") + "\n")
    print(f"wrote {len(out)} lines")
```

### Safe file access with Result

Built-ins like `read_text` raise a runtime error when the file is missing. Wrap them in `attempt` to turn that into a value:

```text
def load(path: str) -> Result[str, str]:
    if not file_exists(path):
        return Err(f"{path} does not exist")
    return attempt(fn() => read_text(path))

def main():
    match load("config.txt"):
        case Ok(text):
            print(len(text))
        case Err(problem):
            eprint(problem)
            exit(1)
```

### Walking a directory

```text
def main():
    for name in list_dir("."):
        if ends_with(name, ".tq"):
            print(f"{name}: {len(read_text(name))} bytes")
```

## 3. Text processing

### Word frequencies

```text
import std.collections
import std.strings

def main():
    let counter = collections.new_counter()
    for word in strings.words(lower(read_text("book.txt"))):
        counter.add(word)
    match counter.most_common():
        case Some(word):
            print(f"most common: {word} ({counter.get(word)})")
        case None:
            print("empty")
```

### Parsing a simple CSV

```text
struct Row:
    name: str
    amount: i64

def parse_row(line: str) -> Result[Row, str]:
    let parts = split(line, ",")
    if len(parts) != 2:
        return Err(f"expected 2 columns: {line}")
    match parse_int(strip(parts[1])):
        case Some(amount):
            return Ok(Row(strip(parts[0]), amount))
        case None:
            return Err(f"bad amount in: {line}")

def main():
    var total = 0
    for line in read_lines("data.csv")[1:]:          # skip the header
        match parse_row(line):
            case Ok(row):
                total += row.amount
            case Err(problem):
                eprint(problem)
    print(total)
```

### Unicode-aware operations

`len` counts bytes. Use `chars`/`char_count` for code points:

```text
def main():
    let text = "héllo wörld"
    print(len(text))              # 13 bytes
    print(char_count(text))       # 11 code points
    let letters = chars(text)
    reverse(letters)              # reverses code points, not bytes
    print(join(letters, ""))
```

## 4. Data structures

### A typed stack and queue

```text
import std.collections

def main():
    let stack = collections.new_stack[str]()
    stack.push("a")
    stack.push("b")
    print(stack.pop())              # Some("b")
    let queue = collections.new_queue[i64]()
    queue.push(1)
    queue.push(2)
    print(queue.pop())              # Some(1)
```

### Grouping with a dict of lists

```text
def main():
    let words = ["apple", "avocado", "banana", "blueberry", "cherry"]
    var groups: dict[str, list[str]] = {}
    for word in words:
        let key = word[:1]
        if not (key in groups):
            groups[key] = []
        push(groups[key], word)
    for key in keys(groups):
        print(f"{key}: {join(groups[key], ", ")}")
```

### A linked structure with Option

```text
struct Node:
    value: i64
    next: Option[Node]

def length(node: Option[Node]) -> i64:
    var count = 0
    var current = node
    while True:
        match current:
            case Some(n):
                count += 1
                current = n.next
            case None:
                return count

def main():
    let chain = Some(Node(1, Some(Node(2, Some(Node(3, None))))))
    print(length(chain))
```

### Sorting by a key

```text
import std.lists

struct Person:
    name: str
    age: i64

def main():
    let people = [Person("Ada", 36), Person("Bob", 25), Person("Cy", 31)]
    for p in lists.sorted_by(people, fn(p: Person) => p.age):
        print(f"{p.name} {p.age}")
    print(lists.max_by(people, fn(p: Person) => len(p.name)))
```

## 5. Error handling patterns

### Propagating with `?`

```text
def read_port(path: str) -> Result[i64, str]:
    let text = attempt(fn() => read_text(path))?        # Err if the file cannot be read
    match parse_int(strip(text)):
        case Some(port):
            if port < 1 or port > 65535:
                return Err(f"port out of range: {port}")
            return Ok(port)
        case None:
            return Err("port file does not contain a number")

def main():
    print(read_port("port.txt"))
```

### Defaults and early exits

```text
def main():
    let port = unwrap_or(parse_int(unwrap_or(env("PORT"), "")), 8080)
    let home = expect(env("HOME"), "HOME must be set")
    print(f"{home} {port}")
```

### Validating input with a custom error struct

```text
struct Invalid:
    field: str
    reason: str

def validate(name: str, age: i64) -> Result[bool, Invalid]:
    if name == "":
        return Err(Invalid("name", "empty"))
    if age < 0:
        return Err(Invalid("age", "negative"))
    return Ok(True)

def main():
    match validate("", 3):
        case Ok(ok):
            print("valid")
        case Err(problem):
            print(f"{problem.field}: {problem.reason}")
```

## 6. Functions, closures and generics

### Building a pipeline

```text
import std.lists

def main():
    let numbers = lists.range_list(1, 11)
    let result = lists.reduce(
        lists.map(lists.filter(numbers, fn(n: i64) => n % 2 == 1), fn(n: i64) => n * n),
        0, fn(acc: i64, n: i64) => acc + n)
    print(result)                                    # 165
```

### Memoization with a captured dict

Captures are by value, but a dict is a reference, so a captured dict is shared:

```text
def main():
    let cache: dict[i64, i64] = {}
    let slow_square = fn(n: i64) -> i64:
        match lookup(cache, n):
            case Some(v):
                return v
            case None:
                let v = n * n
                cache[n] = v
                return v
    print(slow_square(12))
    print(slow_square(12))
    print(len(cache))                                # 1
```

### A generic function with a trait bound

```text
trait Describe:
    def describe(self) -> str

struct Cat(Describe):
    name: str
    def describe(self) -> str:
        return "cat " + self.name

def show_all[T: Describe](items: list[T]):
    for item in items:
        print(item.describe())

def main():
    show_all([Cat("Tom"), Cat("Kit")])
```

## 7. Structs and traits

### Polymorphic lists

```text
trait Shape:
    def area(self) -> f64

struct Rect(Shape):
    w: f64
    h: f64
    def area(self) -> f64:
        return self.w * self.h

struct Circle(Shape):
    r: f64
    def area(self) -> f64:
        return 3.141592653589793 * self.r * self.r

def main():
    let shapes: list[Shape] = [Rect(2.0, 3.0), Circle(1.0)]
    var total = 0.0
    for s in shapes:
        total += s.area()
    print(fixed(total, 2))
```

### Builder-style methods

Methods mutate `self` in place; return `self` for chaining:

```text
struct Query:
    parts: list[str]

    def where(self, clause: str) -> Query:
        push(self.parts, "WHERE " + clause)
        return self

    def limit(self, n: i64) -> Query:
        push(self.parts, f"LIMIT {n}")
        return self

    def text(self) -> str:
        return join(self.parts, " ")

def main():
    print(Query(["SELECT *"]).where("x > 1").limit(10).text())
```

## 8. JSON

```text
import std.json

def main():
    let doc = json.object()
    json.set(doc, "name", json.string("Ada"))
    json.set(doc, "tags", json.array([json.string("math"), json.string("code")]))
    json.set(doc, "age", json.integer(36))
    let text = json.encode(doc)
    print(text)
    match json.parse(text):
        case Ok(parsed):
            print(unwrap(unwrap(parsed.get("name")).as_text()))
            print(len(unwrap(parsed.get("tags")).items))
        case Err(problem):
            print(problem)
```

## 9. HTTP

### A JSON API server

```text
import std.http
import std.json

def handler(request: http.Request) -> http.Response:
    match request.path:
        case "/health":
            return http.text(200, "ok")
        case "/echo":
            let doc = json.object()
            json.set(doc, "method", json.string(request.method))
            json.set(doc, "body", json.string(request.body))
            return http.json(200, json.encode(doc))
        case _:
            return http.not_found()

def main():
    let port = unwrap_or(parse_int(unwrap_or(env("PORT"), "8080")), 8080)
    print(f"listening on {port}")
    http.serve(port, handler)
```

### Serving on more than one core

`http.serve` takes one connection at a time, so a single slow handler stalls everyone behind it. `http.serve_concurrent(port, workers, handler)` runs a pool of worker threads instead. The model is share-nothing: each worker handles a whole request on its own heap, the `handler` must be a plain top-level function (no captures), and anything shared between requests lives in a file or a database — not in a captured variable.

```text
import std.http

# A plain function — no captured variables — so it is safe to run on every worker at once.
def handler(request: http.Request) -> http.Response:
    return http.text(200, "handled " + request.path)

def main():
    print("serving on 8080 with 8 workers")
    http.serve_concurrent(8080, 8, handler)   # blocks, like serve()
```

To keep a running counter or cache across requests under this model, read and write it through a file (or a database) inside the handler, so each worker coordinates its own access rather than sharing a value on the heap.

### Calling an API

```text
import std.http
import std.json

def main():
    match http.get("http://localhost:8080/echo"):
        case Ok(response):
            print(response.status)
            match json.parse(response.body):
                case Ok(doc):
                    print(doc.get("method"))
                case Err(problem):
                    print(problem)
        case Err(problem):
            eprint("request failed: " + problem)
```

(Plain `http://` only; for HTTPS put a reverse proxy in front of the server or call `run_capture("curl -s https://...")`.)

## 10. Parallelism

### Summing an expensive function over a range

```text
def work(i: i64) -> i64:
    var x = i
    for k in range(1000):
        x = (x * 1103515245 + 12345) % 2147483648
    return x % 1000

def main():
    print(parallel_sum(100000, 8, work))
```

### Mapping with a closure (captures must be numbers)

```text
def main():
    let scale = 3
    let results = parallel_map(1000000, 8, fn(i: i64) => i * scale % 7)
    print(sum(results))
```

To use a list inside a parallel mapper, index a module-level computation instead of capturing the list, or compute the elements from `i`.

## 11. Calling C libraries

Declare the C function with `extern def`, then build native. `from "..."` names a library to link; libc and libm need no `from`.

```text
extern def sqrt(x: f64) -> f64 from "m"
extern def getenv(name: str) -> str

def main():
    print(sqrt(2.0))
    print("HOME is " + getenv("HOME"))
```

```text
tacqor build --native app.tq -o app && ./app
```

Scalars (`i64`, `f64`, `bool`) and strings cross the boundary; pointers and structs do not. Use `= "symbol"` when the C name differs from the Tacqor name (`extern def c_abs(n: i64) -> i64 = "llabs"`). A foreign call only runs in a native build — under `tacqor run` it reports that a native build is required.

### Offloading heavy work: the handle pattern (and GPUs)

Because only scalars and strings cross the boundary, bulk data stays on the C side and Tacqor refers to it by an `i64` **handle**. The library allocates a buffer and returns a handle; later calls take that handle and return a scalar. This is the same interface a BLAS, a SIMD kernel, or a CUDA/OpenCL binding presents — `alloc` becomes `cudaMalloc`, a `set` becomes `cudaMemcpy`, and the kernel call launches on the device and copies one scalar back. The Tacqor side does not change when the backend becomes a GPU; only the C file does.

The runnable example is `examples/ffi_offload.tq` with its library `examples/ffi_offload.c`:

```text
extern def accel_alloc(n: i64) -> i64 from "accel"
extern def accel_fill(handle: i64, value: f64) from "accel"
extern def accel_dot(a: i64, b: i64, reps: i64) -> f64 from "accel"
extern def accel_free(handle: i64) from "accel"
```

Build the library, then point the native build at it with `TACQOR_CXXFLAGS` (which the native backend folds into the compile verbatim):

```sh
cc -O3 -fPIC -shared examples/ffi_offload.c -o /tmp/libaccel.so
TACQOR_CXXFLAGS="-L/tmp -Wl,-rpath,/tmp" tacqor build --native examples/ffi_offload.tq -o offload
./offload                       # offloaded dot = 9.8304e8
```

For purely on-CPU numeric work you usually do not need this — the built-in `dot`/`vadd`/`vmul`/`vscale` already vectorize over `list[f64]` (see `PERFORMANCE.md` §4b). The offload pattern earns its keep when the kernel lives in an existing C/CUDA library or must run on a device.

## 12. Testing

`tests.tq`:

```text
import std.testing
import main                       # the module under test (its main() is ignored)

def test_parse():
    testing.assert_eq(testing.assert_ok(main.parse_row("ada, 3")).amount, 3)

def test_rejects_garbage():
    assert is_err(main.parse_row("nope"))
```

Run `tacqor test tests.tq`; every `test_*` procedure runs, failures are reported with line and message, and the exit status is 1 if any failed.

## 13. Timing and profiling

```text
def main():
    let started = time_ms()
    var total = 0
    for i in range(10000000):
        total += i % 7
    print(f"{total} in {time_ms() - started} ms")
```

Build with `--native` and compare: the VM is good for development, native builds for anything hot. `tacqor dump file.tq` shows the bytecode of a hot loop; fewer instructions per iteration means faster.

## 14. Deployment

```sh
tacqor build app.tq -o app              # self-contained, VM embedded, no dependencies
tacqor build --native app.tq -o app     # machine code (needs c++ when building)
scp app server:/opt/app/ && ssh server /opt/app/app
```

Set `PORT`-style configuration through `env(...)`, log with `eprint`, return non-zero through `exit(n)`, and keep handlers failure-tolerant with `attempt` or by returning `Result`.
