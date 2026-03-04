#!/bin/bash

echo "Compiling MyWhisper..."

# Compile the swift source files into the MyWhisper executable inside the app bundle
swiftc -o MyWhisper.app/Contents/MacOS/MyWhisper MyWhisper/Sources/*.swift \
    -framework Cocoa \
    -framework SwiftUI \
    -framework AVFoundation \
    -framework Carbon

if [ $? -eq 0 ]; then
    echo "Successfully compiled MyWhisper.app!"
    echo "You can run it with: open MyWhisper.app"
else
    echo "Compilation failed."
    exit 1
fi
