# Changelog

All notable changes to Tacqor. Versions follow `MAJOR.MINOR.PATCH`: a minor release may add language features and standard-library functions; it does not remove or change the meaning of existing programs. The bytecode format is versioned separately (`docs/BYTECODE.md` §8) and may change in any minor release; recompile `.tqb` files when upgrading.

## Unreleased

**Project infrastructure**
- Website at https://tacqor.github.io/tacqor/ (landing page, HTML edition of every document, diagrams), deployed by `.github/workflows/pages.yml`.
- Continuous integration (`.github/workflows/ci.yml`): GCC build with the full suite and documentation-snippet verification, Clang `-Werror` build of the switch-dispatch VM, AddressSanitizer/UBSan run, macOS build, MSVC build (non-blocking), embedded-runtime freshness check.
- Release automation (`.github/workflows/release.yml`): static Linux x64 binary, macOS and Windows builds, standalone package, VS Code extension and SHA-256 sums attached to each `v*` tag.
- Issue forms, pull request template, `CODEOWNERS`, Dependabot for Actions, `CODE_OF_CONDUCT.md`, `SUPPORT.md`, `CITATION.cff`, `.editorconfig`, `.gitattributes` (`.tq` files highlighted as Python on GitHub).
- `SECURITY.md` now points to GitHub private vulnerability reporting.
- Remote installer `docs/get.sh` (`curl -fsSL https://tacqor.github.io/tacqor/get.sh | sh`).
- Logo, social preview and README badges.

**Fixes**
- Windows: the Winsock library is now linked (`#pragma comment(lib, "ws2_32.lib")` for MSVC, `-lws2_32` for MinGW native builds, and in `build-windows.cmd`), which the first CI run on a real Windows machine caught as unresolved `__imp_accept`/`__imp_bind`/… symbols.
- The switch-dispatch VM and the native generator handle the `Op::count` sentinel, so `-Werror -Wswitch` builds are clean; `static_assert`s tie the dispatch table and the disassembler's name table to the opcode count.

## 1.2.0 — 2026-11-04

Production-readiness release.

**Language**
- Bit operators on `i64`: `&`, `|`, `^`, `~`, `<<`, `>>` and their compound assignments; shift counts are checked (0–63) with a compile-time warning for constants (`invalid-shift`).
- `attempt(f)`: runs a zero-argument function value and converts a runtime error into `Err(message)`; the only catching construct.
- Nested quotes inside f-string braces (`f"{d["key"]}"`).
- Local bindings may shadow module names.
- `attempt` is no longer a reserved word (it is a builtin).

**Runtime and library**
- TCP networking: `tcp_listen`, `tcp_accept`, `tcp_connect`, `tcp_read`, `tcp_write`, `tcp_close` (Winsock on Windows, BSD sockets elsewhere).
- `std.http`: HTTP/1.1 server (`serve`, `serve_count`) and client (`get`, `post`), `Request`/`Response` structs, response helpers.
- `run_capture(command)`, `unix_time()`, `hash(text)` (FNV-1a), `bytes`/`from_bytes`, `to_hex`/`from_hex`, `fixed(f, digits)`.
- 104 builtins in total.

**Hardening**
- Iterative destruction of deeply nested values (no C++ stack overflow on long linked structures).
- Call-depth guard in the VM and native code (100,000 frames; native executables run on a 512 MB stack thread).
- Limits on expression nesting, block nesting, value nesting (2,000 levels for printing and comparison), source size (1 MB) and bytecode size.
- Immortal constant pool is released on exit so sanitizer builds report no leaks.
- The complete suite, including native builds, runs clean under AddressSanitizer and UndefinedBehaviorSanitizer; 5,500 mutated programs and 600 corrupted bytecode files were fuzzed with zero findings.

