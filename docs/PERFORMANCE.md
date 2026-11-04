# Tacqor Performance Report

This report describes how Tacqor executes code quickly, what was measured, how to reproduce the measurements, and how to write Tacqor programs that run fast. Benchmark sources and the runner are in `benchmarks/`.

## 1. Summary

- Native builds (`tacqor build --native`) run compute-bound code within 1.4–3× of C++ (`-O2`) and Rust (`-O`), and 1.6–90× faster than CPython 3.13.
- The bytecode VM is faster than CPython on every workload measured, by 1.1–5×.
- The gap to C++ and Rust comes from reference counting of containers and strings, checked indexing, and the generic dictionary; the gap is largest on dictionary-heavy code and smallest on arithmetic.
- Compile times are negligible: the whole standard library compiles in a few milliseconds; a native build takes as long as the system C++ compiler needs (typically 2–5 s at `-O3`).

## 2. Methodology

- Same algorithm, written idiomatically in each language, with the same checked semantics where the language offers them (C++ uses `__builtin_add_overflow` and `vector::at`; Rust uses `checked_add` and indexed `Vec`).
- One Linux x64 machine (2 vCPUs, shared), GCC 13.3, rustc 1.97, CPython 3.13.18, Tacqor 1.2 release binary.
- Flags: `c++ -std=c++17 -O2`, `rustc -O`, Tacqor native at `-O3` (the backend's default), Tacqor VM via `tacqor build` (embedded-VM executable).
- Wall-clock time of the whole process including startup, best of nine runs (three invocations of `benchmarks/run.sh`, each best of three). The machine is shared, so individual runs vary by up to 50 %; minima are the stable statistic.
- Output of every program is checked to be identical across languages before timing.

## 3. Benchmarks

| Name | What it measures | Size |
|---|---|---|
| `loop` | function calls with checked integer arithmetic in a tight loop | 12 M calls |
| `fib` | recursion, call overhead | fib(30) = 1.3 M calls |
| `sieve` | `list[bool]` indexing and nested loops | 5 M elements |
| `words` | dictionary updates with freshly built string keys | 2 M updates, 1,000 keys |
| `closures` | `lists.map` and `lists.reduce` with lambdas (standard library) | 2 M elements |
| `objs` | struct allocation and field access with the cycle collector enabled | 3 M allocations |

`closures` and `objs` have no C++/Rust counterpart because they exercise Tacqor-specific machinery (closure calls through the standard library, refcounted objects under the collector).

## 4. Results

Seconds, lower is better.

| Benchmark | C++ -O2 | Rust -O | **Tacqor native** | Tacqor VM | Python 3.13 |
|---|---|---|---|---|---|
| `loop` (12 M checked-arithmetic calls) | 0.020 | 0.021 | **0.028** | 0.52 | 2.53 |
| `fib` (fib(30), 1.3 M calls) | 0.005 | 0.008 | **0.014** | 0.064 | 0.129 |
| `sieve` (5 M, `list[bool]`) | 0.041 | 0.031 | **0.096** | 0.43 | 1.03 |
| `words` (2 M dict updates, str keys) | 0.105 | 0.156 | **0.30** | 0.44 | 0.47 |
| `closures` (map/reduce 2 M items) | — | — | **0.085** | 0.23 | 0.34 |
| `objs` (3 M struct allocations) | — | — | **0.19** | 0.27 | 1.01 |

Ratios:

| Benchmark | native vs. best of C++/Rust | native vs. CPython | VM vs. CPython |
|---|---|---|---|
| `loop` | 1.4× slower | 90× faster | 4.9× faster |
| `fib` | 1.8–2.8× slower | 9× faster | 2.0× faster |
| `sieve` | 2.3–3.1× slower | 11× faster | 2.4× faster |
| `words` | 1.9–2.8× slower | 1.6× faster | 1.1× faster |
| `closures` | — | 4× faster | 1.5× faster |
| `objs` | — | 5× faster | 3.7× faster |

## 5. Where the time goes

**`loop` and `fib`** — the native backend keeps `i64` values in C++ locals (registers after optimization); overflow checks compile to `__builtin_*_overflow`, which the compiler lowers to a flag test. The remaining difference to C++ is the per-call `DepthGuard` (one increment, one compare, one decrement) and the runtime-error plumbing each function carries. The VM pays dispatch (one indirect jump per instruction), operand decoding and stack traffic; superinstructions (`add_store`, `jump_unless_locals`, `loop_next`) cut the instruction count by roughly a third on such loops.

**`sieve`** — `list[bool]` is stored as a vector of bytes (not `std::vector<bool>`), so the inner loop is a bounds check plus a byte store. The gap to Rust is the list header indirection on every access (the list is a refcounted heap object reached through a `Value`) and the bounds check being a call to the runtime error path rather than a branch to a trap. Both are candidates for future work (hoisting the data pointer and length out of loops).

**`words`** — each iteration builds a new string (`"k" + str(i % 1000)`), hashes it, probes the dictionary and releases the string. C++ and Rust do the same work; Tacqor additionally refcounts the key and goes through `Value` tagging in the dictionary's generic key comparison. CPython is close on this benchmark because its dictionary and string machinery are its strongest components.

**`closures`** — `lists.map` and `lists.reduce` are Tacqor source in the standard library; the lambda call is a `call_value` through a `ClosureObj`. Natively, the call becomes a switch over the closure's function id (a thunk table), which the C++ compiler can inline when the set of candidates is small. The VM allocates one frame per call.

**`objs`** — struct allocation is one heap allocation plus registration with the cycle collector; the collector's threshold (max(200,000, 2× live objects)) keeps collection cost amortized to a few percent. Native and VM are close here because allocation dominates in both.

## 6. What the native backend does

1. Abstractly interprets each function's bytecode to assign a static type (`i64`, `f64`, `bool`, or boxed `Value`) to every stack slot at every instruction.
2. Emits one C++ local per (slot, type): `i3`, `f7`, `b2`, `v4`. Integers and floats never touch a `Value` unless they flow into a container or a generic call.
3. Translates each instruction to a C++ statement; jumps become `goto` labels; loops become loops the C++ optimizer recognizes.
4. Inlines small builtins (`len`, `abs`, arithmetic, comparisons) and emits calls into the embedded runtime for the rest (strings, containers, I/O, sockets).
5. Closures and trait methods become `switch` dispatchers over the known candidates, so there is no indirect call the optimizer cannot see through.
6. Compiles the result with the system compiler at `-O3` and links the runtime statically into the executable.

See `ARCHITECTURE.md` §8 and figure 06 for the pipeline.

## 7. What the VM does

- Computed-goto threaded dispatch (GCC/Clang) with a `switch` fallback.
- One contiguous value stack; frames are offsets into it, so calls do not allocate.
- `Value` is 16 bytes; integers, floats and booleans are unboxed; strings and containers are refcounted pointers.
- Typed list storage (`list[i64]`, `list[f64]`, `list[bool]`) stores raw elements, not `Value`s.
- Superinstructions fuse the common sequences the compiler emits (load two locals, compare-and-branch on locals, arithmetic-and-store, range loop step).
- String constants are immortal; they are never refcounted.

See `ARCHITECTURE.md` §5 and figure 03.

## 8. Writing fast Tacqor

| Habit | Why |
|---|---|
| Build with `--native` for anything compute-bound | 5–18× over the VM on arithmetic and loops, 1.5–3× on container-heavy code. |
| Give lists an element type the runtime can unbox (`list[i64]`, `list[f64]`, `list[bool]`) | Raw storage; no tagging, no refcounts. |
| Keep numeric work in `i64`/`f64` locals; avoid pushing scalars into `list[Value]`-like containers inside hot loops | Scalars stay in registers natively. |
| Hoist repeated `len(xs)` out of loops when the list is not modified | Avoids a call per iteration in the VM (the native backend often hoists it already). |
| Prefer `for x in xs` over index loops when the index is not needed | `iter_next` is one instruction; indexing is two plus a bounds check. |
| Build large strings with `join` on a list rather than repeated `+` | `+` copies; `join` allocates once. |
| Reuse a dictionary instead of rebuilding it; precompute keys that are reused | Each fresh key is an allocation plus a hash. |
| Use `parallel_sum` / `parallel_map` for independent per-element work on large inputs | Scales across cores; mapper closures may capture only scalars. |
| Avoid deep recursion where a loop will do | Calls are cheap but not free; recursion depth is bounded (see `SPECIFICATION.md` limits). |
| Let the collector run; do not create reference cycles you do not need | Acyclic garbage is freed immediately by refcounting; cycles wait for the collector. |

Measure before optimizing: `time_ms()` returns a millisecond clock, and `tacqor dump` shows the instructions a function compiles to.

## 9. Compile and startup times

Measured on the same machine with the release binary (process wall-clock, including startup):

| Operation | Time |
|---|---|
| `tacqor check` of a program importing every standard module (≈ 1,000 lines of library, 2,500 instructions) | ≈ 8 ms |
| `tacqor hello.tq` end to end (start, compile, run, exit) | ≈ 4 ms |
| `tacqor run program.tqb` for the 2,500-instruction program (decode, verify, run) | ≈ 6 ms |
| `tacqor build` (bytecode + embedded VM) of the same program | ≈ 20 ms |
| `tacqor build --native` of a 10-line program | ≈ 6 s |
| `tacqor build --native` of the program importing every standard module | ≈ 19 s |

Native build time is the system C++ compiler at `-O3` on the generated source plus the embedded runtime; it grows with the amount of Tacqor code compiled in (every non-generic function of every imported module is compiled, used or not; generic functions only when instantiated). Use the VM while developing and build natively for release.

## 10. Reproducing

```sh
sh benchmarks/run.sh                 # uses ./tacqor or linux-x64/tacqor
sh benchmarks/run.sh /path/to/tacqor
```

The script builds every benchmark with every toolchain it finds (`c++`, `rustc`, `python3` are optional) and prints a table of best-of-three times. Run it three times and take the minimum of each cell to reproduce the table in §4.

## 11. Known gaps and planned work

- Hoist list data pointers and lengths out of loops in the native backend (`sieve`-style code).
- Specialize dictionaries with `str` keys to avoid generic key comparison (`words`-style code).
- Inline caching of closure targets in the VM.
- A register-based VM would remove most stack traffic; it is a large change and is deferred until the native backend stops being the answer for hot code.
