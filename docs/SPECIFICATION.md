# The Tacqor Language Specification

**Version 1.2** — This document defines the syntax and semantics of Tacqor precisely enough to write an independent implementation. The reference implementation's behaviour is normative where this text is silent. `LANGUAGE.md` is the readable reference for programmers; this is the specification.

Notation: grammar rules use EBNF — `{x}` zero or more, `[x]` optional, `x | y` alternatives, `"..."` literal tokens, `(...)` grouping.

---

## 1. Lexical structure

### 1.1 Source text

A source file is a sequence of bytes interpreted as UTF-8; the lexer operates on bytes and only ASCII characters are significant. A file may not contain a null byte. Size limit: 4,000,000 bytes. Line endings are `LF` or `CR LF`.

### 1.2 Lines, indentation and continuation

The grammar is line-oriented. A **logical line** ends at a newline that is not inside brackets (`( ) [ ] { }`) and not preceded by a backslash. Blank lines and lines containing only a comment are ignored.

The leading spaces of a logical line form its **indentation**, which must be a multiple of four; tab and form-feed characters in indentation are errors. When the indentation increases, an `INDENT` token is produced; when it decreases, one `DEDENT` per level, and the new level must be one previously seen on the stack ("inconsistent indentation" otherwise). At end of file every open level is closed.

### 1.3 Comments

`#` starts a comment that runs to the end of the physical line, except inside string literals. A first line beginning with `#!` is therefore a comment, which makes shebang scripts possible.

### 1.4 Tokens

```ebnf
name       = letter { letter | digit } ;      letter = "a".."z" | "A".."Z" | "_" ; digit = "0".."9" ;
integer    = digit { digit | "_" } ;          (* no leading zero unless the literal is 0 *)
float      = digit { digit | "_" } "." digit { digit } [ exponent ]
           | digit { digit | "_" } exponent ;
exponent   = ( "e" | "E" ) [ "+" | "-" ] digit { digit } ;
string     = '"' { character | escape } '"' ;
fstring    = 'f"' { character | escape | "{" expression "}" | "{{" | "}}" } '"' ;
escape     = "\n" | "\r" | "\t" | "\0" | '\"' | "\\" | "\{" | "\}" ;
symbol     = "(" | ")" | "[" | "]" | "{" | "}" | ":" | "," | "." | "?" | "~" | "&" | "|" | "^"
           | "+" | "-" | "*" | "/" | "//" | "%" | "<<" | ">>" | "=" | "==" | "!=" | "<" | "<=" | ">" | ">="
           | "->" | "=>" | "+=" | "-=" | "*=" | "/=" | "//=" | "%=" | "&=" | "|=" | "^=" | "<<=" | ">>=" ;
```

Underscores in numeric literals are separators and are ignored. An integer literal must fit in `i64`; `-9223372036854775808` is accepted as a whole. A letter or `.` directly after a number is an error. A string literal may not span lines. Inside the braces of an f-string, a quoted string may appear (`f"{d["k"]}"`); `{{` and `}}` denote literal braces.

### 1.5 Keywords

Reserved: `def struct trait let var if elif else while for in break continue return assert and or not True False None range fn match case import as const Some Ok Err Option Result i64 f64 bool str list dict`. The names of built-in functions are not reserved: a declaration or local of the same name shadows the built-in.

---

## 2. Grammar

### 2.1 Module

```ebnf
module      = { import | const | struct | trait | function } ;
import      = "import" name { "." name } [ "as" name ] NEWLINE ;
const       = "const" name "=" literal_expr NEWLINE ;           (* integer, float, bool, string, [-]number, or another const *)
function    = "def" name [ type_params ] "(" [ params ] ")" [ "->" type ] ":" block ;
params      = param { "," param } [ "," ] ;      param = name ":" type ;
type_params = "[" type_param { "," type_param } "]" ;   type_param = name [ ":" type ] ;
struct      = "struct" name [ type_params ] [ "(" [ type { "," type } ] ")" ] ":" NEWLINE INDENT
              { field | method } DEDENT ;
field       = name ":" type NEWLINE ;
method      = "def" name "(" "self" { "," param } ")" [ "->" type ] ":" block ;
trait       = "trait" name ":" NEWLINE INDENT { signature } DEDENT ;
signature   = "def" name "(" "self" { "," param } ")" [ "->" type ] NEWLINE ;
```

