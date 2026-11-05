# Roadmap

What is planned, in rough priority order, and what is deliberately not planned. Nothing here changes the meaning of a program that compiles today. Items move to `CHANGELOG.md` when they ship.

## 1.3 — developer experience

- **Language server (LSP).** Shipped in 1.3: `tacqor lsp` speaks LSP over stdio and publishes live diagnostics (errors and warnings) to any editor with an LSP client, not just VS Code. Still to add, reusing the compiler's symbol tables: go-to-definition, hover types, completion and rename.
- **Formatter.** `tacqor fmt` implementing `docs/STYLE.md`, round-tripping comments.
- **Debugger.** Breakpoints, stepping and variable inspection in the VM, driven over a simple text protocol so editors can integrate it; a source-line map already exists in the bytecode.
- **Test runner improvements.** Filtering by name, timing per test, `--fail-fast`, discovering `test_*.tq` files in a folder.
- **Multi-line REPL editing and history.**

## 1.4 — types

- **User-defined enums** with payloads (`enum Shape: Circle(f64) | Rect(f64, f64)`) and `match` over them; `Option` and `Result` become ordinary enums. This is the most requested gap by Rust programmers.
- **Tuples** `(i64, str)` with destructuring in `let` and `match`.
- **Trait default methods** and traits over built-in types (`impl Shape for i64`-style), so generic code can be bounded on things like ordering.
- **Pattern matching on struct fields** and nested patterns.
- **Narrower integer and byte types** only if enums and tuples do not cover the binary-protocol use cases; the preference is to keep `i64` and `f64` as the only numeric types.

## 1.5 — runtime and performance

- **Native backend:** hoist list data pointers and lengths out of loops; specialize `dict[str, V]`; inline small functions across module boundaries; `-O2` option for faster builds.
- **VM:** inline caching for closure and trait calls; a register-based redesign is being evaluated against the native backend's gains before committing to it.
- **Collector:** generational tracking so long-lived objects are not rescanned each cycle.
- **Strings:** small-string optimization and shared substrings for slicing.

## 1.6 — ecosystem

The **foreign function interface** landed in 1.3: `extern def` declarations call C from native builds, marshalling scalar (`i64`, `f64`, `bool`) and string arguments and results, with `from "lib"` naming a library to link. Still to come: a VM path through `dlopen` for the same declarations (today a foreign call under the bytecode VM reports that a native build is required), and pointer/struct arguments.

- **Package index.** A simple registry file format (`name → git URL + commit`) that `tacqor add` can resolve, with lockfile semantics (`tacqor.lock`) and checksum verification.
- **Standard library growth:** `std.csv`, `std.time` (calendar formatting), `std.path`, `std.regex` (a small backtracking-free engine), `std.args` (command-line parsing), `std.random` distributions, `std.http` TLS via the platform's library.
- **Windows and macOS binaries** in the release, built and tested in CI; ARM64 Linux.

## 2.0 — concurrency

Request-level concurrency landed in 1.3: `parallel_run` runs a share-nothing worker pool, and `http.serve_concurrent` serves HTTP on one. What is left for 2.0 is the richer model.

- **Structured concurrency**: tasks with scoped lifetimes and channels for message passing, with the type system ensuring values sent between tasks are either immutable or transferred. This keeps the non-atomic refcount design by never sharing a mutable object across threads — the same rule `parallel_run` and `parallel_map` enforce today, generalized so workers can hand values to one another without going through a file.
- **A sharded allocator** so the per-thread object lock stops being the one point of serialization under heavy allocation.
- **Non-blocking I/O** so a worker can hold many connections at once rather than one at a time.

## Not planned

- **Inheritance.** Traits, generics and composition cover the use cases; see `docs/FAQ.md`.
- **Exceptions.** `Result`, `?` and `attempt` are the error model.
- **Implicit conversions, operator overloading, macros.** Each adds non-local reasoning to every line of code that uses it.
- **Null.** `Option` is the only representation of absence.
- **Unsafe blocks or raw pointers.** The FFI is the only unchecked boundary, and it is explicit at the `extern def` declaration.
- **A garbage collector that replaces reference counting.** Deterministic destruction is a feature.

## How priorities are set

Issues tagged `proposal` are discussed in the open; the maintainers pick what ships in the next minor release by weighing user demand against the complexity added to the language and the implementation (`docs/WHITEPAPER.md` §2). Contributions are welcome on anything above — see `CONTRIBUTING.md`.
