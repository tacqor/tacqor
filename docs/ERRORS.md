# Tacqor Diagnostics Catalog

Every message the compiler and runtime can produce, what it means, and how to fix it. Compile-time messages have the form `FILE:LINE:COLUMN: error: MESSAGE` followed by the source line and a caret; warnings use `warning [CODE]:`; runtime errors use `runtime error:` and a traceback. `tacqor diagnose` emits the same information as JSON.

## 1. Lexical and syntax errors

| Message | Meaning / fix |
|---|---|
| `indent with spaces` | A tab or form feed starts a line. Use four spaces per level. |
| `indentation must be a multiple of four spaces` | Re-indent; Tacqor does not accept 2-space indentation. |
| `inconsistent indentation` | A dedent landed on a level that was never opened. Align with an enclosing block. |
| `unexpected character` | A byte outside the language's character set (non-ASCII identifiers, `$`, `@`, `;`…). Identifiers are ASCII; put Unicode in strings. |
| `unexpected character after a number` | `12abc` or `1.` — separate the number from what follows; floats need digits after the point (`1.0`). |
| `unterminated string`, `unterminated string inside f-string` | A string literal runs to the end of the line. Strings cannot span lines; use `+` or `\n`. |
| `unknown string escape` | Only `\n \r \t \0 \" \\` (and `\{ \}` in f-strings) exist. Double the backslash for a literal one. |
| `unterminated '{' in f-string`, `empty '{}' in f-string` | Balance the braces; use `{{` for a literal brace. |
| `source contains a null byte`, `source exceeds the size limit` | Binary or oversized input (4 MB limit). |
| `incomplete expression` | Brackets or a trailing backslash left open at end of file. |
| `expected 'X'`, `expected a newline`, `expected an indented block`, `expected an identifier`, `expected a type`, `expected a statement` | The parser needed a specific token. The caret points at what it found instead. A statement must be a binding, assignment, call, or control-flow keyword; a bare expression is not a statement (except in the REPL). |
| `expected 'def', 'struct', 'trait', 'const', or 'import'` | Only declarations are allowed at the top level; code goes inside `def main():`. |
| `reserved identifier 'X'` | You used a keyword as a name (`list`, `str`, `fn`, `match`...). Rename. |
| `expected a decimal integer without leading zeroes` | `007` is not allowed; octal does not exist. Write `7`. |
| `integer literal is outside the i64 range` | Beyond ±9.22×10¹⁸. Use `f64` or split the value. |
| `invalid float literal` | Malformed exponent or digits. |
| `parenthesize 'not' here` | `a < not b` is ambiguous; write `a < (not b)`. |
| `use 'and' to combine comparisons` | `a < b < c` does not chain; write `a < b and b < c`. |
| `cannot assign to this expression` | Only names, `x[i]` and `x.field` are assignable. |
| `a method's first parameter must be self` | Methods inside a struct start with `self`. |
| `a trait body lists method signatures: def name(self, ...) -> type` | Traits contain signatures only, no bodies. |
| `a trait needs at least one method` | |
| `program nesting exceeds the compiler limit`, `expression exceeds the compiler limit` | More than 96 nested blocks/brackets or 96 operators in one expression. Split the code. |
| `unexpected end of source` | The file ended inside a construct. |

## 2. Names, modules and declarations

