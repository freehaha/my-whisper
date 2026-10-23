# Grok Voice Transcribe 2.0

This branch adds Grok transcription to the Qt app. It does not change the Swift app.

- Model: `grok-voice-transcribe-2.0`
- Endpoint: `https://api.x.ai/v1/stt`
- Authentication: `Authorization: Bearer <xAI key>`
- Modes: upload the completed WAV recording, or stream PCM during recording.
- Streaming endpoint: `wss://api.x.ai/v1/stt`.
- Streaming audio: mono PCM16 at the microphone sample rate, in 100 ms chunks.
- Streaming completion: send `audio.done` after all PCM bytes, then wait for `transcript.done`.
- Transcript assembly: retain finalized chunks by utterance timing. Replace chunks with each stitched utterance final.
- An empty `transcript.done` is valid when the server already delivered finalized text.
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

## Try streaming

1. Restart the app with `MyWhisperQt/build/MyWhisperQt`.
2. Open Settings.
3. Select `Grok Voice Transcribe 2.0 (streaming)`.
4. Save Settings.
5. Record a short sample with the existing hotkey.

The streaming mode uses the same xAI key as the upload mode. The upload mode remains available.
The app does not paste interim results. It retains finalized streaming events and waits for completion after recording stops.

```text
Start recording → Connect → Wait for transcript.created → Send paced PCM → Send audio.done → Read transcript.done → Refine/paste
```

The client buffers audio while the server starts. It also handles recording stops before server readiness.
A precise 50 ms timer sends at most one captured 100 ms chunk per tick.
This permits bounded 2× catch-up while buffered audio exists. Sending returns to real time when the buffer drains.
If the buffer is already empty when recording stops, the client sends `audio.done` without another timer tick.
The startup and network queues each have a 30-second audio limit.
The server readiness timeout is 15 seconds. The completion timeout is 45 seconds, including queued audio.
Unsupported microphone formats produce an error. The client does not silently switch to upload.
A missing final result produces an error instead of incomplete pasted text.

The implementation reuses the existing WebSocket transport. The transport now accepts a provider URL and authorization header.
Deepgram retains its endpoint and `Token` authorization. Grok uses `Bearer` authorization.

## Validation status

The upload version is committed as `c8ca214`. The operator reported that live upload transcription works well and is fast.
The Qt app and both test executables build successfully on Linux. Both local test suites pass.

The streaming test uses a local WebSocket server. It checks authentication, query parameters, masking, readiness, and exact PCM bytes.
It also checks early stops, single completion, duplicate prevention, cancellation, malformed messages, server errors, unexpected disconnects, empty results, format validation, and queue limits.
A live test streamed `rec-test.wav` through the Qt client with `grokApiKey` from the existing config.
The result matched this reference exactly:

> And this is a recording for testing. I'm wondering whether the transcription service can do its job.

Before the pacing fix, the result arrived 1,067 ms after the file finished streaming in that run.
This measurement excludes microphone capture and paste behavior. Latency varies between requests.

### Pacing validation

A regression test delays readiness by 450 ms while simulated microphone capture continues for two seconds.
It verifies that the startup backlog drains during capture, rather than after capture stops.
It also checks exact audio bytes, bounded catch-up, no uncaptured audio, and single completion.
The streaming suite passed five repeated runs after the pacing fix.

Three live Qt runs with the 20-second meeting sample returned results after 272 ms, 242 ms, and 516 ms.
The median fell from 1,317 ms before the fix to 272 ms after the fix.
These measurements exclude refinement and paste operations. They use PCM without compression.
The full benchmark report is `benchmarks/grok-latency-20261022.md`.

### Empty completion regression

The live service emitted finalized chunks, repeated each complete utterance in a `speech_final` event, and ended with empty `transcript.done.text`.
The original implementation discarded those events because it expected the entire transcript in `transcript.done`.
The fix groups finalized chunks by utterance start and chunk duration. Each stitched utterance replaces its chunks.
The client ignores interim text and retains genuine repetitions with different timings.
A nonempty `transcript.done.text` still takes precedence, matching the documented API behavior.
Regression tests cover the observed event sequence, duplicate finals, chunk-only completion, repeated phrases, and interim-only failure.

### Repeat the live verification

```bash
MyWhisperQt/build/GrokStreamingProbe rec-test.wav "And this is a recording for testing. I'm wondering whether the transcription service can do its job."
```

The probe reads `grokApiKey` from config, with `XAI_API_KEY` as the fallback.
It streams PCM16 WAV audio in real time through the same client as the app.
It prints the transcript and completion latency. An optional reference argument enables an exact comparison.
The probe makes a billable external request only when you run it. CTest does not run it.

## References

- Announcement: https://x.ai/news/grok-voice-transcribe-2
- API guide: https://docs.x.ai/developers/model-capabilities/audio/speech-to-text
