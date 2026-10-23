# Grok upload versus streaming: 2026-10-22

## Conclusion

The current Qt upload client beat the current Qt streaming client on this sample.
The streaming pacer preserves the connection startup delay as an audio backlog.
A benchmark prototype with bounded catch-up removed that backlog and beat upload.
Opus reduced audio size substantially. It did not reduce latency compared with PCM after catch-up.
The initial benchmark used the original app pacer. The follow-up below validates the pacing fix in the Qt app.

## Sample and method

- Source: `/mnt/work/my-whisper/20261022-ltm.ogg`.
- Selected interval: approximately 01:00–01:20.
- Decoded sample: 19.992 seconds, mono PCM16, 16 kHz, 639,744 PCM bytes.
- Model: `grok-voice-transcribe-2.0`.
- Credentials: existing `grokApiKey` config value. The benchmark does not print the key.
- Three trials per mode. The benchmark randomized the order within each round.
- All 33 requests completed successfully with nonempty transcripts.
- Each trial opened a fresh connection.
- The benchmark omitted language bias, formatting, refinement, and paste operations.
- Upload latency starts when the complete sample is available, immediately before the request.
- Streaming latency starts at simulated recording stop, not at recording start.
- Python streaming includes connection startup and the resulting queue drain.
- Qt streaming uses the actual client and its existing file feeder.
- Opus uses 20 ms packets, one packet per WebSocket frame. Other encodings use 100 ms chunks.
- Audio sizes exclude TLS, HTTP, WebSocket framing, and control messages.
- Upload timing excludes conversion. Measured conversion times include ffmpeg startup.

The interval contains pauses and short meeting exchanges. It is not a continuous 20-second monologue.
These results apply to this sample, host, network, and service state. Three trials cannot establish a general codec ranking.

## Follow-up: pacing fix in the Qt app

The app now polls every 50 ms with a precise timer. Each tick sends at most one captured 100 ms PCM chunk.
This permits bounded 2× catch-up without sending uncaptured audio or dumping the entire startup buffer.
If all audio has already reached the socket at recording stop, the app sends `audio.done` immediately.

Three live runs with the same meeting sample returned results after **272 ms, 242 ms, and 516 ms**.
The median was **272 ms**, compared with **1,317 ms** before the fix.
This is approximately a 79% reduction in the median wait after stop for this sample.

The local regression test delays readiness by 450 ms while capture continues for two seconds.
It checks that completion follows stop within 175 ms on the local server, instead of preserving the startup backlog.
It also verifies exact PCM bytes, capture bounds, bounded catch-up, and single completion.
Both test suites pass. The streaming suite also passed five repeated runs.

## Actual Qt clients before the pacing fix

| Mode | Median wait after stop | Range | Audio bytes |
|---|---:|---:|---:|
| Current upload, WAV | 906 ms | 538–1,096 ms | 639,822 |
| Current streaming, PCM | 1,317 ms | 1,164–1,856 ms | 639,744 |

The current streaming median was 411 ms slower, or about 45% longer.
Upload includes a small probe process startup cost. Streaming reports the client's internal completion timing.

## Encoding comparison through Python clients

| Mode | Encoding | Median wait after stop | Range | Audio size |
|---|---|---:|---:|---:|
| Upload | WAV / PCM16 | 1,223 ms | 577–1,454 ms | 624.8 KiB |
| Upload | FLAC, lossless | 1,317 ms | 673–1,422 ms | 189.6 KiB |
| Upload | Ogg Opus | 752 ms | 473–767 ms | 40.3 KiB |
| Streaming, original pacing | PCM16 | 1,080 ms | 793–1,824 ms | 624.8 KiB |
| Streaming, original pacing | μ-law | 1,599 ms | 785–1,827 ms | 312.4 KiB |
| Streaming, original pacing | A-law | 1,065 ms | 796–1,869 ms | 312.4 KiB |
| Streaming, original pacing | Raw Opus packets | 813 ms | 810–1,798 ms | 38.6 KiB |

FLAC reduced upload size by about 70%, but did not improve the measured median.
Opus reduced streaming audio size by 93.8%, approximately a 16.2× reduction.
The test requested Opus at 24 kbit/s with variable bitrate. The actual rate was lower for this sample.
Opus used 1,000 frames with an average payload of 39.6 bytes.
PCM used 200 frames with an average payload of 3,198.7 bytes.
Opus therefore sends smaller packets more frequently in this configuration.

