#!/bin/sh
# Times the 1.3 bulk numeric ops: the dot() builtin against the same inner product written
# as a hand loop, plus the C++ and CPython references. Cache-resident data, so this measures
# compute (vectorization), not memory bandwidth.
# Usage: sh benchmarks/numeric.sh [path/to/tacqor]
set -e
cd "$(dirname "$0")"
TQ=${1:-../tacqor}
[ -x "$TQ" ] || TQ=../linux-x64/tacqor
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

best() {  # best of three wall-clock runs, seconds
    b=
    for _ in 1 2 3; do
        s=$(date +%s.%N); "$@" > /dev/null; e=$(date +%s.%N)
        t=$(echo "$e - $s" | bc)
        if [ -z "$b" ] || [ "$(echo "$t < $b" | bc)" = 1 ]; then b=$t; fi
    done
    printf '%.3f' "$b"
}

echo "dot(): 16384-element f64 inner product, 20000 reps (327 M multiply-adds), cache-resident"
echo

# Tacqor: dot() builtin, portable and host-targeted.
"$TQ" build --native vdot.tq -o "$OUT/vdot_portable" >/dev/null
TACQOR_CXXFLAGS="-march=native" "$TQ" build --native vdot.tq -o "$OUT/vdot_march" >/dev/null
# Tacqor: the same work as a hand-written index loop.
"$TQ" build --native vdot_hand.tq -o "$OUT/vdot_hand" >/dev/null

printf '%-34s %s\n' "Tacqor dot() native (portable)"      "$(best "$OUT/vdot_portable") s"
printf '%-34s %s\n' "Tacqor dot() native (-march=native)" "$(best "$OUT/vdot_march") s"
printf '%-34s %s\n' "Tacqor hand index loop (native)"     "$(best "$OUT/vdot_hand") s"

if command -v c++ >/dev/null; then
    c++ -std=c++17 -O3 vdot.cpp -o "$OUT/vdot_cpp"
    printf '%-34s %s\n' "C++ -O3 (scalar)" "$(best "$OUT/vdot_cpp") s"
    if c++ -std=c++17 -O3 -march=native vdot.cpp -o "$OUT/vdot_cpp_m" 2>/dev/null; then
        printf '%-34s %s\n' "C++ -O3 -march=native (ceiling)" "$(best "$OUT/vdot_cpp_m") s"
    fi
fi
if command -v python3 >/dev/null; then
    printf '%-34s %s\n' "CPython 3 (no numpy)" "$(best python3 vdot.py) s"
fi

echo
echo "All programs print the same sum (9.8304e+08); the runner checks nothing but time."
