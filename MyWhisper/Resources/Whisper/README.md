# Bundled whisper.cpp model resources

MyWhisper now transcribes locally with `whisper.cpp` via released XCFramework.

## What to bundle

Place a Whisper model file somewhere under:

- `Resources/Whisper/`
- recommended: `Resources/Whisper/models/ggml-base.en.bin`

Expected model filename pattern:

- `ggml-*.bin`

## Model lookup order

If `whisperModelPath` is set in `~/.config/my-whisper/config.json`, MyWhisper uses that file.

If `whisperModelPath` is empty, MyWhisper searches bundled resources recursively and prefers:

- `ggml-base.en.bin`
- `ggml-base.bin`
- `ggml-small.en.bin`
- `ggml-small.bin`
- `ggml-medium.en.bin`
- `ggml-medium.bin`
- `ggml-large-v3.bin`
- `ggml-large-v3-turbo.bin`

If none match preferred names, app uses first bundled `ggml-*.bin` file it finds.

## Notes

- Inference runs in-process through `whisper.framework`; no `whisper-cli` binary needed.
- Recorder writes 16 kHz mono PCM WAV, which is fed directly to `whisper.cpp`.
- Optional vocabulary hints from Settings are passed as `initial_prompt`.

## Licensing

- This project uses the MIT License. See `/LICENSE`.
- The portion that uses `whisper.cpp` is based on the `whisper.cpp` library, which is also licensed under the MIT License.
- See `/THIRD_PARTY_NOTICES.md` for a short third-party notice.