Measured conversion times for the complete sample:

| Encoding | Conversion time |
|---|---:|
| FLAC | 26 ms |
| Opus | 113 ms |
| μ-law | 23 ms |
| A-law | 22 ms |

These times include ffmpeg process startup. A live encoder would spread its work across recording time.
Do not interpret the codec medians as isolated codec costs. Connection readiness varied substantially between trials.

## Why current streaming feels slower

The client waits for `transcript.created`, then starts a timer that sends 100 ms of audio every 100 ms.
Audio continues to arrive during the readiness wait. The equal send and capture rates never eliminate the initial backlog.
When recording stops, the client must drain that backlog before sending `audio.done`.

```text
Capture starts → Connection becomes ready → Equal-rate sending preserves backlog → Capture stops → Backlog drains → audio.done → Result
```

For the Python PCM trials:

- Median server readiness: 839 ms after capture starts.
- Median queue drain after capture stops: 839 ms.
- Median server completion after `audio.done`: 241 ms.

These measurements identify the queue as the main avoidable delay in this implementation.

## Bounded catch-up experiment

The prototype sends at up to twice real-time speed while a startup backlog exists.
It never sends audio before its simulated capture time.
Once it catches up, it sends at real-time speed.
It still waits for server readiness before sending audio.

```text
Capture starts → Connection becomes ready → Bounded 2× sending drains backlog → Real-time sending → Capture stops → audio.done → Result
```

| Prototype mode | Median wait after stop | Range | Audio size |
|---|---:|---:|---:|
| PCM16 with catch-up | 237 ms | 234–255 ms | 624.8 KiB |
| Opus with catch-up | 247 ms | 237–247 ms | 38.6 KiB |

Median queue drain fell to less than 1 ms for PCM.
Opus added approximately 8 ms of encoded packet padding to the sample duration.
The result remained fast even when server readiness took approximately 1.55 seconds for Opus.
The latency difference between these codecs was negligible compared with the original pacing delay.
The follow-up above integrates the bounded catch-up strategy and verifies it through the actual Qt client.

## Transcript quality caveat

The sample has no supplied reference transcript.
The requests returned different text for one short phrase across codecs and endpoints.
PCM/FLAC upload returned “I'm actually located.” Opus upload returned “I'm happy to locate you.”
Streaming sometimes returned “how next to locate more?” for that phrase.
These differences show that a smaller payload does not guarantee equivalent recognition.
No word error rate or accuracy ranking is valid without checking the recording against a reference.

## Recommendation

Keep upload available. The streaming backlog fix now precedes any encoder changes in the app.
The PCM benchmark prototype achieved approximately 0.24 seconds after stop. The fixed Qt client achieved a 0.27-second median.
Consider Opus next if bandwidth matters. Validate recognition quality before choosing it as the default.
FLAC offers lossless upload compression, but this sample did not establish a latency advantage.

## Reproduction

Sample files and complete per-request results, including transcripts:

- `/tmp/mywhisper-grok-benchmark/sample.wav`
- `/tmp/mywhisper-grok-benchmark/results.json`
- `/tmp/mywhisper-grok-benchmark/catchup-results.json`

The source recording and sample files remain outside Git commits.
The repeatable benchmark is `MyWhisperQt/tests/benchmark_grok.py`.
It requires `requests`, `websocket-client`, and ffmpeg.
Running it makes billable xAI requests and uploads the selected sample.

```bash
python3 MyWhisperQt/tests/benchmark_grok.py \
  /tmp/mywhisper-grok-benchmark/sample.wav \
  --output /tmp/mywhisper-grok-benchmark/results.json \
  --rounds 3 \
  --qt-build /mnt/work/my-whisper-grok/MyWhisperQt/build

python3 MyWhisperQt/tests/benchmark_grok.py \
  /tmp/mywhisper-grok-benchmark/sample.wav \
  --output /tmp/mywhisper-grok-benchmark/catchup-results.json \
  --rounds 3 \
  --modes stream-pcm-catchup stream-opus-catchup
```

API guide: https://docs.x.ai/developers/model-capabilities/audio/speech-to-text
