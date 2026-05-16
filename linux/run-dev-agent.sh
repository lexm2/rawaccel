#!/usr/bin/env bash
# Dev launcher for rawaccel-agentd. Builds if needed, runs the daemon under
# sudo against a per-user socket in $XDG_RUNTIME_DIR, and chowns the socket
# back to the calling user so the GUI (running unprivileged) can connect.
# Not for production install; for that use `cmake --install` + systemctl.

set -euo pipefail

LINUX_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${LINUX_DIR}/build"
AGENT="${BUILD_DIR}/rawaccel-agentd"
SHIM="${BUILD_DIR}/librawaccel_common.so"

SOCKET_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
SOCKET="${SOCKET_DIR}/rawaccel.sock"
BACKEND="${1:-evdev}"
UID_NUM="$(id -u)"
GID_NUM="$(id -g)"

if [[ ! -x "${AGENT}" || ! -f "${SHIM}" ]]; then
    echo "Building rawaccel-agentd and the curve shim..."
    cmake -S "${LINUX_DIR}" -B "${BUILD_DIR}" >/dev/null
    cmake --build "${BUILD_DIR}" --target rawaccel-agentd rawaccel_common_shim
fi

if [[ ! -d "${SOCKET_DIR}" ]]; then
    echo "error: ${SOCKET_DIR} does not exist; XDG_RUNTIME_DIR is unset and no /run/user/${UID_NUM}" >&2
    exit 1
fi

cleanup() {
    if [[ -n "${chown_pid:-}" ]]; then
        kill "${chown_pid}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

(
    for _ in {1..50}; do
        if [[ -S "${SOCKET}" ]]; then
            sudo chown "${UID_NUM}:${GID_NUM}" "${SOCKET}"
            echo "[run-dev-agent] socket ready at ${SOCKET} (chowned to $(id -un):$(id -gn))"
            break
        fi
        sleep 0.1
    done
) &
chown_pid=$!

cat <<EOF
[run-dev-agent] agent backend : ${BACKEND}
[run-dev-agent] socket path   : ${SOCKET}
[run-dev-agent] shim path     : ${SHIM}

For the GUI in another terminal:
    RAWACCEL_SOCKET=${SOCKET} \\
    LD_LIBRARY_PATH=${BUILD_DIR} \\
    dotnet run --project userinterface

EOF

exec sudo "${AGENT}" --backend "${BACKEND}" --socket "${SOCKET}"
