#!/usr/bin/env bash
# Test runner for the Linux agent + HID-BPF driver. Wraps the existing cargo and
# ctest suites and the instrumented variants behind one entry point.
#
#   ./run-tests.sh                 core: cargo test + ctest + latency gate (no root)
#   ./run-tests.sh --sanitize      ASan/UBSan/LSan rerun of the C++ suites + lifecycle
#   ./run-tests.sh --coverage      llvm-cov over C++ + Rust -> build/coverage/
#   ./run-tests.sh --fuzz          cargo-fuzz the HID/JSON parsers (nightly)
#   ./run-tests.sh --root          (sudo) real BPF verifier load + bpftool cross-check
#   ./run-tests.sh --all           everything the current privilege level allows
#
# Only --root and --fuzz need extra tooling/privilege
# the rest run unprivileged.

set -euo pipefail

LINUX_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${LINUX_DIR}/build"
CARGO_MANIFEST="${LINUX_DIR}/Cargo.toml"
FUZZ_SECS="${FUZZ_SECS:-60}"

do_core=0 do_san=0 do_cov=0 do_fuzz=0 do_root=0
[[ $# -eq 0 ]] && do_core=1
for arg in "$@"; do
    case "$arg" in
        --sanitize) do_san=1 ;;
        --coverage) do_cov=1 ;;
        --fuzz)     do_fuzz=1 ;;
        --root)     do_root=1 ;;
        --all)      do_core=1; do_san=1; do_cov=1; do_fuzz=1
                    [[ "$(id -u)" -eq 0 ]] && do_root=1 ;;
        --core)     do_core=1 ;;
        *) echo "unknown flag: $arg" >&2; exit 2 ;;
    esac
done
# Any explicit flag without --core still implies core build for the .so it needs.
[[ $do_san -eq 1 || $do_cov -eq 1 || $do_fuzz -eq 1 || $do_root -eq 1 ]] && : "${do_core:=0}"

configure() { local dir="$1"; shift; cmake -S "$LINUX_DIR" -B "$dir" "$@" >/dev/null; }

# Build the default (uninstrumented) tree: libra_backend.so + BPF object + tests.
# cargo's ra-backend-sys links libra_backend.so from here (build.rs default dir).
build_default() {
    configure "$BUILD_DIR"
    cmake --build "$BUILD_DIR" -j
}

run_core() {
    build_default
    echo "== cargo test (Rust control plane + FFI contract) =="
    cargo test --manifest-path "$CARGO_MANIFEST"
    echo "== ctest (C++ data plane + math; bpf_verify self-skips unprivileged) =="
    ctest --test-dir "$BUILD_DIR" --output-on-failure -E 'latency_bench'
    echo "== latency gate (p99 < 125 us per packet, 8 kHz) =="
    "$BUILD_DIR/latency_bench"
}

run_sanitize() {
    local dir="${LINUX_DIR}/build-asan"
    configure "$dir" -DRA_SANITIZE=ON
    cmake --build "$dir" -j
    echo "== sanitized ctest (ASan + UBSan + LSan) =="
    # Exclude latency_bench (instrumentation skews timing) and bpf_verify (root).
    ASAN_OPTIONS="detect_leaks=1:abort_on_error=1" \
    UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1" \
        ctest --test-dir "$dir" --output-on-failure -E 'latency_bench|bpf_verify'
}

run_coverage() {
    command -v llvm-profdata >/dev/null || { echo "llvm-profdata missing; skip C++ coverage"; }
    local dir="${LINUX_DIR}/build-cov"
    configure "$dir" -DRA_COVERAGE=ON
    cmake --build "$dir" -j
    local out="${BUILD_DIR}/coverage"; mkdir -p "$out"
    local bins=(compile_check parity_tests json_tests shim_tests \
                dataplane_tests fixedpoint_tests sequence_tests lifecycle_tests)
    if command -v llvm-profdata >/dev/null && command -v llvm-cov >/dev/null; then
        local prof="$dir/profraw"; mkdir -p "$prof"; local objs=()
        for t in "${bins[@]}"; do
            LLVM_PROFILE_FILE="$prof/$t.profraw" "$dir/$t" >/dev/null 2>&1 || true
            objs+=(-object "$dir/$t")
        done
        llvm-profdata merge -sparse "$prof"/*.profraw -o "$dir/cpp.profdata"
        llvm-cov show "${objs[@]:1}" "$dir/${bins[0]}" \
            -instr-profile="$dir/cpp.profdata" -format=html \
            -output-dir="$out/cpp" -ignore-filename-regex='_deps|/usr/'
        echo "C++ coverage: $out/cpp/index.html"
    fi
    echo "== Rust coverage (cargo-llvm-cov) =="
    if cargo llvm-cov --version >/dev/null 2>&1; then
        cargo llvm-cov --manifest-path "$CARGO_MANIFEST" \
            --html --output-dir "$out/rust"
        echo "Rust coverage: $out/rust/html/index.html"
    else
        echo "cargo-llvm-cov not installed (cargo install cargo-llvm-cov); skipping"
    fi
}

run_fuzz() {
    if ! cargo +nightly fuzz --version >/dev/null 2>&1; then
        echo "cargo-fuzz/nightly unavailable (rustup toolchain install nightly;"
        echo "cargo +nightly install cargo-fuzz); skipping fuzz"
        return 0
    fi
    [[ -f "$BUILD_DIR/libra_backend.so" ]] || build_default
    echo "== fuzz: hid_descriptor (${FUZZ_SECS}s) =="
    ( cd "$LINUX_DIR/agentd" && cargo +nightly fuzz run hid_descriptor -- -max_total_time="$FUZZ_SECS" )
    echo "== fuzz: config_parse (${FUZZ_SECS}s) =="
    ( cd "$LINUX_DIR/agentd" && cargo +nightly fuzz run config_parse -- -max_total_time="$FUZZ_SECS" )
}

run_root() {
    if [[ "$(id -u)" -ne 0 ]]; then echo "--root needs sudo/root" >&2; return 1; fi
    [[ -d "$BUILD_DIR" ]] || build_default
    echo "== BPF verifier (real load + verify) =="
    ctest --test-dir "$BUILD_DIR" -R bpf_verify --output-on-failure -V
    echo "== bpftool latency cross-check (best effort) =="
    if command -v bpftool >/dev/null; then
        sysctl -w kernel.bpf_stats_enabled=1 >/dev/null 2>&1 || true
        local line
        line="$(bpftool prog show 2>/dev/null | grep -i rawaccel || true)"
        if [[ -n "$line" ]]; then
            bpftool prog show | grep -iA2 rawaccel
            echo "(divide run_time_ns by run_cnt for ns/run; must stay << 125000)"
        else
            echo "no rawaccel BPF prog loaded; start rawaccel-agentd with a mouse attached first"
        fi
    else
        echo "bpftool not found"
    fi
}

[[ $do_core -eq 1 ]] && run_core
[[ $do_san  -eq 1 ]] && run_sanitize
[[ $do_cov  -eq 1 ]] && run_coverage
[[ $do_fuzz -eq 1 ]] && run_fuzz
[[ $do_root -eq 1 ]] && run_root
echo "done."