A struct's parenthesized list names the traits it implements. A trait must declare at least one method. `main` is `def main():` with no parameters and no result, and must exist in the root module when a program is run (not when it is checked or tested).

### 2.2 Types

```ebnf
type        = "i64" | "f64" | "bool" | "str"
            | "list" "[" type "]"
            | "dict" "[" type "," type "]"
            | "Option" "[" type "]"
            | "Result" "[" type "," type "]"
            | "fn" "(" [ type { "," type } ] ")" [ "->" type ]
            | [ name "." ] name [ "[" type { "," type } "]" ] ;      (* struct, trait, type parameter *)
```

### 2.3 Statements

```ebnf
block       = NEWLINE INDENT statement { statement } DEDENT ;
statement   = let | assignment | expression_stmt | if | while | for | match
            | "break" NEWLINE | "continue" NEWLINE | "return" [ expression ] NEWLINE | "assert" expression NEWLINE ;
let         = ( "let" | "var" ) name [ ":" type ] "=" expression NEWLINE ;
assignment  = target assign_op expression NEWLINE ;
target      = name | postfix_expr "[" expression "]" | postfix_expr "." name ;
assign_op   = "=" | "+=" | "-=" | "*=" | "/=" | "//=" | "%=" | "&=" | "|=" | "^=" | "<<=" | ">>=" ;
expression_stmt = ( call | method_call | question ) NEWLINE ;
if          = "if" expression ":" block { "elif" expression ":" block } [ "else" ":" block ] ;
while       = "while" expression ":" block ;
for         = "for" name "in" ( "range" "(" expression [ "," expression ] ")" | expression ) ":" block ;
match       = "match" expression ":" NEWLINE INDENT case { case } DEDENT ;
case        = "case" pattern ":" block ;
pattern     = "Some" "(" ( name | "_" ) ")" | "None" | "Ok" "(" ( name | "_" ) ")" | "Err" "(" ( name | "_" ) ")"
            | expression | "_" ;
```

When the expression of a `let`, `var`, assignment or `return` is a block-bodied lambda, the `NEWLINE` is supplied by the lambda's block.

### 2.4 Expressions

```ebnf
expression  = or_expr ;
or_expr     = and_expr { "or" and_expr } ;
and_expr    = not_expr { "and" not_expr } ;
not_expr    = "not" not_expr | comparison ;
comparison  = bitor [ ( "==" | "!=" | "<" | "<=" | ">" | ">=" | "in" ) bitor ] ;    (* no chaining *)
bitor       = bitxor { "|" bitxor } ;
bitxor      = bitand { "^" bitand } ;
bitand      = shift { "&" shift } ;
shift       = additive { ( "<<" | ">>" ) additive } ;
additive    = term { ( "+" | "-" ) term } ;
term        = unary { ( "*" | "/" | "//" | "%" ) unary } ;
unary       = ( "-" | "~" ) unary | postfix ;
postfix     = atom { "[" expression "]" | "[" [ expression ] ":" [ expression ] "]"
                   | "." name [ [ type_args ] "(" [ arguments ] ")" ] | "?" } ;
atom        = integer | float | string | fstring | "True" | "False" | "None"
            | "(" expression ")" | "[" [ arguments ] "]" | "{" [ entry { "," entry } ] "}"
            | name [ type_args ] "(" [ arguments ] ")" | name
            | ( "Some" | "Ok" | "Err" | "str" ) "(" [ arguments ] ")"
            | lambda ;
entry       = expression ":" expression ;
arguments   = expression { "," expression } [ "," ] ;
type_args   = "[" type { "," type } "]" ;
lambda      = "fn" "(" [ params ] ")" [ "->" type ] ( "=>" expression | ":" block ) ;
```

Precedence from lowest to highest is the order of the rules above; all binary operators are left-associative. Unary operators bind tighter than every binary operator. `not` may not appear as a direct operand of a comparison without parentheses.

---

## 3. Names and scopes

Each module has one namespace for functions, structs, traits, constants and import aliases; declaring the same name twice in it is an error. A module's declarations are visible throughout the module regardless of order, and to importers through the alias (`alias.name`, `alias.Type`, `alias.CONST`).

