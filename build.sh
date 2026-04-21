#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
PACKAGE_DIR="${ROOT_DIR}/MyWhisper"
APP_BUNDLE="${ROOT_DIR}/MyWhisper.app"
CONTENTS_DIR="${APP_BUNDLE}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"
PLIST_PATH="${CONTENTS_DIR}/Info.plist"

if [ ! -d "${PACKAGE_DIR}" ] || [ ! -f "${PACKAGE_DIR}/Package.swift" ]; then
    echo "Swift package not found at ${PACKAGE_DIR}" >&2
    exit 1
fi

if [ ! -d "${APP_BUNDLE}" ]; then
    echo "MyWhisper.app not found. Creating app bundle..."
fi

mkdir -p "${MACOS_DIR}" "${RESOURCES_DIR}"

if [ ! -f "${PLIST_PATH}" ]; then
    echo "Creating default Info.plist..."
    cat > "${PLIST_PATH}" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>MyWhisper</string>
    <key>CFBundleDisplayName</key>
    <string>MyWhisper</string>
    <key>CFBundleIdentifier</key>
    <string>com.mywhisper.app</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleExecutable</key>
    <string>MyWhisper</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>LSMinimumSystemVersion</key>
    <string>14.0</string>
</dict>
</plist>
EOF
fi

# Ensure required privacy keys exist (macOS TCC will crash without these)
if ! /usr/libexec/PlistBuddy -c "Print :NSMicrophoneUsageDescription" "${PLIST_PATH}" >/dev/null 2>&1; then
    /usr/libexec/PlistBuddy -c "Add :NSMicrophoneUsageDescription string MyWhisper needs microphone access to record audio for transcription." "${PLIST_PATH}"
fi

echo "Building MyWhisper Swift package..."
swift build --package-path "${PACKAGE_DIR}" -c release

BIN_DIR="$(swift build --package-path "${PACKAGE_DIR}" -c release --show-bin-path)"
BINARY_PATH="${BIN_DIR}/MyWhisper"
FRAMEWORK_PATH="${BIN_DIR}/whisper.framework"
RESOURCE_BUNDLE_PATH="${BIN_DIR}/MyWhisper_MyWhisper.bundle"

if [ ! -f "${BINARY_PATH}" ]; then
    echo "Built executable not found at ${BINARY_PATH}" >&2
    exit 1
fi

rm -f "${MACOS_DIR}/MyWhisper"
cp "${BINARY_PATH}" "${MACOS_DIR}/MyWhisper"
chmod +x "${MACOS_DIR}/MyWhisper"

if [ -d "${FRAMEWORK_PATH}" ]; then
    rm -rf "${MACOS_DIR}/whisper.framework"
    cp -R "${FRAMEWORK_PATH}" "${MACOS_DIR}/whisper.framework"
fi

# SwiftPM executable resources are looked up relative to Bundle.main.bundleURL,
# so the generated .bundle needs to live at the app bundle root.
if [ -d "${RESOURCE_BUNDLE_PATH}" ]; then
    rm -rf "${APP_BUNDLE}/MyWhisper_MyWhisper.bundle"
    cp -R "${RESOURCE_BUNDLE_PATH}" "${APP_BUNDLE}/MyWhisper_MyWhisper.bundle"
fi

echo "Successfully built ${APP_BUNDLE}"
echo "You can run it with: open ${APP_BUNDLE}"
