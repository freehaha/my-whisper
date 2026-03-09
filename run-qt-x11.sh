#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_BIN="${ROOT_DIR}/MyWhisperQt/build/MyWhisperQt"

if [ ! -x "$APP_BIN" ]; then
    "${ROOT_DIR}/build-qt.sh"
fi

QT_QPA_PLATFORM=xcb "$APP_BIN" "$@"
