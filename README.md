<p align="center"><a href="https://tacqor.github.io/tacqor/"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/assets/logo.png"><img src="docs/assets/logo-dark.png" alt="Tacqor" width="150"></picture></a></p>

<p align="center">
<a href="https://github.com/tacqor/tacqor/actions/workflows/ci.yml"><img src="https://github.com/tacqor/tacqor/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
<a href="https://github.com/tacqor/tacqor/releases/latest"><img src="https://img.shields.io/github/v/release/tacqor/tacqor?label=release" alt="Latest release"></a>
<a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-0b8a7a.svg" alt="MIT license"></a>
<a href="https://tacqor.github.io/tacqor/"><img src="https://img.shields.io/badge/docs-tacqor.github.io-0f2a3f.svg" alt="Documentation"></a>
</p>

# Tacqor

Tacqor is a statically typed programming language with Python's syntax, Rust's discipline, and two ways to run: a fast bytecode VM, or native machine code through the system C++ compiler. One executable checks, tests, compiles, runs, and packages programs, and doubles as a REPL; a standard library ships as Tacqor source; a VS Code extension scans code as you type.

```text
import std.lists

struct Account:
    owner: str
    balance: i64

    def withdraw(self, amount: i64) -> Result[i64, str]:
        if amount > self.balance:
            return Err(f"{self.owner} cannot withdraw {amount}")
        self.balance -= amount
        return Ok(self.balance)

def total[T](items: list[T], value: fn(T) -> i64) -> i64:
    return lists.reduce(items, 0, fn(acc: i64, item: T) => acc + value(item))

def main():
    let accounts = [Account("Ada", 100), Account("Bob", 20)]
    match accounts[1].withdraw(50):
        case Ok(left):
            print(left)
        case Err(problem):
            print(problem)
    print(total(accounts, fn(a: Account) => a.balance))
```

## Why Tacqor exists

Software keeps repeating the same expensive move: you prototype in **Python** because it is fast to write, then rewrite in **Rust**, **Go**, or **C++** for production because Python is too slow to ship. The rewrite means learning a very different language and maintaining two codebases for a single idea.

Tacqor exists to erase that rewrite — to be one language you can both prototype *and* ship in. It gives you Python's syntax and Rust's discipline in a language that compiles to native machine code, so the thing you prototype is already the thing you ship. Static types catch mistakes before the program runs; `Option`, `Result` and `?` make failure explicit without exceptions; there is no garbage collector and no borrow checker to learn; and the whole toolchain is one small static binary with no runtime to install. The goal is not to beat a systems language on raw speed — it is to get most of the way there for a fraction of the difficulty, in a language you can read on day one.

It is a strong fit for command-line tools, back-end and network services, data pipelines, developer tooling, and anything that must ship as a single fast binary — see [`docs/USE_CASES.md`](docs/USE_CASES.md). It is deliberately not a language for GUIs, mobile, browser front ends, or numerical/AI workloads today.

## What is in the language

| Area | Features |
|---|---|
| Types | `i64` (overflow-checked), `f64`, `bool`, `str`, `list[T]` (flat native storage for numbers and bools), `dict[K, V]`, structs with methods, `Option[T]`, `Result[T, E]`, function types |
| Generics | Generic functions and structs, inferred or explicit type arguments, trait bounds (`T: Shape`), monomorphized like Rust |
| Traits | Interfaces with compile-time conformance checking and runtime dispatch (`list[Shape]`), across modules, on generic structs |
| Functions | First-class functions, closures capturing by value, lambdas with inferred result types, methods, method-call syntax for built-ins (`items.push(x)`) |
| Errors | `Option`/`Result`, `match` with exhaustiveness checking, the `?` operator, `assert`, runtime checks with tracebacks, `attempt` to recover at boundaries |
| Modules | `import a.b`, aliases, module constants, a `std` library on the import path, `TACQOR_PATH` |
| Packages | `tacqor add FOLDER-or-GIT-URL`, `install`, `remove`; `tacqor.pkg` manifest; `packages/` on the import path |
| Parallelism | `parallel_sum` / `parallel_map` over 1–64 threads with named functions or closures, deterministic results |
| Memory | Reference counting plus an automatic cycle collector, in the VM and in native builds |
| Syntax | `if/elif/else`, `while`, `for ... in range()/list`, `break`/`continue`, `+=`, bit operators, f-strings, `const`, slicing, `in` |
| Networking | Blocking TCP built-ins and an HTTP/1.1 server + client (`std.http`) |
| Library | ~110 built-ins (text, lists, dicts, math, files, environment, random, time, process) plus `std.lists`, `std.strings`, `std.math`, `std.collections`, `std.json`, `std.testing` |
| Tooling | `run` (or just `tacqor file.tq`, shebang scripts), `test`, `repl`, `build`, `build --native`, `emit`, `compile`, `check`, `diagnose` (JSON for editors), `dump`, `new`, `add`/`install`/`remove`, `install.sh`; VS Code live scanner; errors show the source line with a caret |
| Static analysis | Type checking, definite returns, unreachable code, non-exhaustive match, plus warnings for constant overflow / division by zero / out-of-range indexes / empty loops / always-failing asserts |

