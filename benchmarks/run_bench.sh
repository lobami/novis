#!/usr/bin/env bash
# ==============================================================================
# Novis vs Python benchmark runner
# Created by Loth Mejía Martínez · México · 2026
# ==============================================================================
# Runs a small but representative benchmark set and prints a side-by-side
# timing comparison.
#
# Caveats:
#   * Novis is currently a tree-walking interpreter, so it is expected to be
#     slower than CPython on most pure-CPU tasks. The point of this script is
#     to establish a baseline, not to claim victory.
#   * All benchmarks are pure-CPU so the difference is dominated by interpreter
#     overhead, which is exactly the dimension we want to track.
#   * Once Novis is lowered to LLVM/Wasm, this script becomes the regression
#     harness for the "be at least as fast as CPython" milestone.
# ==============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NOVIS="$ROOT/novis"
PYTHON="${PYTHON:-python3}"

if [ ! -x "$NOVIS" ]; then
    echo "error: $NOVIS not built. Run 'make' first." >&2
    exit 1
fi

if ! command -v "$PYTHON" >/dev/null 2>&1; then
    echo "error: $PYTHON not found on PATH." >&2
    exit 1
fi

ITERS="${ITERS:-3}"
N_FIB="${N_FIB:-30}"
N_SUM="${N_SUM:-1000000}"

hr() { printf -- '------------------------------------------------------------\n'; }

bench() {
    local name="$1"
    local novis_src="$2"
    local py_src="$3"

    printf '%-32s' "$name"
    # Warm-up
    "$NOVIS" run "$novis_src" >/dev/null
    "$PYTHON" "$py_src" >/dev/null

    local best_novis_ms=99999999
    local best_py_ms=99999999
    for _ in $(seq 1 "$ITERS"); do
        local t0 t1 ms
        t0="$(date +%s%N)"
        "$NOVIS" run "$novis_src" >/dev/null
        t1="$(date +%s%N)"
        ms=$(( (t1 - t0) / 1000000 ))
        if [ "$ms" -lt "$best_novis_ms" ]; then best_novis_ms="$ms"; fi

        t0="$(date +%s%N)"
        "$PYTHON" "$py_src" >/dev/null
        t1="$(date +%s%N)"
        ms=$(( (t1 - t0) / 1000000 ))
        if [ "$ms" -lt "$best_py_ms" ]; then best_py_ms="$ms"; fi
    done

    local ratio
    ratio=$(python3 -c "print(f'{$best_novis_ms / $best_py_ms:.2f}')")
    printf 'novis=%6dms  py=%6dms  ratio=%sx\n' "$best_novis_ms" "$best_py_ms" "$ratio"
}

hr
echo "Novis vs Python — best of $ITERS runs, lower is better"
echo "  novis  : $($NOVIS --help | head -n 1)"
echo "  python : $($PYTHON --version 2>&1)"
echo "  fib n  : $N_FIB"
echo "  sum n  : $N_SUM"
hr

# fib benchmark reads n from a single N_FIB compile-time literal, so we
# rewrite the source for each run with the requested n.
make_novis_fib() {
    local n="$1"
    local out="$ROOT/benchmarks/.fib_${n}.novis"
    cat > "$out" <<EOF
fn fib(n: int) -> int:
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

print(fib(${n}))
EOF
    printf '%s' "$out"
}

FIB_NOVIS_SRC="$(make_novis_fib "$N_FIB")"
trap 'rm -f "$FIB_NOVIS_SRC"' EXIT

# We use a python helper that writes the same fib source for the run.
make_py_fib() {
    local n="$1"
    local out="$ROOT/benchmarks/.fib_${n}.py"
    cat > "$out" <<EOF
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)
print(fib(${n}))
EOF
    printf '%s' "$out"
}
FIB_PY_SRC="$(make_py_fib "$N_FIB")"
trap 'rm -f "$FIB_NOVIS_SRC" "$FIB_PY_SRC"' EXIT

# Rewrite sum_squares to honor N_SUM. We construct a Tensor literal that
# literally contains N_SUM elements at source level, which matches how Novis
# currently lowers tensor literals.
cat > "$ROOT/benchmarks/.sum_squares_${N_SUM}.novis" <<EOF
elements: Tensor<f32> = [$(seq 1 ${N_SUM} | tr '\n' ',' | sed 's/,$//')]
print(sum(elements))
EOF

cat > "$ROOT/benchmarks/.sum_squares_${N_SUM}.py" <<EOF
n = ${N_SUM}
print(sum(range(1, n + 1)))
EOF

trap 'rm -f "$FIB_NOVIS_SRC" "$FIB_PY_SRC" \
        "$ROOT/benchmarks/.sum_squares_${N_SUM}.novis" \
        "$ROOT/benchmarks/.sum_squares_${N_SUM}.py"' EXIT

bench "fib(${N_FIB})"            "$FIB_NOVIS_SRC"               "$FIB_PY_SRC"
bench "sum_squares(${N_SUM})"     "$ROOT/benchmarks/.sum_squares_${N_SUM}.novis" \
                                  "$ROOT/benchmarks/.sum_squares_${N_SUM}.py"

hr
echo "ratio > 1.0 means Novis is faster than CPython on that benchmark."
echo "ratio < 1.0 means Novis is slower (expected for the tree-walking interpreter)."
echo
echo "Reproduce with:"
echo "  ITERS=5 N_FIB=30 N_SUM=1000000 $0"