Within a function, bindings live in lexical block scopes (function body, each `if`/`elif`/`else` block, loop body, match arm). A binding is visible from its declaration to the end of its block. Declaring a name that is already bound in an enclosing block is an error ("already bound in this scope"); the binding of a captured variable inside a lambda is likewise an error. A local may shadow a module-level name or a built-in; within its scope the local is what the name means, so a shadowed function cannot be called by that name.

The loop variable of `for` is an immutable binding scoped to the body. `self` in a method is an ordinary immutable parameter.

---

## 4. Types and values

### 4.1 Types

Every expression has exactly one static type. The types are: `i64`, `f64`, `bool`, `str`, `list[T]`, `dict[K, V]` with `K` ∈ {`i64`, `str`}, `Option[T]`, `Result[T, E]`, function types `fn(T1, …, Tn) -> R` and `fn(T1, …, Tn)`, struct types (including instantiations of generic structs), trait types, and the special non-value type *none* (the result of a procedure), which cannot be bound or passed.

Type equality is structural for all built-in types and nominal for structs and traits (two distinct declarations are distinct types; one generic struct's instantiations are equal when their arguments are equal).

### 4.2 Assignability

A value of type `S` may be used where `T` is expected if `S = T`, or `T` is a trait that `S` implements, or `S = T` as trait types. No other implicit conversion exists.

### 4.3 Value and reference semantics

`i64`, `f64` and `bool` are copied on every use. `str` is immutable, so sharing is unobservable. `list`, `dict`, structs, closures, `Option` and `Result` values are **references**: binding, passing, returning, storing in a container or capturing shares the object. Mutation through any reference is visible through all. `let` forbids rebinding the name, not mutating the object.

### 4.4 Equality and ordering

`==`/`!=` are defined on any two values of the same type and compare structurally: by value for immediates and strings; element-wise for lists; by key set and values for dicts; field-wise for structs (same struct type); by function identity and captured values for closures; by case and payload for `Option`/`Result`. `<`, `<=`, `>`, `>=` are defined on `i64`, `f64` and `str` (bytewise). Comparisons on `f64` follow IEEE 754 (`nan` is unequal to everything).

### 4.5 Numeric semantics

`i64` is two's-complement 64-bit. `+ - *` raise a runtime error on overflow. `//` is floor division and `%` has the sign of the divisor; both raise on a zero divisor; `i64_min // -1` raises on overflow and `i64_min % -1` is 0. Unary `-` raises on `i64_min`. `& | ^ ~` are bitwise on the two's-complement representation; `<< n` and `>> n` require `0 ≤ n ≤ 63` (runtime error otherwise), `<<` discards bits shifted out, `>>` is arithmetic (sign-preserving).

`f64` is IEEE 754 binary64; `+ - * /` never raise; `%` is `fmod` adjusted to take the divisor's sign. `int(f)` truncates toward zero and raises for `nan` or out of range; `floor`, `ceil`, `round` (half away from zero) return `i64` with the same check.

---

## 5. Declarations

### 5.1 Bindings

`let name = e` / `var name = e` evaluate `e` and bind. With `: T`, `e` must be assignable to `T` and the binding has type `T`. Without, the binding has `e`'s type; `e` may not be `[]`, `{}`, `None`, `Ok(x)` or `Err(x)` whose type the context does not determine. Assignment `name = e` requires a `var` binding (not a captured one) and `e` assignable to its type. Compound assignments `x op= e` mean `x = x op e` with `x` evaluated once; for indexed and field targets the container/object and index are evaluated once.

### 5.2 Functions

A function has a fixed signature. Every control path of a function with a result type must end in `return e` (with `e` assignable to the result), `exit(...)`, `fail(...)`, an exhaustive `match` all of whose arms exit, or a `while True:` loop containing no `break`. Code after such an exit (or after `break`/`continue`) is an error. A procedure may `return` without a value.

### 5.3 Generics

Type parameters are bound per call. Inference unifies each declared parameter type expression with the corresponding argument type: a type parameter binds to the argument type on first occurrence and must match on later ones; `list[P]`, `dict[K, V]`, `Option[P]`, `Result[P, E]`, `fn(...)` and generic struct types unify component-wise. If any parameter remains unbound the call is an error (supply explicit arguments). Each distinct tuple of type arguments yields one instantiation, compiled as an ordinary function. A bound `T: Trait` requires the argument type to implement the trait (or be the trait).

### 5.4 Structs, methods and traits

A struct value is constructed by `Name(f1, …, fn)` with one argument per field in order (type arguments inferred from the arguments or given as `Name[T](…)`). Fields are read and written with `.field`. Methods are called with `value.method(args)`; `self` is the receiver.

A struct that names traits must define every trait method with exactly the trait's parameter types and result type. Calling a trait method on a value whose static type is the trait dispatches to the implementing struct's method at run time. A trait-typed value supports method calls, `==`, printing, and passing on; field access requires the static struct type.

### 5.5 Constants

A `const` is a compile-time literal of type `i64`, `f64`, `bool` or `str` (optionally negated, or another const). Uses are replaced by the value.

### 5.6 Modules and packages

`import a.b` binds the alias `b` (or the `as` name) to the module found as `a/b.tq` or `a/b/lib.tq` in: the importing file's directory; `packages/` next to the root program; `packages/` in the working directory; the standard library directory; each `TACQOR_PATH` entry. A module is loaded and compiled once per program; import cycles are errors. Imported modules' `main` functions are ignored.

---

## 6. Expressions

### 6.1 Evaluation order

Operands are evaluated left to right; arguments left to right; the receiver of a method call before its arguments; for `a[i] = e` and `a.f = e` the target's subexpressions before `e`. `and`/`or` evaluate the right operand only if needed. `range` bounds are evaluated once before the loop.

### 6.2 Literals

Integer and float literals as in §1.4. `[e1, …]` is a `list[T]` where `T` is the first element's type and every element must be assignable to it (or the expected type's element type). `{k1: v1, …}` likewise for `dict[K, V]`. `Some(e)` is `Option[T]`; `None`, `Ok(e)`, `Err(e)` take their full type from the expected type and are errors without one.

