#!/bin/sh
# Command-line smoke suite. Usage: sh tests/smoke.sh ./tacqor
set -eu
tool="$1"
case "$tool" in /*) ;; *) tool="$PWD/$tool";; esac
temp=$(mktemp -d)
trap 'find "$temp" -depth -delete' EXIT
check_error() {
    printf '%b' "$1" > "$temp/bad.tq"
    if "$tool" check "$temp/bad.tq" 2>"$temp/bad.log"; then echo "expected an error for: $1"; exit 1; fi
    grep -q "$2" "$temp/bad.log"
}


# --- 0.4 behavior that must keep working -----------------------------------
test "$("$tool" run examples/parallel.tq)" = "332833500"
output=$("$tool" run examples/total.tq -- "hello world")
test "$output" = "Total:
60
Argument: hello world"
printf 'abc\n' > "$temp/sample.txt"
"$tool" run examples/file_size.tq -- "$temp/sample.txt" | tail -n 1 | grep -x 4
"$tool" compile examples/total.tq -o "$temp/total.tqb"
test "$("$tool" run "$temp/total.tqb" -- "hello world")" = "$output"
/usr/bin/env -i PATH=/unavailable "$tool" build examples/total.tq -o "$temp/total"
mkdir "$temp/isolated"
mv "$temp/total" "$temp/isolated/total"
test "$(cd "$temp/isolated"; /usr/bin/env -i PATH=/unavailable ./total "hello world")" = "$output"
test "$(cd "$temp/isolated"; "$tool" --version)" = 'Tacqor 1.3.0'

printf 'def main():\n    print(1 + True)\n' > "$temp/error.tq"
if "$tool" diagnose "$temp/error.tq" > "$temp/findings.json"; then exit 1; fi
grep -q '"code":"compile-error"' "$temp/findings.json"
"$tool" diagnose examples/live_warnings.tq > "$temp/warnings.json"
for code in zero-divisor integer-overflow invalid-workers; do
    grep -q "\"code\":\"$code\"" "$temp/warnings.json"
done
printf 'def main():\n    let a = [1, 2]\n    print(a[2])\n' > "$temp/bounds.tq"
"$tool" diagnose "$temp/bounds.tq" | grep -q '"code":"index-out-of-bounds"'
if "$tool" run "$temp/bounds.tq" 2>"$temp/error.log"; then exit 1; fi
grep -q 'list index out of bounds' "$temp/error.log"
printf 'corrupt' > "$temp/corrupt.tqb"
if "$tool" run "$temp/corrupt.tqb" 2>"$temp/error.log"; then exit 1; fi
grep -q 'Tacqor' "$temp/error.log"
cat > "$temp/wide.tq" <<'TQ'
def value(index: i64) -> i64:
    if index < 2:
        return 9223372036854775807
    else:
        return -9223372036854775807

def main():
    print(parallel_sum(3, 1, value))
    print(parallel_sum(3, 2, value))
    print(parallel_sum(3, 64, value))
TQ
test "$("$tool" run "$temp/wide.tq")" = "9223372036854775807
9223372036854775807
9223372036854775807"

# --- 0.5 language features --------------------------------------------------
"$tool" run tests/features.tq > "$temp/features.out"
cmp "$temp/features.out" tests/features.expected
"$tool" compile tests/features.tq -o "$temp/features.tqb"
"$tool" run "$temp/features.tqb" | cmp - tests/features.expected
"$tool" build tests/features.tq -o "$temp/features"
"$temp/features" | cmp - tests/features.expected
"$tool" dump "$temp/features.tqb" | grep -q 'struct Point: x: f64, y: f64'

test "$("$tool" run examples/stats.tq | head -n 1)" = "5.0"
test "$("$tool" run examples/primes.tq | head -n 1)" = "10000th prime: 104729"
"$tool" run examples/inventory.tq | grep -q 'Low stock: latch (8)'
"$tool" run examples/word_count.tq -- README.md | grep -q 'distinct words'

# static findings added in 0.5
printf 'def main():\n    for i in range(0):\n        print(i)\n    assert 1 > 2\n' > "$temp/warn.tq"
"$tool" diagnose "$temp/warn.tq" > "$temp/warn.json"
grep -q '"code":"empty-range"' "$temp/warn.json"
grep -q '"code":"failing-assert"' "$temp/warn.json"
# a list whose length may change is not flagged
printf 'def main():\n    let a = [1, 2]\n    push(a, 3)\n    print(a[2])\n' > "$temp/grow.tq"
test "$("$tool" diagnose "$temp/grow.tq")" = "[]"
test "$("$tool" run "$temp/grow.tq")" = "3"

# compile errors
check_error 'def main():\n    print(1 + 2.0)\n' 'cannot mix i64 and f64'
check_error 'def main():\n    let a = 1\n    a = 2\n' 'immutable'
check_error 'def main():\n    let a = []\n' 'needs a type'
check_error 'def f(n: i64) -> i64:\n    if n > 0:\n        return 1\ndef main():\n    print(f(1))\n' 'without returning'
check_error 'def main():\n    break\n' 'inside a loop'
check_error 'def main():\n    return\n    print(1)\n' 'unreachable'
check_error 'def p():\n    print(1)\ndef main():\n    let x = p()\n' 'does not return a value'
check_error 'struct P:\n    x: i64\ndef main():\n    print(P(1).y)\n' 'has no field'

# runtime errors carry a traceback and a nonzero status
cat > "$temp/trace.tq" <<'TQ'
def inner(a: list[i64], i: i64) -> i64:
    return a[i]
def outer(i: i64) -> i64:
    return inner([1, 2], i)
def main():
    print(outer(5))
TQ
if "$tool" run "$temp/trace.tq" 2>"$temp/trace.log"; then exit 1; fi
grep -q 'list index out of bounds' "$temp/trace.log"
grep -q 'in outer (line 4)' "$temp/trace.log"
printf 'def main():\n    exit(3)\n' > "$temp/exit.tq"
if "$tool" run "$temp/exit.tq"; then exit 1; else test "$?" = 3; fi
printf 'def main():\n    var x = 9223372036854775807\n    x += argc() + 1\n' > "$temp/overflow.tq"
if "$tool" run "$temp/overflow.tq" 2>"$temp/overflow.log"; then exit 1; fi
grep -q 'i64 overflow' "$temp/overflow.log"
printf 'def main():\n    let d = {"a": 1}\n    print(d["b"])\n' > "$temp/key.tq"
if "$tool" run "$temp/key.tq" 2>"$temp/key.log"; then exit 1; fi
grep -q 'missing dictionary key "b"' "$temp/key.log"

# input() reads stdin
printf 'def main():\n    let name = input()\n    print("hi " + name)\n' > "$temp/input.tq"
test "$(printf 'Ada\n' | "$tool" run "$temp/input.tq")" = "hi Ada"
# write_text round trip
printf 'def main():\n    write_text(arg(0), "written")\n    print(read_text(arg(0)))\n' > "$temp/write.tq"
test "$("$tool" run "$temp/write.tq" -- "$temp/out.txt")" = "written"

# --- 1.0 language features: modules, generics, closures, Option/Result, std library ---------
"$tool" run tests/features_1_0.tq | cmp - tests/features_1_0.expected
"$tool" compile tests/features_1_0.tq -o "$temp/f10.tqb"
"$tool" run "$temp/f10.tqb" | cmp - tests/features_1_0.expected
"$tool" build tests/features_1_0.tq -o "$temp/f10"
"$temp/f10" | cmp - tests/features_1_0.expected
"$tool" test tests/unit_tests.tq | grep -q '8 passed, 0 failed'
printf 'def test_bad():\n    assert 1 == 2\ndef test_good():\n    assert True\n' > "$temp/t.tq"
if "$tool" test "$temp/t.tq" > "$temp/t.log"; then exit 1; fi
grep -q 'FAIL test_bad' "$temp/t.log"
grep -q 'PASS test_good' "$temp/t.log"
rm -rf "$temp/proj" && (cd "$temp" && "$tool" new proj > /dev/null)
(cd "$temp/proj" && "$tool" run main.tq | grep -q 'Hello, proj!')
(cd "$temp/proj" && "$tool" test tests.tq | grep -q '1 passed')
for module in lists strings math testing collections json time random os channels; do "$tool" check "std/$module.tq" > /dev/null; done

check_error 'import nowhere\ndef main():\n    print(1)\n' 'not found'
check_error 'def main():\n    let x = None\n' 'add an annotation'
check_error 'def f() -> Option[i64]:\n    return Some(1)\ndef main():\n    match f():\n        case Some(v):\n            print(v)\n' 'handle both cases'
check_error 'def f() -> Option[i64]:\n    return None\ndef main():\n    let v = f()?\n' "needs the enclosing function"
check_error 'def main():\n    var n = 1\n    let f = fn():\n        n = 2\n    f()\n' 'captured by value'
check_error 'def id[T](x: T) -> T:\n    return x\ndef main():\n    print(id(1, 2))\n' 'expects 1 argument'
check_error 'def main():\n    let f = fn(x: i64) => x\n    print(f("s"))\n' 'expected i64, found str'
printf 'import b\ndef main():\n    print(1)\n' > "$temp/a.tq"
printf 'import a\n' > "$temp/b.tq"
if "$tool" check "$temp/a.tq" 2>"$temp/cycle.log"; then exit 1; fi
grep -q 'circular import' "$temp/cycle.log"

# --- 1.1: traits, parallel closures, cycle collector, packages, repl ---------------------------
"$tool" run tests/features_1_1.tq | cmp - tests/features_1_1.expected
"$tool" compile tests/features_1_1.tq -o "$temp/f11.tqb"
"$tool" run "$temp/f11.tqb" | cmp - tests/features_1_1.expected
"$tool" build tests/features_1_1.tq -o "$temp/f11"
"$temp/f11" | cmp - tests/features_1_1.expected
"$tool" dump tests/features_1_1.tq | grep -q 'trait Shape: area: fn() -> f64'
check_error 'trait S:\n    def area(self) -> f64\nstruct C(S):\n    r: f64\ndef main():\n    print(1)\n' 'does not implement S.area'
check_error 'trait S:\n    def area(self) -> f64\nstruct C(S):\n    r: f64\n    def area(self) -> i64:\n        return 1\ndef main():\n    print(1)\n' 'requires fn() -> f64'
check_error 'trait S:\n    def area(self) -> f64\ndef f[T: S](x: T) -> f64:\n    return x.area()\ndef main():\n    print(f(3))\n' 'does not implement S'
check_error 'def main():\n    let xs = [1, 2]\n    print(parallel_sum(2, 1, fn(i: i64) => xs[i]))\n' 'may only capture'
# a cycle-heavy program stays small: 1M cyclic pairs must not keep more than a few thousand objects alive
cat > "$temp/cycles.tq" <<'TQ'
struct Node:
    next: Option[Node]
def main():
    for i in range(500000):
        let a = Node(None)
        let b = Node(Some(a))
        a.next = Some(b)
    gc_collect()
    print(gc_count() < 5000)
TQ
test "$("$tool" run "$temp/cycles.tq")" = "True"
# packages: local folder and manifest round trip
rm -rf "$temp/pkg" && mkdir -p "$temp/pkg/lib/src" "$temp/pkg/app"
printf 'def twice(x: i64) -> i64:\n    return x * 2\n' > "$temp/pkg/lib/src/lib.tq"
printf 'import mylib\ndef main():\n    print(mylib.twice(21))\n' > "$temp/pkg/app/main.tq"
(cd "$temp/pkg/app" && "$tool" add ../lib/src mylib > /dev/null && "$tool" run main.tq | grep -qx 42 && rm -rf packages && "$tool" install > /dev/null && "$tool" run main.tq | grep -qx 42 && "$tool" remove mylib > /dev/null && test ! -d packages/mylib)
# repl: expressions print, definitions persist, errors do not kill the session
printf 'let x = 20\nx + 22\ndef sq(n: i64) -> i64:\n    return n * n\n\nsq(x)\nx = 1\n:quit\n' | "$tool" repl > "$temp/repl.log"
grep -q '^>>> 42' "$temp/repl.log" || grep -q '42' "$temp/repl.log"
grep -q '400' "$temp/repl.log"
grep -q 'immutable' "$temp/repl.log"

# --- 1.2: bit operators, attempt, networking, CLI ergonomics, diagnostics -----------------
"$tool" run tests/features_1_2.tq | cmp - tests/features_1_2.expected
"$tool" compile tests/features_1_2.tq -o "$temp/f12.tqb"
"$tool" run "$temp/f12.tqb" | cmp - tests/features_1_2.expected
"$tool" build tests/features_1_2.tq -o "$temp/f12"
"$temp/f12" | cmp - tests/features_1_2.expected
"$tool" tests/features_1_2.tq | cmp - tests/features_1_2.expected      # `tacqor FILE.tq` runs it

# --- 1.3: wrapping arithmetic and SIMD-friendly bulk numeric ops (VM/bytecode/native parity) ---
"$tool" run tests/features_1_3.tq | cmp - tests/features_1_3.expected
"$tool" compile tests/features_1_3.tq -o "$temp/f13.tqb"
"$tool" run "$temp/f13.tqb" | cmp - tests/features_1_3.expected
"$tool" build tests/features_1_3.tq -o "$temp/f13"
"$temp/f13" | cmp - tests/features_1_3.expected
# the i64 bulk ops stay overflow-checked, and mismatched lengths are a runtime error
printf 'def main():\n    print(vadd([9223372036854775807], [1]))\n' > "$temp/ov.tq"
if "$tool" run "$temp/ov.tq" 2>"$temp/ov.log"; then exit 1; fi
grep -q 'overflow' "$temp/ov.log"
printf 'def main():\n    print(dot([1.0, 2.0], [1.0]))\n' > "$temp/dm.tq"
if "$tool" run "$temp/dm.tq" 2>"$temp/dm.log"; then exit 1; fi
grep -q 'equal length' "$temp/dm.log"
test "$("$tool" run examples/total.tq hello world | tail -n 1)" = "Argument: hello"   # args without --
printf '#!/usr/bin/env tacqor\ndef main():\n    print(args())\n' > "$temp/script.tq"
test "$("$tool" "$temp/script.tq" a b)" = '["a", "b"]'
printf 'def main():\n    let x: i64 = "oops"\n' > "$temp/caret.tq"
if "$tool" check "$temp/caret.tq" 2>"$temp/caret.log"; then exit 1; fi
grep -q '\^~~~~~' "$temp/caret.log"
printf 'def main():\n    print(1 << -1)\n' > "$temp/shiftwarn.tq"
"$tool" check "$temp/shiftwarn.tq" 2>&1 | grep -q 'invalid-shift'
printf 'def main():\n    print(1 << 64)\n' > "$temp/shift.tq"
if "$tool" run "$temp/shift.tq" 2>"$temp/shift.log"; then exit 1; fi
grep -q 'between 0 and 63' "$temp/shift.log"
check_error 'def main():\n    print(attempt(fn(x: i64) => x))\n' 'takes no arguments'
check_error 'def main():\n    print(1.5 & 1)\n' 'expected i64, found f64'
# http: a server process answering three requests, driven by the Tacqor client
port=$((20000 + $$ % 10000))
"$tool" run tests/http_server.tq -- "$port" > "$temp/http_server.log" 2>&1 &
server_pid=$!
sleep 1
"$tool" run tests/http_client.tq -- "$port" > "$temp/http_client.log" 2>&1 || { cat "$temp/http_client.log"; kill $server_pid 2>/dev/null; exit 1; }
wait $server_pid
test "$(cat "$temp/http_client.log")" = "GET /hello 
POST /items payload
500"
grep -q 'served' "$temp/http_server.log"
grep -q 'handler failed: deliberate' "$temp/http_server.log"
"$tool" check std/http.tq > /dev/null
# concurrency: a four-worker serve_concurrent pool answering 160 requests fired at once by the
# parallel_map client. Every response must carry its own token (share-nothing, no crossed heaps).
cport=$((40000 + $$ % 10000))
"$tool" run tests/concurrent_server.tq -- "$cport" > "$temp/cc_server.log" 2>&1 &
cc_pid=$!
sleep 1
"$tool" run tests/concurrent_client.tq -- "$cport" > "$temp/cc_client.log" 2>&1 || { cat "$temp/cc_client.log"; kill "$cc_pid" 2>/dev/null; exit 1; }
kill "$cc_pid" 2>/dev/null
grep -q 'requests=160 correct=160' "$temp/cc_client.log"
grep -q 'PASS' "$temp/cc_client.log"
# a task that captures a non-shareable value (a heap list) is refused at runtime, even when
# the task is bound to a name first (so the capture is not visible at the call site)
printf 'def main():\n    let xs = [1, 2]\n    let task = fn(w: i64):\n        print(xs[0])\n    parallel_run(2, task)\n' > "$temp/badrun.tq"
if "$tool" run "$temp/badrun.tq" 2>"$temp/badrun.log"; then exit 1; fi
grep -q 'only capture scalars and functions' "$temp/badrun.log"

# C FFI: extern declarations type-check, the VM reports that a native build is required, and
# non-marshalable signatures are rejected at compile time.
"$tool" check tests/ffi.tq > /dev/null
if "$tool" run tests/ffi.tq 2>"$temp/ffi_vm.log"; then exit 1; fi
grep -q 'needs a native build' "$temp/ffi_vm.log"
check_error 'extern def bad(xs: list[i64]) -> i64\ndef main():\n    print(1)\n' 'extern function parameter must be'
check_error 'struct P:\n    x: i64\nextern def bad(n: i64) -> P\ndef main():\n    print(1)\n' 'extern function must return'

# LSP: a scripted stdio session (initialize, a diagnostic on open, cleared on change). Needs python3.
if command -v python3 >/dev/null 2>&1; then
    python3 tests/lsp_check.py "$tool" | grep -q 'LSP PASS'
fi

# --- native backend (needs a C++17 compiler; skipped when none is installed) ------------
"$tool" emit tests/features.tq -o "$temp/features.cpp"
grep -q 'static void f0_' "$temp/features.cpp" || grep -q 'Generated by Tacqor' "$temp/features.cpp"
if command -v "${CXX:-c++}" >/dev/null 2>&1; then
    "$tool" build --native tests/features.tq -o "$temp/features_native"
    "$temp/features_native" | cmp - tests/features.expected
    "$tool" build --native examples/parallel.tq -o "$temp/parallel_native"
    test "$("$temp/parallel_native")" = "332833500"
    "$tool" build --native "$temp/wide.tq" -o "$temp/wide_native"
    test "$("$temp/wide_native")" = "9223372036854775807
9223372036854775807
9223372036854775807"
    "$tool" build --native "$temp/trace.tq" -o "$temp/trace_native"
    if "$temp/trace_native" 2>"$temp/trace_native.log"; then exit 1; fi
    grep -q 'list index out of bounds' "$temp/trace_native.log"
    grep -q 'in outer (line 4)' "$temp/trace_native.log"
    "$tool" build --native "$temp/exit.tq" -o "$temp/exit_native"
    if "$temp/exit_native"; then exit 1; else test "$?" = 3; fi
    "$tool" build --native "$temp/overflow.tq" -o "$temp/overflow_native"
    if "$temp/overflow_native" 2>"$temp/overflow_native.log"; then exit 1; fi
    grep -q 'i64 overflow' "$temp/overflow_native.log"
    "$tool" build --native "$temp/key.tq" -o "$temp/key_native"
    if "$temp/key_native" 2>"$temp/key_native.log"; then exit 1; fi
    grep -q 'missing dictionary key "b"' "$temp/key_native.log"
    "$tool" build --native "$temp/input.tq" -o "$temp/input_native"
    test "$(printf 'Ada\n' | "$temp/input_native")" = "hi Ada"
    "$tool" build --native examples/total.tq -o "$temp/total_native"
    test "$("$temp/total_native" "hello world")" = "$output"
    "$tool" build --native tests/features_1_0.tq -o "$temp/f10_native"
    "$temp/f10_native" | cmp - tests/features_1_0.expected
    "$tool" build --native tests/features_1_1.tq -o "$temp/f11_native"
    "$temp/f11_native" | cmp - tests/features_1_1.expected
    "$tool" build --native tests/features_1_2.tq -o "$temp/f12_native"
    "$temp/f12_native" | cmp - tests/features_1_2.expected
    "$tool" build --native tests/http_server.tq -o "$temp/http_native"
    port=$((30000 + $$ % 10000))
    "$temp/http_native" "$port" > "$temp/http_native.log" 2>&1 &
    native_pid=$!
    sleep 1
    "$tool" run tests/http_client.tq -- "$port" | grep -q 'POST /items payload'
    wait $native_pid
    grep -q 'served' "$temp/http_native.log"
    # concurrency, compiled to machine code: the same pooled server, native this time
    "$tool" build --native tests/concurrent_server.tq -o "$temp/cc_native"
    cport=$((50000 + $$ % 10000))
    "$temp/cc_native" "$cport" > "$temp/cc_native.log" 2>&1 &
    cc_native_pid=$!
    sleep 1
    "$tool" run tests/concurrent_client.tq -- "$cport" | grep -q 'correct=160'
    kill "$cc_native_pid" 2>/dev/null
    # C FFI: a native binary that calls libm (sqrt/pow), libc (llabs/strlen/getenv)
    "$tool" build --native tests/ffi.tq -o "$temp/ffi_native"
    test "$(TACQOR_FFI_SMOKE=ok "$temp/ffi_native")" = "12.0
81.0
7
1234
env:ok"
    "$tool" build --native "$temp/cycles.tq" -o "$temp/cycles_native"
    test "$("$temp/cycles_native")" = "True"
    native_status="native backend, "
else
    native_status="native backend skipped (no C++ compiler), "
fi

printf 'standalone build, bytecode, types, scanner, bounds, arguments, file access, parallelism, 0.5 features, 1.0 features, 1.1 features, 1.2 features, concurrency, C FFI, LSP, and %spassed\n' "$native_status"
