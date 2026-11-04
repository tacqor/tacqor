#!/bin/sh
# Builds and times every benchmark in this folder on every available toolchain.
# Usage: sh benchmarks/run.sh [path/to/tacqor]   (defaults to ./tacqor, then linux-x64/tacqor)
set -e
cd "$(dirname "$0")"
TQ=${1:-../tacqor}
[ -x "$TQ" ] || TQ=../linux-x64/tacqor
OUT=$(mktemp -d)
best() {  # best of three wall-clock runs, seconds
    b=
    for i in 1 2 3; do
        s=$(date +%s.%N); "$@" > /dev/null; e=$(date +%s.%N)
        t=$(echo "$e - $s" | bc)
        if [ -z "$b" ] || [ "$(echo "$t < $b" | bc)" = 1 ]; then b=$t; fi
    done
    printf '%.3f' "$b"
}
printf '%-10s %-9s %-9s %-9s %-9s %-9s\n' benchmark c++ rust native vm python
for name in loop fib sieve words closures objs; do
    cpp=-; rs=-; py=-
    "$TQ" build "$name.tq" -o "$OUT/${name}_vm" >/dev/null
    "$TQ" build --native "$name.tq" -o "$OUT/${name}_native" >/dev/null
    if [ -f "$name.cpp" ] && command -v c++ >/dev/null; then c++ -std=c++17 -O2 "$name.cpp" -o "$OUT/${name}_cpp" && cpp=$(best "$OUT/${name}_cpp"); fi
    if [ -f "$name.rs" ] && command -v rustc >/dev/null; then rustc -O "$name.rs" -o "$OUT/${name}_rs" 2>/dev/null && rs=$(best "$OUT/${name}_rs"); fi
    if command -v python3 >/dev/null; then py=$(best python3 "$name.py"); fi
    printf '%-10s %-9s %-9s %-9s %-9s %-9s\n' "$name" "$cpp" "$rs" "$(best "$OUT/${name}_native")" "$(best "$OUT/${name}_vm")" "$py"
done
rm -rf "$OUT"
