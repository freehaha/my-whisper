#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

APP_NAME="${APP_NAME:-MyWhisper}"
EXECUTABLE_NAME="${EXECUTABLE_NAME:-MyWhisper}"
CONFIGURATION="${CONFIGURATION:-release}"
BUNDLE_IDENTIFIER="${BUNDLE_IDENTIFIER:-com.freehaha.mywhisper}"
MARKETING_VERSION="${MARKETING_VERSION:-1.0.0}"
BUILD_VERSION="${BUILD_VERSION:-1}"
CODESIGN_IDENTITY="${CODESIGN_IDENTITY:--}"
APP_DIR="${APP_DIR:-$ROOT_DIR/dist/${APP_NAME}.app}"
MODULE_BUNDLE_NAME="${MODULE_BUNDLE_NAME:-${EXECUTABLE_NAME}_${APP_NAME}.bundle}"

printf 'Building %s (%s)\n' "$EXECUTABLE_NAME" "$CONFIGURATION"
swift build -c "$CONFIGURATION"
BIN_DIR="$(swift build -c "$CONFIGURATION" --show-bin-path)"

EXECUTABLE_PATH="$BIN_DIR/$EXECUTABLE_NAME"
FRAMEWORK_SRC="$BIN_DIR/whisper.framework"
MODULE_BUNDLE_SRC="$BIN_DIR/$MODULE_BUNDLE_NAME"
INFO_TEMPLATE="$ROOT_DIR/Packaging/Info.plist"

if [[ ! -x "$EXECUTABLE_PATH" ]]; then
    echo "error: executable not found at $EXECUTABLE_PATH" >&2
    exit 1
fi

if [[ ! -d "$FRAMEWORK_SRC" ]]; then
    echo "error: whisper.framework not found at $FRAMEWORK_SRC" >&2
    exit 1
fi

CONTENTS_DIR="$APP_DIR/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
FRAMEWORKS_DIR="$CONTENTS_DIR/Frameworks"
RESOURCES_DIR="$CONTENTS_DIR/Resources"
APP_EXECUTABLE="$MACOS_DIR/$EXECUTABLE_NAME"
APP_FRAMEWORK="$FRAMEWORKS_DIR/whisper.framework"
APP_MODULE_BUNDLE="$RESOURCES_DIR/$MODULE_BUNDLE_NAME"
INFO_PLIST_DEST="$CONTENTS_DIR/Info.plist"

rm -rf "$APP_DIR"
mkdir -p "$MACOS_DIR" "$FRAMEWORKS_DIR" "$RESOURCES_DIR"

cp "$EXECUTABLE_PATH" "$APP_EXECUTABLE"
ditto "$FRAMEWORK_SRC" "$APP_FRAMEWORK"

if [[ -d "$ROOT_DIR/Resources" ]]; then
    ditto "$ROOT_DIR/Resources" "$RESOURCES_DIR"
fi

if [[ -d "$MODULE_BUNDLE_SRC" ]]; then
    ditto "$MODULE_BUNDLE_SRC" "$APP_MODULE_BUNDLE"
fi

export APP_NAME EXECUTABLE_NAME BUNDLE_IDENTIFIER MARKETING_VERSION BUILD_VERSION INFO_PLIST_DEST INFO_TEMPLATE
python3 <<'PY'
from pathlib import Path
import os

content = Path(os.environ["INFO_TEMPLATE"]).read_text()
for key in [
    "APP_NAME",
    "EXECUTABLE_NAME",
    "BUNDLE_IDENTIFIER",
    "MARKETING_VERSION",
    "BUILD_VERSION",
]:
    content = content.replace(f"__{key}__", os.environ[key])
Path(os.environ["INFO_PLIST_DEST"]).write_text(content)
PY

if ! otool -l "$APP_EXECUTABLE" | grep -q "@executable_path/../Frameworks"; then
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$APP_EXECUTABLE"
fi

plutil -lint "$INFO_PLIST_DEST" >/dev/null
xattr -cr "$APP_DIR" || true
codesign --force --deep --sign "$CODESIGN_IDENTITY" --timestamp=none "$APP_DIR"

printf 'Bundled app: %s\n' "$APP_DIR"
printf 'Bundle ID: %s\n' "$BUNDLE_IDENTIFIER"
printf 'Code sign identity: %s\n' "$CODESIGN_IDENTITY"

if [[ "$CODESIGN_IDENTITY" == "-" ]]; then
    cat <<EOF

Note: this build is ad-hoc signed. Accessibility permission will only persist reliably
when the app is signed with a stable Apple signing identity and launched from a fixed path.
Re-run with CODESIGN_IDENTITY='Apple Development: Your Name (TEAMID)' or a Developer ID cert
once you have one available.
EOF
fi