### 6.3 f-strings

`f"…{e}…"` is equivalent to concatenating the literal segments with `str(e)` for each expression; the result is `str`.

### 6.4 Indexing and slicing

`list[i]` and `str[i]` require `0 ≤ i < len` (runtime error otherwise); `str[i]` yields a one-byte string. `dict[k]` raises when `k` is absent. `x[a:b]`, `x[:b]`, `x[a:]` produce a new list or string with indices clamped to `[0, len]` (never negative-wrapped); `a > b` gives an empty result.

### 6.5 Calls

`f(args)` calls a module function, generic instantiation, struct constructor, function-typed local, or built-in, in that resolution order. `x.f(args)` calls a method of `x`'s struct or trait if one exists, else `f(x, args)`. A procedure call is only valid as a statement. Argument types must be assignable to parameter types.

### 6.6 Lambdas

`fn(p: T, …) [-> R] => e` and `fn(…) [-> R]: block` create a closure. Parameters are typed explicitly; the result type is `R` if given, otherwise the body expression's type (expression form only; a block form without `-> R` is a procedure). Every name in the body (including nested lambdas) that resolves to a binding of the enclosing function is **captured by value** when the lambda expression is evaluated; inside the lambda such names are immutable. The closure's type is `fn(T, …) -> R`.

### 6.7 `?`

`e?` requires `e : Option[T]` in a function returning `Option[U]`, or `e : Result[T, E]` in a function returning `Result[U, E]`. If `e` is `Some(v)`/`Ok(v)` the expression's value is `v`; otherwise the enclosing function returns `e` immediately.

### 6.8 `attempt`

`attempt(f)` with `f : fn() -> T` evaluates `f()` and yields `Ok(result) : Result[T, str]`; if a runtime error occurs during the call the stack is unwound to the `attempt` and the value is `Err(message)`. If `f` is a procedure the type is `Result[bool, str]` with `Ok(True)`. `exit` is not intercepted.

### 6.9 Parallel operations

`parallel_sum(count, workers, m)` and `parallel_map(count, workers, m)` require `m : fn(i64) -> i64` and `1 ≤ workers ≤ 64`. They evaluate `m(i)` for every `0 ≤ i < count` on up to `workers` threads and return the exact sum (runtime error if it does not fit `i64`) or the list of results in index order. If `m` is a closure, all its captured values must be `i64`, `f64` or `bool` (compile-time error for a lambda literal, runtime error otherwise). The first error raised, in index order, propagates.

