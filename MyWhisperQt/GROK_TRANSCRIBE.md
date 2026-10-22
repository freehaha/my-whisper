# Grok Voice Transcribe 2.0

This branch adds Grok transcription to the Qt app. It does not change the Swift app.

- Model: `grok-voice-transcribe-2.0`
- Endpoint: `https://api.x.ai/v1/stt`
- Authentication: `Authorization: Bearer <xAI key>`
- Mode: upload the completed WAV recording. This implementation does not stream.
- Language: automatic detection. The request does not force English.
- Timeout: 120 seconds without network activity.
- Credentials: the configured `grokApiKey` takes precedence over `XAI_API_KEY`.
- Privacy: the client does not log credentials, audio, transcripts, or response bodies.

```text
Record audio → Stop recording → Upload WAV → Read transcript → Existing refinement/paste flow
```

## Use the app

1. Start `MyWhisperQt/build/MyWhisperQt`.
2. Open Settings.
3. Select `Grok Voice Transcribe 2.0 (upload after recording)`.
4. Enter an xAI key, or launch the app with `XAI_API_KEY` set.
5. Save Settings.
6. Record a short sample with the existing hotkey.

The app shares `~/.config/my-whisper/config.json` with other checkouts. This branch does not change that file automatically.

## Build and run local tests

```bash
cmake -S MyWhisperQt -B MyWhisperQt/build -DCMAKE_BUILD_TYPE=Debug
cmake --build MyWhisperQt/build --parallel 4
/usr/bin/ctest --test-dir MyWhisperQt/build --output-on-failure
```

The current machine has a broken Python wrapper at `~/.local/bin/ctest`. `/usr/bin/ctest` works.

The test uses a local HTTP server. It verifies the actual Qt upload, including the Bearer header, model, WAV bytes, and field order.
The file field follows the model field, as the xAI API requires.

Nine HTTP cases cover English, Unicode, HTTP 401/429/500, malformed JSON, a missing text field, a non-string text field, and empty text.
Other checks cover configuration persistence, credential precedence, missing credentials, unreadable files, and disabling Deepgram streaming for Grok.
The test isolates its settings in a temporary home directory. It makes no external API calls by default.

## Optional live test

Set `XAI_API_KEY` in the shell without committing it. Then run:

```bash
MyWhisperQt/build/GrokTranscriberTest --live /absolute/path/to/sample.wav
```

This command uploads the specified audio to xAI and can incur API charges. It prints the transcript on success.
Use a short, non-sensitive sample. Compare the result with the spoken text.

## Validation status

The Qt app and test executable build successfully on Linux. All local tests pass.
No live transcription test ran because the session had no `XAI_API_KEY`.
The local tests do not measure transcription accuracy, API latency, microphone behavior, or paste behavior.

## References

- Announcement: https://x.ai/news/grok-voice-transcribe-2
- API guide: https://docs.x.ai/developers/model-capabilities/audio/speech-to-text
