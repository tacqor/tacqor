# Tacqor Tooling Reference

The `tacqor` executable is the whole toolchain. This document covers every command, flag, environment variable, exit code, file produced, and the editor protocol.

## 1. Installation

```sh
sh install.sh                    # /usr/local if writable, else ~/.local; prints the PATH line to add
sh install.sh --prefix DIR       # DIR/bin/tacqor + DIR/lib/tacqor/std
sh install.sh --uninstall [--prefix DIR]
```

Without installing, use `./linux-x64/tacqor` from the distribution folder; it finds `std/` next to itself. Build from source with `make` (any C++17 compiler; `CXXFLAGS="-O3 -static"` for a portable binary) and `make test`.

## 2. Commands

### `tacqor FILE.tq [ARG …]` · `tacqor run FILE.tq [-- ARG …]`

Compile the file and its imports in memory and run it in the VM. Arguments after the file (or after `--`) are the program's `args()`. `.tqb` bytecode files run the same way. Exit status: the program's (`exit(n)`), 1 on a runtime error, 1 on a compile error, 2 for usage errors.

A file whose first line is `#!/usr/bin/env tacqor` is executable once `tacqor` is on `PATH`.

### `tacqor test FILE.tq`

Compile without requiring `main`, run every `def test_*()` procedure in the root module in definition order, print `PASS name` / `FAIL name: line N: message`, a summary, and exit 1 if any failed. Tests may import the module under test; its `main` is ignored.

### `tacqor check FILE.tq`

Compile only. Prints warnings as `FILE:LINE:COLUMN: warning [CODE]: MESSAGE` to stderr and `FILE: OK` on success; errors print the source line with a caret. Exit 0 if it compiles (warnings included), 1 otherwise. Does not require `main`.

### `tacqor diagnose FILE.tq` · `tacqor diagnose --stdin [FILE.tq]`

Like `check` but prints one JSON array for editors. With `--stdin` the source is read from standard input; the optional path is used only to resolve relative imports (the file need not exist). Exit 0 with no errors, 1 with errors.

### `tacqor dump FILE.tq | FILE.tqb`

Print traits, structs and every function's bytecode (after optimization) with type names, operands and source lines. See `BYTECODE.md`.

### `tacqor compile FILE.tq -o FILE.tqb`

Write portable bytecode. The output name must end in `.tqb`. Any 1.2 runtime runs it with `tacqor run FILE.tqb`.

### `tacqor build FILE.tq -o EXE`

Write a self-contained executable: a copy of the `tacqor` binary with the bytecode appended. It runs on any machine where the binary runs; no standard library files are needed at run time (modules are compiled in). On Windows `.exe` is appended when no extension is given.

### `tacqor build --native FILE.tq -o EXE`

Generate C++ for the program and compile it with the system C++ compiler at `-O3`. The compiler is `$TACQOR_CXX`, else `$CXX`, else `c++` (`cl` on Windows); it must support C++17. The intermediate `.cpp` is written next to the output and removed afterwards. Native executables are dynamically linked against the C++ standard library of the machine that built them.

### `tacqor emit FILE.tq -o FILE.cpp`

Write the C++ that `build --native` would compile (self-contained: the runtime is embedded). Useful for inspection, for cross-compiling, or for integrating with another build system: `c++ -std=c++17 -O3 -pthread FILE.cpp -o app`.

### `tacqor new NAME`

Create `NAME/` with `main.tq` (a small program using the standard library), `tests.tq` (a test importing it), and `README.md`. Fails if the folder exists.

### `tacqor repl`

Interactive scratchpad. Enter definitions (`def`, `struct`, `trait`, `import`, `const`) or statements; a lone expression is printed; a line ending in `:` opens a block closed by an empty line. Commands: `:show`, `:reset`, `:quit`/`:q`, `:help`. The session is recompiled and re-run after each entry with only new output shown, so side effects repeat. Compile and runtime errors are printed without ending the session; `exit(...)` does end it.

### `tacqor add SOURCE [NAME]` · `tacqor install` · `tacqor remove NAME`

Package management in the current directory. `add` copies a folder or `git clone --depth 1`s a URL (anything containing `://`, starting with `git@`, or ending in `.git`) into `packages/NAME` (default name: the last path component without `.git`), strips its `.git`, and records `NAME SOURCE` in `tacqor.pkg`. `install` fetches every manifest entry whose folder is missing. `remove` deletes the folder and the entry. Import a package with `import NAME` (→ `packages/NAME/lib.tq`) or `import NAME.module` (→ `packages/NAME/module.tq`).

