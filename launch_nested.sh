#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Ensure target executable exists
if [ ! -x "${SCRIPT_DIR}/vangoghflow_wayland" ]; then
    echo "vangoghflow_wayland executable not found. Building..."
    make -C "${SCRIPT_DIR}"
fi

COMPOSITOR=""
if command -v wayfire >/dev/null 2>&1; then
    COMPOSITOR="wayfire"
elif command -v labwc >/dev/null 2>&1; then
    COMPOSITOR="labwc"
elif command -v sway >/dev/null 2>&1; then
    COMPOSITOR="sway"
else
    echo "No Wayland compositor found! Please run ./setup_wayland.sh first."
    exit 1
fi

echo "Launching nested Wayland compositor: ${COMPOSITOR} inside X11..."

# Record existing sockets in runtime dir
EXISTING_SOCKETS=$(ls -1 "${XDG_RUNTIME_DIR}"/wayland-[0-9]* 2>/dev/null || true)

# Launch compositor in background with WAYLAND_DISPLAY unset
# Unsetting WAYLAND_DISPLAY ensures wlroots attaches to the existing X11 display (nested mode)
env -u WAYLAND_DISPLAY "${COMPOSITOR}" &
COMPOSITOR_PID=$!

HUD_PID=""
cleanup() {
    echo "Stopping nested compositor and HUD..."
    if [ -n "${HUD_PID}" ]; then
        kill "${HUD_PID}" 2>/dev/null || true
    fi
    kill "${COMPOSITOR_PID}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# Wait for and detect the Wayland socket created by this compositor
DETECTED_SOCKET=""
for i in {1..30}; do
    if ! kill -0 "${COMPOSITOR_PID}" 2>/dev/null; then
        echo "Error: ${COMPOSITOR} exited unexpectedly."
        exit 1
    fi

    # Check compositor's open file descriptors for the socket lock
    SOCK_FROM_FD=$(ls -l "/proc/${COMPOSITOR_PID}/fd" 2>/dev/null | grep -o 'wayland-[0-9]*' | head -n1 || true)
    if [ -n "${SOCK_FROM_FD}" ] && [ -S "${XDG_RUNTIME_DIR}/${SOCK_FROM_FD}" ]; then
        DETECTED_SOCKET="${SOCK_FROM_FD}"
        break
    fi

    # Fallback: check newly created sockets in XDG_RUNTIME_DIR
    for sock in "${XDG_RUNTIME_DIR}"/wayland-[0-9]*; do
        if [ -S "${sock}" ]; then
            sname="$(basename "${sock}")"
            if ! echo "${EXISTING_SOCKETS}" | grep -qw "${sname}"; then
                DETECTED_SOCKET="${sname}"
                break 2
            fi
        fi
    done

    sleep 0.2
done

if [ -z "${DETECTED_SOCKET}" ]; then
    echo "Timeout waiting for nested Wayland socket in ${XDG_RUNTIME_DIR}"
    exit 1
fi

export WAYLAND_DISPLAY="${DETECTED_SOCKET}"
echo "Wayland compositor ready on socket ${WAYLAND_DISPLAY}! Starting VanGoghFlow Wayland overlay..."

"${SCRIPT_DIR}/vangoghflow_wayland" "$@" &
HUD_PID=$!

# Wait for compositor to terminate
wait "${COMPOSITOR_PID}"
