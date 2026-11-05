# Tacqor FAQ

### What is Tacqor, in one sentence?

A statically typed, compiled language with Python's indentation syntax and Rust's error discipline (`Option`, `Result`, `?`, checked arithmetic), shipped as a single executable that runs programs in a bytecode VM or compiles them to native code.

### Who is it for?

People who write scripts, tools, services and data-processing programs in Python and want them to be type-checked and fast without learning an ownership system, and people who like Rust's error handling but want a smaller language.

### Is it interpreted or compiled?

Both. `tacqor file.tq` compiles to bytecode in memory and runs it in the VM; `tacqor build --native` compiles the same bytecode to C++ and then to machine code. Both produce identical output for every program; the test suite checks this for every feature.

### How fast is it?

Native builds run within 1.4–3× of C++ and Rust on compute-bound code and 1.6–90× faster than CPython; the VM is faster than CPython on every benchmark. See `PERFORMANCE.md`.

### Do I need a C++ compiler?

Only for `build --native`. Running, testing, `build` (bytecode + embedded VM) and everything else need just the `tacqor` binary.

### Is there a garbage collector?

Reference counting frees most objects immediately; a cycle collector runs automatically to reclaim reference cycles. There are no pauses to tune; `gc_collect()` forces a collection if you want one.

### Why are there no classes or inheritance?

Structs hold data and methods; traits describe behaviour that several structs share; generics write code once for many types. Together these cover what inheritance is used for, without the fragile base class problem or the question of which overridden method runs. This is the same position as Rust and Go.

### Why is there no `try`/`except`?

Because then every call could fail invisibly. A function that can fail says so in its type by returning `Result[T, E]`; the caller handles it with `match`, propagates it with `?`, or converts it with `unwrap_or`. `attempt(f)` exists for the boundary case — keep a server running when one request fails — not as a general control-flow mechanism.

### What happens on integer overflow?

A runtime error with a traceback, in both the VM and native code. There is no silent wraparound. If you want wrapping, use the bit operators on masked values or `f64`.

### Why `i64` and `f64` only? Where are `i32`, `u8`, …?

One integer type and one float type remove a whole class of conversion bugs and keep the language small. Lists of `i64`, `f64` and `bool` are stored unboxed, so there is no memory penalty for the common cases. Narrower types may come later if a use case demands them (see `ROADMAP.md`).

### Are strings Unicode?

Strings are immutable UTF-8 byte strings. `len` counts bytes and indexing yields bytes; `chars`, `char_count`, `chr` and `ord` work in code points. This matches what files and sockets deliver and avoids the index-is-not-a-character surprise of UTF-16 languages.

### Can I write concurrent programs?

Two kinds, both share-nothing. Data-parallel: `parallel_sum` and `parallel_map` run a function over an index range on up to 64 threads and return deterministic results. Request-parallel: `parallel_run` runs a task on a pool of worker threads (and `http.serve_concurrent` serves HTTP on such a pool), each worker with its own VM, so one slow handler no longer blocks the rest. There are still no shared-memory threads, locks or async: a worker owns everything it allocates and nothing on the heap is touched by two threads at once, which keeps the memory model simple (refcounts are not atomic). State shared between workers lives in a file or a database. `http.serve` still handles one connection at a time when that is all you need. See `ROADMAP.md` for what is planned.

### How do I handle `None`?

```text
match lookup(ages, "ann"):
    case Some(age):
        print(age)
    case None:
        print("unknown")
```

Or `unwrap_or(lookup(ages, "ann"), 0)`, or `lookup(ages, "ann")?` inside a function that itself returns an `Option`.

### How do modules work?

`import std.lists` loads `std/lists.tq` and exposes its definitions as `lists.map`, `lists.filter`, and so on; `import utils.geometry` loads `utils/geometry.tq` relative to the importing file. There is no `from … import`. See `TOOLING.md` §3 for the search path and `LANGUAGE.md` §1.

### How do I install a package?

`tacqor add https://github.com/someone/pkg` (or a local folder) puts it in `packages/pkg` and records it in `tacqor.pkg`; `import pkg` loads `packages/pkg/lib.tq`. `tacqor install` restores all packages from the manifest on another machine. There is no central registry.

### How do I distribute a program?

`tacqor build app.tq -o app` makes a single executable that runs anywhere the `tacqor` binary runs (no standard library files needed). `tacqor build --native app.tq -o app` makes a machine-code executable that depends only on the C++ standard library of the machine that built it.

### Does it run on Windows and macOS?

The toolchain is portable C++17: `build-windows.cmd` builds it with MSVC and `make` builds it with GCC or Clang on macOS and Linux. The distribution includes a static Linux x64 binary; other platforms build from source. Networking uses Winsock on Windows and BSD sockets elsewhere.

### Is it production ready?

The toolchain has a differential test suite across all four execution modes, runs clean under AddressSanitizer and UndefinedBehaviorSanitizer, and survived 6,100 fuzz cases with no crashes. The language has no planned breaking changes. What it does not have yet: a large ecosystem, an LSP server, a debugger, and shared-memory concurrency (the concurrency it does have is share-nothing by design — `parallel_run` and `http.serve_concurrent`). See `ROADMAP.md`.

### Can I call C code?

Not directly. `run(command)` and `run_capture(command)` run external programs; TCP sockets talk to services. A foreign-function interface is on the roadmap. In the meantime, `tacqor emit` gives you the C++ for a program, which can be linked with other C++ by hand.

### Why does the REPL repeat side effects?

Because Tacqor compiles whole programs: the REPL recompiles and re-runs the session after each entry and shows only new output. A `print` in a definition you entered earlier runs again but its output is hidden; a file write runs again for real. Use the REPL for exploring values and types, not for scripts with side effects.

### Why does `print(items[-1])` fail?

Negative indexes are not wrapped around (unlike Python) — `items[-1]` is out of bounds. Use `items[len(items) - 1]`. Slices clamp like Python, so `items[-1:]` is simply empty. Being explicit avoids the off-by-one class of bugs where a negative index silently reads the wrong end.

### Why can't my lambda assign to a captured variable?

Closures capture by value at creation time, so assigning to a capture could never be seen outside the lambda. The compiler rejects it instead of letting the write disappear. Share a list, dict or struct and mutate that instead.

### Why must `None`, `Ok` and `Err` sometimes be annotated?

`None` alone does not say what the `Some` would hold; `Ok(3)` does not say the error type. When the context supplies the type (a return type, a parameter, an annotated `let`) no annotation is needed; otherwise write `let x: Option[i64] = None`.

### Where do I report a bug or ask for a feature?

See `CONTRIBUTING.md` for the process and `SECURITY.md` for anything security-related.

### Where should I read next?

`TOUR.md` to learn the language in twenty minutes, `COOKBOOK.md` for working programs, `LANGUAGE.md` and `STDLIB.md` as references, `SPECIFICATION.md` for the exact rules, and `WHITEPAPER.md` for why things are the way they are. `INDEX.md` lists every document.
