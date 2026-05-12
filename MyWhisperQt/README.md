# MyWhisperQt

Qt 6 port of the `MyWhisper/` Swift menu bar app.

## Features

- Tray icon with History / Settings / Quit
- Global hotkeys on:
  - macOS via Carbon
  - Linux X11 via XGrabKey
- Audio recording via Qt Multimedia
- Selectable audio input device (system default or specific source)
- Selectable Deepgram upload transcription or AssemblyAI streaming transcription
- Deepgram keyterm biasing via Settings (one keyterm/phrase per line)
- Optional text refinement via OpenAI or local llama.cpp
- Floating status overlay
- Transcription history persisted to `~/.config/my-whisper/history.json`
- Shared config file at `~/.config/my-whisper/config.json`
- llama.cpp local refinement launches `llama-server` on a local port, queries via HTTP, and shuts down after 20 s idle

## Config

Config file: `~/.config/my-whisper/config.json`

Common keys:

- `transcriptionBackend`: `deepgram` or `assemblyai`
- `deepgramApiKey`: Deepgram API key
- `deepgramKeywords`: array of keyterms/phrases for Deepgram biasing
- `assemblyAiApiKey`: AssemblyAI API key
- `assemblyAiSpeechModel`: AssemblyAI streaming speech model (default `u3-rt-pro`)
- `enableRefinement`: enable post-transcription text refinement
- `refinementProvider`: `openai` or `llama_cpp`
- `refinementPrompt`: instruction sent to refinement backend
- `audioInputDeviceId`: optional selected audio device id; empty/null = system default
- `toggleHotkey`, `abortHotkey`, `historyHotkey`: hotkey objects with `keyCode` and `modifiers`
- `showDoneScreen`: show temporary Done overlay after success

OpenAI refinement keys:

- `openaiApiKey`: required when `refinementProvider` = `openai`

llama.cpp refinement keys:

- `llamaCppModelPath`: required when `refinementProvider` = `llama_cpp`; path to GGUF model
- `llamaCppBinaryPath`: optional explicit path to compiled `llama-server`; if empty, app searches `PATH`
- `llamaCppAdditionalArgs`: optional raw CLI args appended to llama.cpp invocation

Example:

```json
{
  "transcriptionBackend": "assemblyai",
  "deepgramApiKey": "YOUR_DEEPGRAM_API_KEY",
  "deepgramKeywords": ["AcmeCloud", "MyWhisper", "GPU"],
  "assemblyAiApiKey": "YOUR_ASSEMBLYAI_API_KEY",
  "assemblyAiSpeechModel": "u3-rt-pro",
  "openaiApiKey": null,
  "enableRefinement": true,
  "refinementProvider": "llama_cpp",
  "refinementPrompt": "Fix spelling and grammar. Return only the fixed text.",
  "llamaCppBinaryPath": "/home/you/llama.cpp/build/bin/llama-server",
  "llamaCppAdditionalArgs": "--ctx-size 4096 --threads 8 --gpu-layers 999",
  "llamaCppModelPath": "/home/you/models/model.gguf",
  "audioInputDeviceId": null,
  "toggleHotkey": { "keyCode": 15, "modifiers": 6144 },
  "abortHotkey": { "keyCode": 7, "modifiers": 6144 },
  "historyHotkey": { "keyCode": 4, "modifiers": 6144 },
  "showDoneScreen": false
}
```

Notes:

- `llamaCppAdditionalArgs` parsed with `QProcess::splitCommand()`.
- If same llama.cpp flag appears in both built-in args and `llamaCppAdditionalArgs`, llama.cpp decides which value wins.
- Settings UI currently exposes backend selection and model path; binary path and additional args are config-file-only.

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

To keep the old success overlay behavior, pass:

```bash
./run-qt-x11.sh --show-done-screen
```

To enable verbose debug logging, pass:

```bash
QT_QPA_PLATFORM=xcb ./build/MyWhisperQt --verbose
```

## Linux notes

- The Linux implementation targets **X11/xcb**, not Wayland.
- Global hotkeys use `XGrabKey`.
- On Linux X11, completed transcriptions are copied to both the regular clipboard and the PRIMARY selection instead of being auto-pasted, so terminal users can paste manually.
- By default, the success overlay is hidden immediately after transcription is ready. You can restore the temporary Done screen in the Settings window, or force it by launching with `--show-done-screen`.
- Audio cues use a bundled short ding sound instead of relying on the desktop/system beep.
- The Settings dialog includes an audio input dropdown backed by Qt Multimedia (PulseAudio/PipeWire on most Linux desktops), with a “System default” option.
- Deepgram keyterm hints can be configured in Settings and are sent as repeated `keyterm` query params on transcription requests for Nova-3.
- AssemblyAI streaming sends raw mono 16-bit PCM microphone chunks during recording, then finalizes with `Terminate` when recording stops.
- Default Linux hotkeys are `Ctrl+Alt+R`, `Ctrl+Alt+X`, and `Ctrl+Alt+H`.
- You need the Qt 6 development packages plus X11/XTest development libraries.

Example Debian/Ubuntu packages:

```bash
sudo apt install qt6-base-dev qt6-multimedia-dev libx11-dev libxtst-dev pkg-config
```

## Sound probe

Use bundled test app to isolate Qt audio path:

```bash
./MyWhisperQt/build/SoundProbe --list-devices
./MyWhisperQt/build/SoundProbe --engine platform --sound start
./MyWhisperQt/build/SoundProbe --engine platform --sound success
./MyWhisperQt/build/SoundProbe --engine mediaplayer
./MyWhisperQt/build/SoundProbe --engine tone
```

Useful options:

```bash
./MyWhisperQt/build/SoundProbe --engine tone --duration-ms 15000
./MyWhisperQt/build/SoundProbe --engine mediaplayer --device Bose
./MyWhisperQt/build/SoundProbe --engine soundeffect
```

While probe runs, verify PipeWire/Pulse stream creation:

```bash
wpctl status
pactl list short sink-inputs
```

## Notes

- If you run this under Wayland, use `QT_QPA_PLATFORM=xcb` from an X11 session/XWayland-capable environment.
- This avoids synthetic paste on Linux so terminals can use their normal manual paste flow (for example middle-click or Shift+Insert, depending on the terminal).