| Message | Meaning / fix |
|---|---|
| `unknown binding 'X'` | No local, constant or function of that name is in scope. Check spelling and that the variable is declared before use and in an enclosing block. |
| `unknown function 'X'`, `unknown method or function 'X'`, `unknown generic function 'X'` | Not a built-in, not declared in this module. Imported functions need the module prefix: `lists.map`. |
| `unknown type 'X'` | Misspelled type, or a struct from another module without its prefix (`geo.Point`). |
| `unknown module 'X'; import it first` | `X.something` where `X` is not an import alias. |
| `module 'a/b.tq' not found; looked in: …` | The import could not be resolved; the message lists every path tried. Check the file name, the `packages/` folder, `TACQOR_HOME`/`TACQOR_PATH`, or run `tacqor install`. |
| `circular import of 'X'` | A imports B imports A. Move shared code into a third module. |
| `could not read module file …`, `could not read module: …` | The file exists but is unreadable or too large. |
| `duplicate function/struct/trait method/field/method/import alias/name 'X'` | Two declarations with one name in a module (or two imports with one alias — use `as`). |
| `'X' is already bound in this scope` | A `let`/`var`/loop variable repeats a name visible in an enclosing block. Rename or reuse with assignment (`var`). |
| `'X' is captured from the enclosing function and cannot be redeclared here` | A lambda mentions `X` from outside *and* declares its own `X`. Rename one of them. |
| `missing def main():`, `entry point must be def main():` | The root file needs `def main():` with no parameters and no result (`check` and `test` do not require it). |
| `cannot call main`, `main cannot be used as a value` | `main` is the entry point only. |
| `a const must be an integer, float, bool, or string literal` | Constants are literals (optionally negated) or other constants, not expressions. |
| `'self' is only valid as a method parameter` | |

## 3. Types

| Message | Meaning / fix |
|---|---|
| `expected T, found U` | The most common error: a value of type `U` where `T` is required — argument, return, assignment, list element, annotation. Convert explicitly or fix the type. A struct is accepted where one of its traits is expected; nothing else converts implicitly. |
| `cannot mix i64 and f64; use float(x) or int(x)` | Arithmetic needs both operands of one numeric type. |
| `arithmetic expects i64 or f64, found T` | `+ - * /` on strings (except `str + str`), bools, lists... |
| `i64 uses '//' for division; use float(a) / float(b) for an f64 result` / `f64 uses '/' for division…` / `i64 uses '//='` / `f64 uses '/='` | Integer division is `//`, float division `/`. |
| `str supports only '+'` / `'+='` | Strings concatenate; use `repeat`, `replace`, etc. for other operations. |
| `order comparisons apply to i64, f64, and str` | `<` on other types; compare a field or key instead. |
| `'in' expects a list, dict, or str on the right` | |
| `indexing expects a list, dict, or str`, `slicing expects a list or str`, `only list and dict elements can be assigned` | |
| `field access expects a struct value, found T`, `field assignment expects a struct value` | Trait-typed values have no fields; use a method, or keep the concrete type. |
| `'S' has no field 'f'`, `'S' has no method 'm'`, `trait 'T' has no method 'm'` | |
| `an empty list needs a type, for example: let items: list[i64] = []` / `an empty dict needs a type…` | `[]`/`{}` with no context; add an annotation. |
| `the type of None is unknown here…`, `the error type of Ok is unknown here…`, `the value type of Err is unknown here…` | Give the full `Option`/`Result` type through an annotation, a parameter type or the function's return type. |
| `Some/Ok/Err takes one argument`, `None takes no arguments` | |
| `dict keys must be i64 or str` | Other key types are not supported; use `str(x)` or a different structure. |
| `this call does not return a value` | A procedure used as an expression. Make it return something or call it as a statement. |
| `'f' expects N argument(s)`, `'S' expects N field value(s)`, `'T.m' expects N argument(s)` | Arity mismatch. Struct constructors take one value per field in order. |
| `'X' is not a function` | A local of non-function type called with `()`. |
| `len expects str, list, or dict`, `expected a list, found T`, `expected a dict, found T`, `expected i64 or f64, found T`, `int expects f64, str, or bool`, `float expects i64, f64, or str`, `min and max expect i64, f64, or str`, `sort expects a list of i64, f64, str, or bool`, `sum expects a list of i64 or f64`, `min_of expects a list of i64, f64, or str`, `clear expects a list or dict`, `unwrap expects an Option or Result`, `unwrap_or…`, `expect expects an Option or Result`, `expected an Option, found T`, `expected a Result, found T` | A built-in received the wrong type; see `STDLIB.md` for signatures. |
| `too many list elements`, `too many dict entries`, `too many local bindings`, `too many distinct types` | 65,536 limits. |

## 4. Control flow and definite returns

