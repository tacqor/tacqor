# Tacqor Documentation

Everything in the distribution, by audience. Start with the first table if you are new. All of it is also online at [tacqor.github.io/tacqor](https://tacqor.github.io/tacqor/).

## Learn

| Document | What it is | Read it when |
|---|---|---|
| [`../README.md`](../README.md) | Overview, install, first program, project layout | First contact |
| [`../TOUR.md`](../TOUR.md) | The language in twenty minutes, by example | Learning the language |
| [`COOKBOOK.md`](COOKBOOK.md) | Complete, runnable programs for everyday tasks: files, text, data structures, errors, closures, structs and traits, JSON, HTTP, parallelism, tests, deployment | You know what you want to build |
| [`FROM_PYTHON.md`](FROM_PYTHON.md) | Migration guide: what carries over, what changes, idioms side by side, library map | Coming from Python |
| [`FROM_RUST.md`](FROM_RUST.md) | Migration guide: the shared error model, what the ownership-free design changes, idioms side by side | Coming from Rust |
| [`FAQ.md`](FAQ.md) | Short answers to the questions everyone asks | Something seems odd |

## Reference

| Document | What it is |
|---|---|
| [`../LANGUAGE.md`](../LANGUAGE.md) | Language reference: every construct, with examples |
| [`../STDLIB.md`](../STDLIB.md) | Every builtin and standard-library function, with signatures |
| [`SPECIFICATION.md`](SPECIFICATION.md) | The formal specification: lexical structure, grammar (EBNF), scopes, types, evaluation rules, statements, runtime errors, limits, conformance |
| [`ERRORS.md`](ERRORS.md) | Diagnostics catalog: every compiler error, warning code and runtime error, with the fix |
| [`TOOLING.md`](TOOLING.md) | The `tacqor` command: every subcommand, flag, environment variable, exit code, file produced, and the editor protocol |
| [`STYLE.md`](STYLE.md) | Style guide for Tacqor code |
| [`PERFORMANCE.md`](PERFORMANCE.md) | Performance report: methodology, results against C++/Rust/Python, where time goes, how to write fast code, how to reproduce (`../benchmarks/`) |

## Design and implementation

| Document | What it is |
|---|---|
| [`WHITEPAPER.md`](WHITEPAPER.md) | Why Tacqor is the way it is: motivation, design principles and the decisions that follow, the type/memory/execution/concurrency/error models, measurements, assurance, comparison with other languages, limits, future work. Figures in Appendix A. |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | The implementation blueprint: source layout, data model, compiler passes, runtime, VM, bytecode artifacts, verifier, native backend, CLI, error handling end to end, testing strategy, and recipes for extending every part |
| [`BYTECODE.md`](BYTECODE.md) | The bytecode: file format, program structure, verification rules, the complete instruction set, calling convention, a worked example, versioning |
| `diagrams/` | 17 diagrams (Mermaid source, SVG and PNG): toolchain, compiler pipeline, VM, value representation, memory management, native backend, module resolution, execution modes, type system, error handling, HTTP flow, monomorphization, closure calls, traits, parallelism, bytecode format, developer lifecycle |

## Project

| Document | What it is |
|---|---|
| [`../CHANGELOG.md`](../CHANGELOG.md) | What changed in every release, 0.4 to 1.2 |
| [`../ROADMAP.md`](../ROADMAP.md) | What is planned and what is deliberately not |
| [`../CONTRIBUTING.md`](../CONTRIBUTING.md) | How to build, test, change and submit; code style; review |
| [`../SECURITY.md`](../SECURITY.md) | Security goal, scope, reporting, measures in place, deployment advice |
| [`../LICENSE`](../LICENSE) | MIT |

## HTML edition

`docs/html/` holds every document above as a standalone HTML page with the diagrams embedded (open `docs/html/index.html`; `docs/html/WHITEPAPER.html` is the complete whitepaper with all 17 figures). Regenerate after editing the Markdown with `python3 -I tools/build_html.py . docs/html` (see `CONTRIBUTING.md` §2).

## Also in the distribution

- `examples/` — fourteen small programs (`tacqor run examples/NAME.tq`).
- `tests/` — the differential suite (`make test`); `features*.tq` double as feature-complete sample programs for each release.
- `std/` — the standard library, in Tacqor.
- `editor/` — the VS Code extension source; `Tacqor_Live_Scanner_1.2.1.vsix` is the packaged extension.
- `benchmarks/` — the programs behind `PERFORMANCE.md` and `run.sh`.
- `tools/` — `verify_docs.py` (compiles and runs every documentation snippet) and `build_html.py` (the HTML edition).
- `install.sh`, `Makefile`, `build-windows.cmd` — installation and building from source.
