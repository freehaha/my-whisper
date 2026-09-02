#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_BIN="${ROOT_DIR}/MyWhisperQt/build/MyWhisperQt"

if [ ! -x "$APP_BIN" ]; then
    "${ROOT_DIR}/build-qt.sh"
fi

# Disable core dumps.
ulimit -c 0

# Restart app automatically if it crashes/exits with non-zero status.
while true; do
    if QT_QPA_PLATFORM=xcb "$APP_BIN" "$@"; then
        exit 0
    fi

    status=$?
    echo "MyWhisperQt exited with status ${status}; restarting in 1s..." >&2
    sleep 1
done
