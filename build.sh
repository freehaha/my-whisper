#!/bin/bash
set -e

APP_BUNDLE="MyWhisper.app"
CONTENTS_DIR="$APP_BUNDLE/Contents"
MACOS_DIR="$CONTENTS_DIR/MacOS"
RESOURCES_DIR="$CONTENTS_DIR/Resources"
PLIST_PATH="$CONTENTS_DIR/Info.plist"

if [ ! -d "$APP_BUNDLE" ]; then
    echo "MyWhisper.app not found. Creating app bundle..."
fi

mkdir -p "$MACOS_DIR" "$RESOURCES_DIR"

if [ ! -f "$PLIST_PATH" ]; then
    echo "Creating default Info.plist..."
    cat > "$PLIST_PATH" <<'EOF'
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
if ! /usr/libexec/PlistBuddy -c "Print :NSMicrophoneUsageDescription" "$PLIST_PATH" >/dev/null 2>&1; then
    /usr/libexec/PlistBuddy -c "Add :NSMicrophoneUsageDescription string MyWhisper needs microphone access to record audio for transcription." "$PLIST_PATH"
fi

echo "Compiling MyWhisper..."

swiftc -o "$MACOS_DIR/MyWhisper" MyWhisper/Sources/*.swift \
    -framework Cocoa \
    -framework SwiftUI \
    -framework AVFoundation \
    -framework Carbon

echo "Successfully compiled MyWhisper.app!"
echo "You can run it with: open MyWhisper.app"