Read `TOUR.md` for a guided introduction, `LANGUAGE.md` for the reference, and `STDLIB.md` for the library.

## Documentation

Everything is online at **[tacqor.github.io/tacqor](https://tacqor.github.io/tacqor/)** — the [whitepaper](https://tacqor.github.io/tacqor/html/WHITEPAPER.html), the tour, the references and the guides as web pages. In the repository, `docs/INDEX.md` maps the whole set. The short version:

| | |
|---|---|
| Learn | `TOUR.md` · `docs/USE_CASES.md` · `docs/COOKBOOK.md` · `docs/FROM_PYTHON.md` · `docs/FROM_RUST.md` · `docs/FAQ.md` |
| Reference | `LANGUAGE.md` · `STDLIB.md` · `docs/SPECIFICATION.md` · `docs/ERRORS.md` · `docs/TOOLING.md` · `docs/STYLE.md` · `docs/PERFORMANCE.md` |
| Design | `docs/WHITEPAPER.md` (with 17 figures in `docs/diagrams/`) · `docs/ARCHITECTURE.md` · `docs/BYTECODE.md` |
| Project | `CHANGELOG.md` · `ROADMAP.md` · `CONTRIBUTING.md` · `SECURITY.md` · `LICENSE` |

## Speed

Two backends, one semantics. The VM uses threaded dispatch, a contiguous value stack, fused instructions and typed list storage. `build --native` translates the typed bytecode into typed C++ and compiles it with GCC/Clang at `-O3`: integers and floats live in registers, overflow checks become CPU flag tests, generic code is monomorphized. Measured on the same Linux x64 machine (best of three):

| benchmark | C++ -O2 | Rust -O | **Tacqor native** | Tacqor VM | Python 3.13 |
|---|---|---|---|---|---|
| 12M checked-arithmetic calls | 0.020 s | 0.021 s | **0.028 s** | 0.52 s | 2.53 s |
| fib(30), 1.3M calls | 0.005 | 0.008 | **0.014** | 0.064 | 0.129 |
| sieve of 5M (`list[bool]`) | 0.041 | 0.031 | **0.096** | 0.43 | 1.03 |
| 2M dict updates with str keys | 0.105 | 0.156 | **0.30** | 0.44 | 0.47 |
| map/reduce 2M items with closures | — | — | **0.085** | 0.23 | 0.34 |
| 3M struct allocations (collector on) | — | — | **0.19** | 0.27 | 1.01 |

Native Tacqor sits within 1.4–3× of Rust and C++ on compute-bound code and 1.6–90× ahead of CPython; the VM alone beats CPython on every workload. Sources, method and analysis: `docs/PERFORMANCE.md`; reproduce with `sh benchmarks/run.sh`. The remaining gap to Rust is reference counting of strings and containers; Tacqor keeps those simple rather than adding an ownership system.

## Production readiness

- **Robustness.** The compiler, VM and bytecode loader were fuzzed with thousands of mutated programs and corrupted bytecode files under AddressSanitizer and UndefinedBehaviorSanitizer with zero findings; the complete test suite — including natively compiled programs — runs clean under both sanitizers. Malformed input always ends in a diagnostic, never a crash.
- **Failure behaviour.** Every runtime check aborts with `file:line:column: runtime error` plus a traceback and exit status 1, or becomes an `Err` inside `attempt`. Deep recursion is caught at 100,000 frames in both backends; dropping a million-element linked list cannot overflow the stack; cycles are collected.
- **Deployment.** `sh install.sh` installs the static binary and the standard library (`TACQOR_HOME` overrides the location). `build` produces a self-contained executable with no runtime dependencies; `build --native` produces machine code. Bytecode files are versioned and verified before running.
- **Compatibility.** Programs written for 1.0 and 1.1 run unchanged; the test suite keeps the feature programs of every release.
- **Known caveat.** The shipped `linux-x64/tacqor` is statically linked; glibc's resolver then needs compatible system libraries for DNS names other than `localhost` and IP literals. If `tcp_connect` to a hostname reports "could not resolve", build from source (`make`) for a dynamically linked binary — native builds (`build --native`) are always dynamically linked and unaffected.

## Run it

```sh
sh install.sh                      # or: chmod +x linux-x64/tacqor and use it in place
tacqor examples/server.tq 8080   # a web service; then open http://localhost:8080/
chmod +x linux-x64/tacqor
./linux-x64/tacqor run examples/bank.tq
./linux-x64/tacqor run examples/todo.tq -- add "write docs"
./linux-x64/tacqor test tests/unit_tests.tq
./linux-x64/tacqor build --native examples/pipeline.tq -o pipeline && ./pipeline
./linux-x64/tacqor new myproject
./linux-x64/tacqor repl
./linux-x64/tacqor add https://example.com/someone/somelib.git
```

Linux x64 one-liner once a release is published: `curl -fsSL https://tacqor.github.io/tacqor/get.sh | sh` (downloads the latest release and runs its `install.sh`).

On Windows, download `tacqor-vX.Y.Z-windows-x64.zip` from the [latest release](https://github.com/tacqor/tacqor/releases/latest), unzip it anywhere (it contains `tacqor.exe` and the `std` library side by side), and add that folder to your `PATH`; or use the Linux x64 build from an Ubuntu terminal in WSL. `build --native` needs a C++17 compiler on `PATH` (`c++`, or `cl` from a Visual Studio Developer prompt; or set `CXX`); without one, `build` still produces a self-contained executable with the embedded VM.

**VS Code:** install `Tacqor_Live_Scanner_1.2.1.vsix` (Extensions: Install from VSIX...), open any `.tq` file, and see type errors and warnings in Problems as you type.

## Layout

```text
linux-x64/tacqor   the toolchain (static binary; VM, compiler, native backend)
std/                 standard library modules (Tacqor source)
examples/            server (HTTP), shapes (traits), bank, todo, pipeline, inventory, word_count, primes, stats, ...
install.sh           installs the binary and std library under /usr/local or ~/.local
tests/               smoke.sh (command-line suite), features*.tq + expected output, unit_tests.tq
src/                 ~8,000 lines of C++17: compiler.cpp, runtime.cpp (VM), native.cpp, values.hpp (shared runtime + collector), artifact.cpp (bytecode), main.cpp (CLI, REPL, packages)
editor/              the VS Code extension source
docs/                whitepaper, architecture, specification, bytecode reference, cookbook, guides; docs/diagrams/ (SVG + PNG)
benchmarks/          the programs behind docs/PERFORMANCE.md, with run.sh
tools/               doc tooling: verify_docs.py (compiles every snippet), build_html.py (HTML edition of the docs)
```

## Project

| | |
|---|---|
| Website | https://tacqor.github.io/tacqor/ |
| Releases | https://github.com/tacqor/tacqor/releases — Linux x64 static binary, macOS and Windows builds, the standalone package, the VS Code extension, SHA-256 sums |
| Questions | [Discussions](https://github.com/tacqor/tacqor/discussions) · [`SUPPORT.md`](SUPPORT.md) |
| Bugs and proposals | [Issues](https://github.com/tacqor/tacqor/issues/new/choose) |
| Security | [`SECURITY.md`](SECURITY.md) — private reporting |
| Contributing | [`CONTRIBUTING.md`](CONTRIBUTING.md) · [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md) |
| Citing | [`CITATION.cff`](CITATION.cff) |
| CI | Linux (GCC, Clang, sanitizers), macOS, Windows; every documentation snippet compiled and run on each push |

## Build from source

`make` (Linux/macOS, any C++17 compiler; add `CXXFLAGS="-O3 -static"` for a portable binary), then `make test`. `build-windows.cmd` targets the MSVC Developer Command Prompt; MSVC uses switch dispatch instead of threaded dispatch and is otherwise identical. Continuous integration builds and tests every push on Linux (GCC and Clang, with and without sanitizers), macOS and Windows (MSVC via `build-windows.cmd`); the full suite — every feature program through the VM, portable bytecode, embedded-VM executables, and native builds — runs on each.

## History

0.4 was a preview with `i64`, `str`, `list[i64]` and `for`/`if`. 0.5 added floats, generic containers, structs, `while`, procedures, ~40 built-ins and a 6× faster VM. 0.6 added the native backend and typed list storage. 1.0 added modules, generics, closures, `Option`/`Result`/`match`/`?`, methods, f-strings, constants, the standard library, `test`, and `new`. 1.1 added traits with dynamic dispatch and generic bounds, a cycle collector, closures and `parallel_map` for parallelism, the package manager, and the REPL. 1.2 adds bit operators, `attempt`, TCP networking and `std.http`, process capture, hashing, shebang scripts, caret diagnostics, `install.sh`, iterative destruction, recursion guards, and the sanitizer/fuzzing hardening.