**Tooling**
- `tacqor file.tq [args]` runs a file directly; `#!/usr/bin/env tacqor` scripts.
- Diagnostics show the source line with a caret and length underline.
- `install.sh` (`--prefix`, `--uninstall`), `TACQOR_HOME`, standard library lookup in `../lib/tacqor/std`, `~/.tacqor/std`, `/usr/local/lib/tacqor/std`.
- `tacqor diagnose --stdin [FILE]` for editors; VS Code extension 1.2.0.
- Documentation set: whitepaper, architecture, specification, bytecode reference, cookbook, style guide, diagnostics catalog, tooling reference, performance report, FAQ, migration guides (`docs/`), 17 rendered diagrams.

**Fixes**
- Smoke-test expectations for argument echo and warning codes.
- Block-bodied lambdas are documented as statement-position only (bind the lambda with `let` before passing it); the parser reports `expected a newline` inside call parentheses.

## 1.1.0

- **Traits**: `trait Name:` with method signatures; `struct S(Trait)`; trait-typed values with dynamic dispatch through vtables (`call_trait`); generic bounds `[T: Trait]` with static dispatch per instantiation; implicit struct→trait conversion.
- **Cycle collector**: CPython-style trial deletion over tracked containers; automatic threshold max(200,000, 2× live); `gc_collect()`, `gc_count()`; paused during parallel sections.
- **Closures in parallel code**: `parallel_map(count, workers, f)` returning every result in order; mapper closures may capture `i64`/`f64`/`bool`; 128-bit accumulation in `parallel_sum`.
- **Package manager**: `tacqor add SOURCE [NAME]`, `install`, `remove`, `tacqor.pkg` manifest, `packages/` resolution.
- **REPL**: `tacqor repl` with `:show`, `:reset`, `:quit`, block entry, recompile-and-replay semantics.
- Generic structs implementing traits; qualified trait names across modules.

## 1.0.0

- **Modules**: `import a.b [as c]`, file and `lib.tq` folder modules, standard library search path, `TACQOR_PATH`; errors name the module file.
- **Generics**: type parameters on functions and structs, inference by unification, explicit instantiation `f[i64](...)`, monomorphization with an instantiation cache.
- **Closures**: `fn(x: T) => expr` and block lambdas, capture by value, named functions as values, `fn(A) -> R` types, `call_value` convention.
- **`Option[T]` and `Result[T, E]`**: `Some`/`None`/`Ok`/`Err`, `match … case`, exhaustiveness checking, `?`, `unwrap`, `unwrap_or`, `expect`, `is_some`/`is_none`/`is_ok`/`is_err`.
- **Methods** inside structs with `self`; method-call syntax for builtins and module functions (`items.push(x)`, `name.upper()`).
- **f-strings** and `const` declarations.
- **Standard library** in Tacqor source: `std.lists`, `std.strings`, `std.math`, `std.testing`, `std.collections`, `std.json`.
- **Tooling**: `tacqor test` (`def test_*()`), `tacqor new`, warnings with codes, `diagnose` JSON.
- Bytecode magic bumped; verifier extended for the new ops (`push_function`, `make_closure`, `call_value`, `make_variant`, `variant_which`, `variant_payload`).

## 0.6.0

- **Native backend**: `tacqor build --native` and `tacqor emit`; typed abstract interpretation of the bytecode, typed C++ registers, embedded runtime, compiled by the system C++ compiler at `-O3`.
- **Typed list storage**: `list[i64]`, `list[f64]`, `list[bool]` stored unboxed.
- Superinstructions `loop_init`/`loop_next`, `iter_next`; `jump_unless_locals`.

## 0.5.0

- `f64`, generic `list[T]`, `dict[K, V]` (insertion-ordered, `i64`/`str` keys), `struct` declarations, `while`, `break`, `continue`, `elif`, procedures, string slicing, ~40 builtins.
- VM rewrite: contiguous value stack, frames as offsets, computed-goto dispatch, `load2`/`load_integer`/`add_store` fusions — about 6× faster than 0.4.
- Portable bytecode (`compile`) with verification on load; embedded-VM executables (`build`).

## 0.4.0

- Preview: `i64`, `str`, `list[i64]`, `let`/`var`, `def`, `if`/`else`, `for … in range`, `print`, checked arithmetic, a tree-walking compiler to a simple stack VM.