`parallel_run(workers, t)` requires `t : fn(i64) -> ()` (a task returning nothing) and `1 ≤ workers ≤ 64`. It evaluates `t(k)` for every `0 ≤ k < workers`, each on its own thread with its own VM, and returns once all have returned (a task that loops forever makes the call block forever). It returns `workers`. If `t` is a closure, every captured value must be a scalar (`i64`, `f64`, `bool`) or a function value — any other captured heap value is a compile-time error for a lambda literal and a runtime error otherwise, because a shared task is read by every worker and the implementation's reference counting is not atomic. The shared task closure and the function values it captures are not freed for the remainder of the program. The first error raised by any task propagates after all tasks finish.

---

## 7. Statements

- `if`/`elif`/`else`: conditions are `bool`. Each block is a scope.
- `while c:` evaluates `c` before each iteration.
- `for i in range(n)` / `range(a, b)`: `i` takes `a, a+1, …, b-1` (`a = 0` by default); bounds evaluated once; empty when `b ≤ a`. `for x in L`: `x` takes each element of the list `L` as it is at each step (`L` is evaluated once; mutations during the loop are observed through the current length).
- `break` / `continue` affect the innermost loop and must be inside one.
- `match` is described in §2.3/§5; arms are tried in order; the scrutinee is evaluated once; a non-exhaustive match is a compile error; an `Option`/`Result` match may not repeat an arm; nothing may follow `case _`.
- `assert c` raises `assertion failed` when `c` is `False`.
- An expression statement evaluates a call (or `e?`) and discards its result.

---

## 8. Runtime errors

A runtime error terminates the program with status 1 after writing `FILE:LINE:COLUMN: runtime error: MESSAGE` and, when the call depth exceeds one, a traceback of `in FUNCTION (line N)` lines, to standard error — unless it occurs inside `attempt`. The defined runtime errors are: integer overflow (`+ - * // -` and parallel sums), division or modulo by zero, invalid shift count, index out of bounds (list, string, argument), missing dictionary key, `unwrap`/`expect` on `None`/`Err`, `pop`/`min_of`/`max_of` on an empty list, `assert` failure, `fail(message)`, conversion failures (`int`, `float`, `chr`, `ord`, `from_bytes`, `fixed`), invalid worker count, invalid parallel capture (including a non-shareable `parallel_run` task capture), function nesting beyond 100,000 frames, value nesting beyond 2,000, resource limits (text 256 MB, list 256 M elements, stack 4 M values), and I/O failures reported by built-ins that do not return `Result`.

`exit(code)` ends the program immediately with `code` (0–255) after flushing output.

---

## 9. Compile-time diagnostics

A conforming implementation rejects programs that violate any rule in §§1–7 with a diagnostic carrying the file, line and column. The reference implementation additionally reports **warnings** that do not prevent execution: a divisor that is a constant zero, an arithmetic operation whose constant operands overflow, an index that is provably outside a list of known length, a loop with provably no iterations, an assertion that is provably false, a shift count provably outside 0–63, and a parallel worker count provably outside 1–64.

---

## 10. Limits

| Limit | Value |
|---|---|
| Source file | 4,000,000 bytes |
| Block / expression nesting | 96 levels; expression height 192; 96 operators per expression |
| Locals per function; list literal size | 65,536 |
| Call depth | 100,000 frames |
| Value stack (VM) | 4,000,000 values (512,000 per worker) |
| Text or file size | 256,000,000 bytes |
| Value nesting (print, compare) | 2,000 levels |
| Bytecode file | 64,000,000 bytes |
| Parallel workers | 64 |

---

## 11. Standard environment

The entry point is `main`. `args()` returns the arguments after the program name (after `--` when run through `tacqor run`). `print`/`write` go to standard output, `eprint` to standard error, `input` reads standard input. The built-in functions and the standard library are specified in `STDLIB.md`; their signatures are part of this specification.

---

## 12. Conformance

An implementation conforms if, for every valid program, it produces the same output and exit status as this specification defines, and for every invalid program it reports a diagnostic. The reference implementation's differential test suite (`tests/smoke.sh`) is the executable form of this requirement: all four execution paths must agree byte for byte.