### `tacqor --version` · `tacqor --help`

## 3. Environment variables

| Variable | Effect |
|---|---|
| `TACQOR_HOME` | Directory containing `std/`; checked first when resolving the standard library. |
| `TACQOR_PATH` | Colon-separated extra module roots, searched after `std`. |
| `TACQOR_CXX`, `CXX` | C++ compiler command for `build --native` (`TACQOR_CXX` wins). |
| `HOME` | `~/.tacqor/std` is one of the standard library locations. |

Standard library search order: `$TACQOR_HOME/std`, `<binary dir>/std`, `<binary dir>/../std`, `<binary dir>/../../std`, `<binary dir>/../lib/tacqor/std`, `~/.tacqor/std`, `/usr/local/lib/tacqor/std`. The first existing directory wins.

Module search order for `import a.b`: importing file's directory → `<root program dir>/packages` → `<cwd>/packages` → the directory containing `std/` (so `import std.lists` resolves to `<that dir>/std/lists.tq`) → each `TACQOR_PATH` entry. At each of the first four roots the compiler tries `a/b.tq` then `a/b/lib.tq`; `TACQOR_PATH` entries are tried as `a/b.tq` only. The first regular file wins; a module is loaded once even when imported under several paths.

## 4. Exit codes

| Code | Meaning |
|---|---|
| 0 | Success (or all tests passed) |
| 1 | Compile error, runtime error, failed tests, or `exit(1)` |
| 2 | Usage error (bad command line), or `exit(2)` |
| n | `exit(n)` from the program (0–255) |

## 5. Diagnostics format

Text (stderr):

```text
app.tq:12:18: error: expected i64, found str
        let x: i64 = "oops"
                     ^~~~~~
app.tq:20:9: warning [empty-range]: this loop has no iterations
std/json.tq:7:1: error: ...            # errors in modules name the module file
app.tq:33:5: runtime error: list index out of bounds
  in inner (line 33)
  in outer (line 40)
  in main (line 48)
```

JSON (`diagnose`): an array of objects `{"line", "column", "length", "severity" (1 error, 2 warning), "code", "message"}`. Warnings carry their warning code (see `ERRORS.md`); a compile error has code `compile-error`, and unreadable or oversized input (over one million bytes) yields a single `invalid-input` error. Errors inside imported modules are reported at line 1 of the root file with `in MODULE:LINE:` prefixed to the message; warnings from imported modules are not reported. At most one error object is produced per run because the compiler stops at the first error.

```json
[{"line":12,"column":18,"length":6,"severity":1,"code":"compile-error","message":"expected i64, found str"}]
```

## 6. Editor integration

The VS Code extension (`Tacqor_Live_Scanner_1.2.1.vsix`, source in `editor/`) registers the `tacqor` language for `.tq` files and, after each edit (debounced 180 ms), runs `tacqor diagnose --stdin <document path>` with the document text on stdin, mapping the JSON to VS Code diagnostics. It bundles a Linux x64 compiler and the standard library; the setting **Tacqor: Compiler Path** points it at another build, and **Tacqor: Restart Live Scanner** restarts it. Any editor that can run a command on save or change can use the same protocol.

## 7. Files produced

| File | Produced by | Notes |
|---|---|---|
| `*.tqb` | `compile` | versioned bytecode, verified on load |
| executable | `build`, `build --native` | written to a temporary name and renamed into place; never overwrites the source or the toolchain |
| `*.cpp` | `emit` | self-contained C++17 |
| `packages/`, `tacqor.pkg` | `add`, `install`, `remove` | commit `tacqor.pkg`; `packages/` can be regenerated |
| `NAME/main.tq`, `tests.tq`, `README.md` | `new` | |

## 8. Build from source

```sh
make                                   # ./tacqor
make test                              # runs tests/smoke.sh
make CXXFLAGS="-O3 -static"            # portable Linux binary (static glibc: DNS caveat in README)
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -pthread src/*.cpp -o tacqor-asan   # sanitized build
```

`src/embedded_runtime.inc` is regenerated by `make` whenever `tacqor.hpp` or `values.hpp` changes and is committed so that `build-windows.cmd` (MSVC) needs no generator. `-DTACQOR_NO_THREADED` selects the portable `switch` dispatch loop.