| Message | Meaning / fix |
|---|---|
| `a path reaches the end without returning a value` | Some path through a value-returning function has no `return`. Add one (often in an `else` or after a loop). `exit`, `fail`, exhaustive `match` with all arms exiting, and `while True:` without `break` count as exits. |
| `a path reaches the end of the lambda without returning a value` | Same, for a block-bodied lambda with `-> T`. |
| `unreachable statement` | Code after `return`, `break`, `continue` or an exit. Remove it or fix the control flow. |
| `'f' does not return a value` | `return x` in a procedure. Add `-> T` or drop the value. |
| `'f' must return T` | Bare `return` in a value-returning function. |
| `'break'/'continue' is only valid inside a loop` | |
| `match needs at least one case` | |
| `match must handle both cases or end with 'case _'` | An `Option`/`Result` match is missing an arm. |
| `match on a plain value must end with 'case _'` | Literal matches need a default arm. |
| `this pattern needs an Option, but the value is T` (or `a Result`) | `Some`/`None` on a `Result` or vice versa. |
| `duplicate case` | The same `Some`/`None`/`Ok`/`Err` arm twice. |
| `this case is unreachable after 'case _'` | Move the default last. |
| `match works on Option, Result, i64, f64, bool, and str values` | Match on something else (a struct, a list): use `if`. |
| `'X' is immutable; declare it with var` | Assignment to a `let` binding. |
| `'X' is captured by value and cannot be assigned inside the lambda` | Lambdas copy captured values; mutate a captured container or return a value instead. |

## 5. Generics, traits and closures

| Message | Meaning / fix |
|---|---|
| `cannot infer type parameter 'T'; write f[...](...)` | No argument determines `T` (e.g. an empty list). Give explicit type arguments. |
| `type parameter 'T' is both A and B` | Conflicting inference; the arguments disagree. |
| `cannot match T against the declared parameter type` | Unification failed (argument shape differs from the parameter). |
| `function arity mismatch: expected N parameter(s)`, `expected a function without a result` | A lambda passed where a `fn` type with a different shape is expected. |
| `'f' is not generic`, `'f' takes N type argument(s)`, `'T' is a trait and takes no type arguments`, `type parameter 'T' takes no arguments`, `list takes one type argument`, `dict takes two type arguments` | Wrong number of `[...]` arguments. |
| `'f' is generic; instantiate it explicitly, for example f[i64]` | A generic function used as a value. |
| `type argument T for 'P' does not implement Trait` | A bound `[P: Trait]` is not satisfied. |
| `a type parameter bound must be a trait` | `[T: SomeStruct]` is not allowed. |
| `'S' does not implement Trait.method` | The struct lists the trait but lacks a method. |
| `'S.m' has signature A but Trait requires B` | The method exists with a different signature; match the trait exactly. |
| `'X' is not a trait` | Something other than a trait in a struct's parenthesized list (there is no inheritance). |
| `methods cannot have their own type parameters` | Make the struct generic instead. |
| `attempt expects a function value that takes no arguments, found T`, `'attempt' expects 1 argument: a fn() -> T` | Wrap the call: `attempt(fn() => risky(x))`. |
| `parallel mapper must be a fn(i64) -> i64, found T`, `parallel mapper must have signature fn(i64) -> i64`, `parallel mapper must name a function` | |
| `a parallel mapper may only capture i64, f64, or bool values (captured T)` | The lambda captures a list/dict/struct/str; compute from the index or capture only numbers. |
| `'?' expects an Option or Result, found T`, `'?' needs the enclosing function to return an Option/a Result, but it returns T`, `'?' error type E does not match the function's F` | `?` propagates the same kind of failure as the enclosing function returns. |

## 6. Warnings (`tacqor check`, editor)

| Code | Meaning |
|---|---|
| `zero-divisor` | The divisor is a constant 0 — will fail at run time if reached. |
| `integer-overflow` | Constant operands overflow `i64`. |
| `index-out-of-bounds` | The index is provably outside a list whose length is known and cannot change. |
| `empty-range` | `range(a, b)` with provably `b ≤ a` — the loop never runs. |
| `failing-assert` | The condition is provably `False`. |
| `invalid-shift` | A constant shift count outside 0–63. |
| `invalid-workers` | A constant worker count outside 1–64. |

