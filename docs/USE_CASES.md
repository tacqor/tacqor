# What you can build with Tacqor

Tacqor is a compiled, statically typed language that ships as one small static binary with no runtime to install. That shape — fast startup, native speed, Python-like syntax, Rust-style errors, share-nothing concurrency, and a C FFI — makes it a strong fit for command-line tools, back-end services, and compute, and a poor fit for anything that needs a graphical or browser front end. This page lists the categories it is built for, the feature behind each, and, honestly, what it is not for yet.

For runnable code, see [`COOKBOOK.md`](COOKBOOK.md); for the full feature reference, [`../LANGUAGE.md`](../LANGUAGE.md) and [`../STDLIB.md`](../STDLIB.md).

## Built for

### 1. Command-line tools and utilities
Arguments (`args`, `arg`), standard input/output (`input`, `print`, `eprint`), exit codes (`exit`), and full file I/O. Converters, formatters, text processors, calculators, report generators — anything with a terminal interface. One static binary means it runs anywhere with no install step.

### 2. HTTP APIs and web services (back ends)
`std.http` is an HTTP/1.1 server and client with routing, `std.json` for payloads, and `http.serve_concurrent(port, workers, handler)` for a worker pool that handles many requests at once. REST APIs, microservices, webhook receivers, internal dashboards' back ends. (Plain `http://`; put a reverse proxy in front for TLS.)

### 3. Network servers and clients
Blocking TCP primitives (`tcp_listen`, `tcp_accept`, `tcp_connect`, `tcp_read`, `tcp_write`, `tcp_close`). Chat servers, custom wire protocols, proxies, port probes, health checkers — anything that speaks a socket.

### 4. Concurrent and parallel compute
`parallel_sum` and `parallel_map` run a function over an index range on up to 64 threads with deterministic results; `parallel_run` is a share-nothing worker pool; **channels** (`std.channels`) pass messages between workers. CPU-bound batch jobs, simulations, fan-out/fan-in pipelines.

### 5. Data processing and pipelines (ETL)
Read files and lines, parse (`std.json`, string tools), transform — with `parallel_map` when the work is big — and write results back out. Log processing, data cleaning, format conversion, aggregation and rollups.

### 6. Developer tooling
The toolchain is itself the proof of concept. Linters, formatters, code generators, test runners (`tacqor test`), build and release scripts, and editor integrations — the compiler already exposes machine-readable diagnostics (`tacqor diagnose`) and a language server (`tacqor lsp`).

### 7. Parsers, interpreters, compilers and DSLs
Traits, generics, closures, pattern matching (`match`) and `Result`/`?` make language work comfortable. Config-language processors, expression evaluators, template engines, small interpreters, transpilers.

### 8. System automation and scripting
`run` and `run_capture` execute shell commands; the file builtins and `std.os` handle paths and the filesystem; `std.time` handles dates. Glue scripts, deploy helpers, backup and housekeeping jobs, cron tasks (schedule the binary from the OS).

### 9. Scientific and numerical computing
`std.math` (trigonometry, `exp`, `log`, `sqrt`, `pow`), 64-bit floats and checked 64-bit integers, exact parallel reductions, and — via the FFI — C numeric libraries such as BLAS or GSL for the heavy lifting.

### 10. C-ecosystem bridges (FFI)
`extern def` links and calls any C library from a native build: SQLite, zlib, libcurl, crypto and compression libraries, and more. This is how a young language reuses the entire C ecosystem instead of reimplementing it. (Scalars and strings cross the boundary today; see [`../LANGUAGE.md`](../LANGUAGE.md) §13.)

### 11. Bots, webhooks and API integrations
The HTTP client (`get`, `post`, `request`), `std.json`, and TCP are enough to call third-party APIs and receive callbacks. Chat bots (over a service's HTTP API), polling agents, notification senders, integration glue between systems.

### 12. Text-based games and simulations
State, logic, loops and randomness (`std.random`) with no graphics dependency. Roguelikes, cellular automata, board-game engines, puzzle generators and solvers, teaching simulations.

### 13. Self-contained distributable binaries
Every program compiles to a single executable of a few megabytes with no runtime to install (`tacqor build` embeds the VM; `tacqor build --native` produces machine code). Ideal for internal tools you hand to teammates, utilities that must "just run," and anything distributed to machines you do not control.

## Not a fit (yet)

- **Graphical desktop apps** and **mobile apps** — there are no windowing or graphics bindings (reachable only through the C FFI to a GUI library, with real effort).
- **Web front ends** — Tacqor compiles to native code and bytecode, not WebAssembly or JavaScript, so it builds the *back end*, not code that runs in a browser.
- **Native HTTPS/TLS** — the HTTP stack is `http://` only; terminate TLS at a reverse proxy.
- **GPU compute**, **asynchronous / non-blocking I/O** (concurrency is blocking, share-nothing threads), and **graphics, audio or video** processing.

Some of these are deliberate and some are planned — see [`../ROADMAP.md`](../ROADMAP.md).
