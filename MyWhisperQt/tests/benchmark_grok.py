#!/usr/bin/env python3
"""Billable, opt-in live benchmark. Requires requests, websocket-client, and ffmpeg.

Input: a mono PCM16 16 kHz WAV. Outputs transcripts and timings, never credentials.
Streaming starts a fresh connection at simulated recording start and sends paced audio.
The server-readiness delay remains in the sender schedule, like the Qt pacer.
"""
import argparse
import json
import os
from pathlib import Path
import random
import re
import statistics
import subprocess
import threading
import time
import urllib.parse
import wave

import requests
import websocket


def opus_packets(path):
    data = path.read_bytes()
    offset, pending, packets = 0, bytearray(), []
    while offset < len(data):
        assert data[offset:offset + 4] == b'OggS'
        count = data[offset + 26]
        sizes = data[offset + 27:offset + 27 + count]
        pos = offset + 27 + count
        for size in sizes:
            pending.extend(data[pos:pos + size])
            pos += size
            if size < 255:
                packet = bytes(pending)
                pending.clear()
                if not packet.startswith((b'OpusHead', b'OpusTags')):
                    packets.append(packet)
        offset = pos
    assert not pending
    # ffmpeg uses exactly 20 ms frames in this benchmark.
    return [(p, .02) for p in packets]


def prepare(wav, directory):
    directory.mkdir(parents=True, exist_ok=True)
    result = {'wav': wav}
    times = {}
    for name, flags in {
        'flac': ['-c:a', 'flac'],
        'opus': ['-c:a', 'libopus', '-b:a', '24k', '-application', 'voip', '-frame_duration', '20'],
        'mulaw': ['-f', 'mulaw', '-c:a', 'pcm_mulaw'],
        'alaw': ['-f', 'alaw', '-c:a', 'pcm_alaw'],
    }.items():
        output = directory / ('sample.' + name)
        start = time.monotonic()
        subprocess.run(['ffmpeg', '-y', '-hide_banner', '-loglevel', 'error', '-i', str(wav), *flags, str(output)], check=True)
        times[name] = round((time.monotonic() - start) * 1000, 1)
        result[name] = output
    return result, times


def upload(path, key):
    mime = {'wav': 'audio/wav', 'flac': 'audio/flac', 'opus': 'audio/ogg'}[path.suffix[1:]]
    start = time.monotonic()
    with path.open('rb') as audio:
        response = requests.post('https://api.x.ai/v1/stt', headers={'Authorization': 'Bearer ' + key},
                                 data={'model': 'grok-voice-transcribe-2.0'},
                                 files={'file': (path.name, audio, mime)}, timeout=120)
    elapsed = time.monotonic() - start
    # Avoid printing arbitrary error bodies or request headers.
    if response.status_code != 200:
        raise RuntimeError('Upload HTTP ' + str(response.status_code))
    return {'after_stop_ms': round(elapsed * 1000, 1), 'audio_bytes': path.stat().st_size,
            'transcript': response.json()['text']}


def stream(encoding, packets, key, duration, catchup=False):
    query = {'model': 'grok-voice-transcribe-2.0', 'encoding': encoding, 'channels': '1'}
    if encoding != 'opus':
        query['sample_rate'] = '16000'
    start = time.monotonic()
    ws = websocket.create_connection('wss://api.x.ai/v1/stt?' + urllib.parse.urlencode(query),
                                     header=['Authorization: Bearer ' + key], timeout=60)
    first = json.loads(ws.recv())
    if first.get('type') != 'transcript.created':
        ws.close()
        raise RuntimeError('Missing transcript.created')
    ready = time.monotonic()
    sent_done, first_final, done = None, None, None
    utterances, sender_errors = {}, []
    audio_bytes = sum(len(packet) for packet, _ in packets)
    def send():
        nonlocal sent_done
        try:
            target = ready
            produced_seconds = 0
            for packet, seconds in packets:
                produced_seconds += seconds
                # Optional bounded 2x catch-up. Never send audio before its simulated capture.
                target = max(start + produced_seconds, target + seconds * (.5 if catchup else 1))
                time.sleep(max(0, target - time.monotonic()))
                ws.send_binary(packet)
            ws.send(json.dumps({'type': 'audio.done'}))
            sent_done = time.monotonic()
        except Exception as error:
            sender_errors.append(type(error).__name__)
    sender = threading.Thread(target=send)
    sender.start()
    try:
        while True:
            raw = ws.recv()
            if not raw:
                raise RuntimeError('Closed without transcript.done')
            event = json.loads(raw)
            kind = event.get('type')
            text = event.get('text', '').strip()
            if kind == 'transcript.partial' and event.get('is_final') and text:
                first_final = first_final or time.monotonic()
                part = utterances.setdefault(event['start'], {'chunks': {}, 'text': None})
                if event.get('speech_final'):
                    part['text'] = text
                    part['chunks'].clear()
                elif part['text'] is None:
                    part['chunks'][event['duration']] = text
            elif kind == 'transcript.done':
                done = time.monotonic()
                transcript = text or ' '.join(
                    p['text'] if p['text'] is not None else ' '.join(p['chunks'][k] for k in sorted(p['chunks']))
                    for _, p in sorted(utterances.items()))
                break
            elif kind == 'error':
                raise RuntimeError('Streaming server error')
    finally:
        sender.join(timeout=65)
        ws.close()
    if sender_errors or sent_done is None:
        raise RuntimeError('Streaming sender failed')
    return {'after_stop_ms': round((done - start - duration) * 1000, 1),
            'ready_ms': round((ready - start) * 1000, 1),
            'after_audio_done_ms': round((done - sent_done) * 1000, 1),
            'drain_after_stop_ms': round((sent_done - start - duration) * 1000, 1),
            'first_final_from_start_ms': round((first_final - start) * 1000, 1) if first_final else None,
            'audio_bytes': audio_bytes, 'frames': len(packets), 'transcript': transcript}


