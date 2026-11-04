# Tacqor Style Guide

Conventions used by the standard library and examples. Following them makes code read the same everywhere; none of them is enforced by the compiler except where noted.

## Layout

- Four spaces per indentation level (enforced). No tabs (enforced).
- One blank line between functions; two before a top-level `struct` or `trait` when the file is long.
- Lines up to 110 characters. Break long calls after a comma; continuation lines inside brackets are free-form.
- A file is: optional shebang, a comment describing the module, imports, constants, structs and traits, functions, `main` last.

## Naming

| Thing | Convention | Example |
|---|---|---|
| Functions, methods, locals, fields, modules | `snake_case` | `parse_row`, `total_area`, `std.lists` |
| Structs, traits, type parameters | `CamelCase`, single capitals for type parameters | `Account`, `Shape`, `T`, `K`, `V` |
| Constants | `UPPER_SNAKE` | `MAX_REQUEST` |
| Tests | `test_` prefix, describe the behaviour | `test_rejects_empty_name` |
| Predicates | `is_`/`has_` prefix, return `bool` | `is_prime`, `has_header` |
| Conversions | `to_x` / `from_x` / `parse_x` | `to_hex`, `from_bytes`, `parse_int` |

Avoid single-letter names except for loop indices, lambda parameters in one-line lambdas, and type parameters.

## Bindings

- Default to `let`; use `var` only for values that are reassigned. The compiler tells you when a `var` is needed.
- Annotate the type when the initializer does not fix it (`var items: list[i64] = []`) and when it helps the reader at a module boundary; otherwise let inference work.
- Declare variables close to first use, inside the narrowest block.

## Functions

- Every function that can fail for an expected reason returns `Result[T, str]` (or a struct error type when callers need to inspect it). Reserve runtime errors (`fail`, `assert`, `unwrap`) for bugs.
- Use `?` to propagate; use `match` at the point where you decide what to do.
- Keep `main` short: parse arguments, call into functions, report.
- Prefer returning a value over mutating an argument, except for performance-sensitive code and methods that obviously mutate `self` (`push`, `deposit`).
- Procedures (no result) should have verb names: `save`, `print_report`.

## Types

- Use `Option[T]` for "may be absent", never a sentinel like `-1` or `""` — except where a built-in already uses one (`find` returns `-1`); wrap such built-ins when passing results around.
- Use structs for records of more than two fields; `std.lists.Pair` for exactly two.
- Use traits only when more than one struct needs the same interface; a single struct with methods is simpler.
- Generic code should be *actually* generic: if it only ever runs on one type, write it for that type.

## Control flow

- `for x in items` over an index loop unless the index is needed; `for i in range(len(items))` when it is.
- `while True:` with `break`/`return` for loops whose exit condition sits in the middle.
- `match` for `Option`/`Result` and for multi-way string/integer dispatch; `if`/`elif` for ranges and compound conditions.
- One `return` per arm is fine; early returns for guard clauses are encouraged.

## Errors and messages

- Error strings are lowercase, no trailing period, name the offending value: `f"port out of range: {port}"`.
- Validate at the boundary (arguments, files, requests) and trust the types inside.
- Use `eprint` for diagnostics, `print` for output a user or another program consumes.
- Exit codes: 0 success, 1 failure, 2 usage error.

## Modules

- One module per concept; names are nouns (`geometry`, `http`). Keep `main` in the entry file.
- Import with the default alias unless it clashes; `as` is for clashes, not for shortening.
- Public API first in the file, helpers below. There is no private keyword; prefix internal helpers with `_` by convention? **No** — Tacqor does not treat `_` specially; put helpers at the bottom and document the intended API in the module comment.

## Comments and documentation

- A module starts with a comment stating what it provides and the one-line usage.
- Comment *why*, not *what*; the types already say what.
- Keep examples in comments runnable (the standard library does).

## Performance habits

- Lists of `i64`, `f64` or `bool` use flat native storage; prefer them to lists of structs for numeric work.
- Build strings with `join` over a list rather than repeated `+=` in a long loop.
- Use `parallel_sum`/`parallel_map` for CPU-bound loops over an index; keep the mapper pure.
- Build with `--native` before concluding something is slow.

## Testing

- One `tests.tq` per module or project; import the module under test.
- Name tests after behaviours; one assertion idea per test.
- Use `std.testing` so failures print the values involved.

## Things to avoid

- Shadowing a module-level function with a local of the same name (allowed, but confusing).
- Catching everything with `attempt` far from the failure; catch at the boundary you own.
- Deep nesting of `match` inside `match`; extract a function.
- Global mutable state through captured containers in lambdas that are passed around widely.
