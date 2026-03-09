# MyWhisperQt

Qt 6 port of the `MyWhisper/` Swift menu bar app.

## Features

- Tray icon with History / Settings / Quit
- Global hotkeys on:
  - macOS via Carbon
  - Linux X11 via XGrabKey
- Audio recording via Qt Multimedia
- Deepgram transcription
- Optional OpenAI text refinement
- Floating status overlay
- Transcription history persisted to `~/.config/my-whisper/history.json`
- Shared config file at `~/.config/my-whisper/config.json`

## Build

```bash
./build-qt.sh
```

Or from the Qt project directory:

```bash
cd MyWhisperQt
./build.sh
```

Or manually:

```bash
cmake -S MyWhisperQt -B build-mywhisper-qt -DCMAKE_BUILD_TYPE=Release
cmake --build build-mywhisper-qt --parallel
```

## Run on Linux X11

Build first, then run under the X11 Qt platform plugin:

```bash
QT_QPA_PLATFORM=xcb ./MyWhisperQt/build/MyWhisperQt
```

There is also a helper script:

```bash
./run-qt-x11.sh
```

## Linux notes

- The Linux implementation targets **X11/xcb**, not Wayland.
- Global hotkeys use `XGrabKey`.
- On Linux X11, completed transcriptions are copied to both the regular clipboard and the PRIMARY selection instead of being auto-pasted, so terminal users can paste manually.
- Default Linux hotkeys are `Ctrl+Alt+R`, `Ctrl+Alt+X`, and `Ctrl+Alt+H`.
- You need the Qt 6 development packages plus X11/XTest development libraries.

Example Debian/Ubuntu packages:

```bash
sudo apt install qt6-base-dev qt6-multimedia-dev libx11-dev libxtst-dev pkg-config
```

## Notes

- If you run this under Wayland, use `QT_QPA_PLATFORM=xcb` from an X11 session/XWayland-capable environment.
- This avoids synthetic paste on Linux so terminals can use their normal manual paste flow (for example middle-click or Shift+Insert, depending on the terminal).