def qt(mode, wav, build, key):
    env = dict(os.environ, XAI_API_KEY=key)
    command = [str(build / 'GrokStreamingProbe'), str(wav)] if mode == 'stream' else [str(build / 'GrokTranscriberTest'), '--live', str(wav)]
    start = time.monotonic()
    result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=120)
    if result.returncode:
        raise RuntimeError('Qt probe failed: ' + result.stderr.strip())
    if mode == 'stream':
        transcript = re.search(r'^Transcript: (.*)$', result.stdout, re.M).group(1)
        ms = float(re.search(r'Result after stop: (\d+) ms', result.stdout).group(1))
    else:
        transcript = result.stdout.strip()
        ms = (time.monotonic() - start) * 1000
    return {'after_stop_ms': round(ms, 1), 'audio_bytes': wav.stat().st_size if mode == 'upload' else wav.stat().st_size - 78,
            'transcript': transcript}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('wav', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--qt-build', type=Path)
    parser.add_argument('--modes', nargs='+', help='Override the mode list, e.g. stream-pcm-catchup stream-opus-catchup')
    args = parser.parse_args()
    config = json.loads((Path.home() / '.config/my-whisper/config.json').read_text())
    key = config.get('grokApiKey', '').strip() or os.environ.get('XAI_API_KEY', '').strip()
    if not key:
        raise SystemExit('Missing grokApiKey / XAI_API_KEY')
    with wave.open(str(args.wav)) as wav:
        assert wav.getnchannels() == 1 and wav.getsampwidth() == 2 and wav.getframerate() == 16000
        pcm = wav.readframes(wav.getnframes())
        duration = wav.getnframes() / wav.getframerate()
    files, preparation = prepare(args.wav, args.output.parent / 'encodings')
    packets = {'pcm': [(pcm[i:i+3200], len(pcm[i:i+3200])/32000) for i in range(0, len(pcm), 3200)],
               'opus': opus_packets(files['opus'])}
    for encoding in ('mulaw', 'alaw'):
        raw = files[encoding].read_bytes()
        packets[encoding] = [(raw[i:i+1600], len(raw[i:i+1600])/16000) for i in range(0, len(raw), 1600)]
    modes = ['upload-wav', 'upload-flac', 'upload-opus', 'stream-pcm', 'stream-mulaw', 'stream-alaw', 'stream-opus']
    if args.qt_build:
        modes += ['qt-upload-wav', 'qt-stream-pcm']
    if args.modes:
        modes = args.modes
    report = {'sample_seconds': duration, 'sample_path': str(args.wav), 'preparation_ms': preparation,
              'notes': ['Fresh connections per trial. No language or formatting bias.',
                        'Upload timing starts after recording ends. Encoding time excluded and listed separately.',
                        'Python streaming includes readiness plus paced queue drain after simulated recording stop.',
                        'Qt streaming reports its measured wait after the PCM feeder stops.',
                        'Audio bytes exclude TLS, HTTP, WebSocket framing, and control messages.'], 'runs': []}
    randomizer = random.Random(20261022)
    for round_number in range(1, args.rounds + 1):
        order = modes.copy()
        randomizer.shuffle(order)
        for mode in order:
            print(f'Round {round_number}: {mode}', flush=True)
            row = {'round': round_number, 'mode': mode}
            try:
                if mode.startswith('qt-'):
                    row.update(qt(mode.split('-')[1], args.wav, args.qt_build, key))
                    row['audio_bytes'] = len(pcm) if 'stream' in mode else args.wav.stat().st_size
                elif mode.startswith('upload-'):
                    row.update(upload(files[mode.split('-')[1]], key))
                else:
                    encoding = mode.split('-')[1]
                    row.update(stream(encoding, packets[encoding], key, duration, catchup=mode.endswith('-catchup')))
                print(f"  {row['after_stop_ms']:.1f} ms after stop; {row['audio_bytes']} audio bytes", flush=True)
            except Exception as error:
                row['error'] = str(error)
                print('  ERROR: ' + str(error), flush=True)
            report['runs'].append(row)
            args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2))
    summary = {}
    for mode in modes:
        rows = [r for r in report['runs'] if r['mode'] == mode and 'after_stop_ms' in r]
        values = [r['after_stop_ms'] for r in rows]
        if values:
            summary[mode] = {'median_after_stop_ms': statistics.median(values), 'min_ms': min(values), 'max_ms': max(values),
                             'audio_bytes': rows[0]['audio_bytes'], 'successful_runs': len(rows)}
    report['summary'] = summary
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2))
    print(json.dumps(summary, indent=2), flush=True)


if __name__ == '__main__':
    main()
