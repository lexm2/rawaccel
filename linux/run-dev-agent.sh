#!/usr/bin/env bash
# Dev launcher for rawaccel-agentd. Builds if needed, runs the daemon under
# sudo against a per-user socket in $XDG_RUNTIME_DIR, and chowns the socket
# back to the calling user so the GUI (running unprivileged) can connect.
# Not for production install; for that use `cmake --install` + systemctl.

set -euo pipefail

LINUX_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${LINUX_DIR}/build"
# CMake builds the C++ data-plane .so + shim + BPF object; cargo builds the
# Rust daemon. The daemon's rpath defaults to ${BUILD_DIR}, so it finds
# libra_backend.so there without LD_LIBRARY_PATH.
AGENT="${LINUX_DIR}/target/release/rawaccel-agentd"
SHIM="${BUILD_DIR}/librawaccel_common.so"
BACKEND_LIB="${BUILD_DIR}/libra_backend.so"
BPF_OBJ="${BUILD_DIR}/rawaccel.bpf.o"

SOCKET_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
SOCKET="${SOCKET_DIR}/rawaccel.sock"
# Backend: auto (probe kernel, pick bpf if >= 6.11 else exit), bpf (force
# HID-BPF), or noop (control plane only, for tests). The old evdev backend
# was removed; HID-BPF is the only data-plane transport.
BACKEND="${1:-auto}"
UID_NUM="$(id -u)"
GID_NUM="$(id -g)"

if [[ ! -f "${BACKEND_LIB}" || ! -f "${SHIM}" || ! -f "${BPF_OBJ}" ]]; then
    echo "Building libra_backend.so, the curve shim, and the BPF object..."
    cmake -S "${LINUX_DIR}" -B "${BUILD_DIR}" >/dev/null
    cmake --build "${BUILD_DIR}" --target ra_backend rawaccel_common_shim rawaccel_bpf_object
fi
if [[ ! -x "${AGENT}" ]]; then
    echo "Building the Rust daemon (rawaccel-agentd)..."
    cargo build --release --manifest-path "${LINUX_DIR}/Cargo.toml" -p rawaccel-agentd
fi

if [[ ! -d "${SOCKET_DIR}" ]]; then
    echo "error: ${SOCKET_DIR} does not exist; XDG_RUNTIME_DIR is unset and no /run/user/${UID_NUM}" >&2
    exit 1
fi

# The agent chowns the socket to SUDO_UID/SUDO_GID itself after bind, so
# the GUI (running unprivileged) can connect without a second sudo prompt.

cat <<EOF
[run-dev-agent] agent backend : ${BACKEND}
[run-dev-agent] socket path   : ${SOCKET}
[run-dev-agent] shim path     : ${SHIM}

The rawaccel binary launches the GUI and runs CLI commands; build it once with
\`cargo build --release --manifest-path ${LINUX_DIR}/Cargo.toml -p rawaccel-cli\`.

For the GUI in another terminal (no args -> launches the GUI, finds the source
tree and sets LD_LIBRARY_PATH itself):
    RAWACCEL_SOCKET=${SOCKET} ${LINUX_DIR}/target/release/rawaccel

To check the agent from the CLI:
    RAWACCEL_SOCKET=${SOCKET} ${LINUX_DIR}/target/release/rawaccel status

EOF

# Run sudo in the background and trap SIGINT/SIGTERM so Ctrl+C in this
# terminal forwards a clean shutdown signal to the agent instead of being
# swallowed. Using exec sudo would replace the shell and remove the trap.
sudo "${AGENT}" --backend "${BACKEND}" --socket "${SOCKET}" --bpf-object "${BPF_OBJ}" &
agent_pid=$!

shutdown() {
    kill -INT "${agent_pid}" 2>/dev/null || true
}
trap shutdown INT TERM

# Loop because the first wait returns once the signal handler runs; the
# agent may still need a moment to flush its shutdown path.
while kill -0 "${agent_pid}" 2>/dev/null; do
    wait "${agent_pid}" 2>/dev/null || true
done