Warnings never stop compilation; they are reported where the code would fail if executed. The editor extension shows them inline.

## 7. Runtime errors

Format: `FILE:LINE:COLUMN: runtime error: MESSAGE`, then `  in FUNCTION (line N)` per frame (innermost first), exit status 1 — or `Err(MESSAGE)` inside `attempt`.

| Message | Cause |
|---|---|
| `i64 overflow in +` / `-` / `*` / `//`, `i64 overflow in parallel_sum result` | Checked arithmetic. Use `f64`, restructure, or check bounds first. |
| `division by zero`, `modulo by zero` | |
| `shift count must be between 0 and 63` | |
| `list index out of bounds`, `string index out of bounds`, `argument index out of bounds` | Check `len` first or use slicing (which clamps). |
| `missing dictionary key "k"` | Use `lookup(d, k)` (returns `Option`) or `k in d`. |
| `unwrap on None`, `unwrap on Err: …`, `expect` messages | Match instead of unwrapping, or use `unwrap_or`. |
| `pop from an empty list`, `min_of of an empty list`, `max_of of an empty list` | |
| `assertion failed` | An `assert` was false. |
| any text | `fail(message)` or `expect(value, message)`. |
| `cannot convert "x" to i64` / `to f64`, `float is outside the i64 range`, `cannot convert nan to i64` | `int`/`float`/`floor`/`ceil`/`round` on bad input; use `parse_int`/`parse_float` for untrusted text. |
| `chr expects a Unicode scalar value`, `ord expects a non-empty string`, `ord expects valid UTF-8`, `from_bytes expects values from 0 to 255`, `fixed expects 0 to 40 digits`, `repeat expects a non-negative count`, `split expects a non-empty separator`, `random_int expects low <= high`, `exit expects a code from 0 to 255` | Argument validation in built-ins. |
| `expected a regular file: p`, `could not open file: p`, `could not open file for writing: p`, `could not write file: p`, `could not list directory: p`, `could not run: cmd`, `input could not be read` | I/O failures. Wrap in `attempt` or check with `file_exists` first. |
| `parallel_sum needs 1 to 64 workers`, `parallel_map needs 1 to 64 workers`, `a parallel mapper may only capture i64, f64, or bool values` | |
| `function value called with the wrong number of arguments`, `attempt expects a function that takes no arguments` | A closure's arity did not match (only possible through bytecode edits). |
| `value does not implement Trait` | A trait call on an object whose struct lacks the vtable (only possible through bytecode edits). |
| `function nesting exceeds the runtime limit` | Recursion deeper than 100,000 frames. Convert to a loop. |
| `value nesting exceeds the runtime limit` | Printing or comparing data nested more than 2,000 levels. |
| `value stack limit exceeded`, `expression stack limit exceeded` | 4 M values live at once in the VM (e.g. an enormous list literal built on the stack). |
| `text exceeds the size limit`, `list exceeds the size limit`, `input exceeds the size limit`, `message too large` | 256 MB limits; `std.http` limits requests to 8 MB. |
| `invalid file path` | A path containing a null byte. |
| `invalid bytecode …` | A corrupted or foreign `.tqb`/executable payload; the verifier rejected it. Recompile from source. |
| `the native build needs a working C++17 compiler (set CXX or TACQOR_CXX); command failed: …` | `build --native` could not run the C++ compiler. Install one or use `build`. |

## 8. Tooling errors

| Message | Cause |
|---|---|
| `could not open input` | The file given to the command does not exist. |
| `portable bytecode output must use the .tqb extension` | `compile -o` needs a `.tqb` name. |
| `output points to the source or Tacqor executable` | Refusing to overwrite the input or the toolchain. |
| `'X' already exists` (`new`), `packages/X already exists`, `'X' is neither a folder nor a git URL`, `git clone failed for …` | Package and project commands. |
| `usage: …` (exit 2) | Wrong command-line shape; `tacqor --help` lists the commands. |
