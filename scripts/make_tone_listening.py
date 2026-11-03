"""Render dark/neutral/bright tone comparisons from a WAV and RMS-match them."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

def read_wav(path):
    data = path.read_bytes()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        raise ValueError('expected RIFF WAV')
    offset = 12
    fmt = audio = None
    while offset+8 <= len(data):
        kind, size = struct.unpack_from('<4sI', data, offset)
        chunk = data[offset+8:offset+8+size]
        if len(chunk) != size:
            raise ValueError('truncated WAV')
        if kind == b'fmt ':
            fmt = chunk
        if kind == b'data':
            audio = chunk
        offset += 8+size+(size % 2)
    if fmt is None or audio is None:
        raise ValueError('missing WAV chunks')
    code, channels, rate, _, align, bits = struct.unpack_from('<HHIIHH', fmt)
    if code == 65534 and len(fmt) >= 40:
        code = struct.unpack_from('<H', fmt, 24)[0]
    if channels not in (1, 2) or not 8000 <= rate <= 384000 or align != channels*(bits//8):
        raise ValueError('unsupported WAV layout')
    if len(audio) % align:
        raise ValueError('partial frame')
    if code == 3 and bits == 32:
        values = list(struct.unpack('<'+'f'*(len(audio)//4), audio))
    elif code == 1 and bits in (16, 24, 32):
        width = bits//8
        values = [int.from_bytes(audio[n:n+width], 'little', signed=True)/2**(bits-1)
                  for n in range(0, len(audio), width)]
    else:
        raise ValueError('supported WAV: PCM16/24/32 or float32')
    if not values or not all(math.isfinite(v) for v in values):
        raise ValueError('empty or nonfinite WAV')
    return rate, channels, values

def write_wav(path, rate, channels, samples):
    audio = struct.pack('<'+'f'*len(samples), *samples)
    fmt = struct.pack('<HHIIHH', 3, channels, rate, rate*channels*4, channels*4, 32)
    body = b'WAVEfmt '+struct.pack('<I', 16)+fmt+b'data'+struct.pack('<I', len(audio))+audio
    path.write_bytes(b'RIFF'+struct.pack('<I', len(body))+body)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--renderer', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('output must be new')
    rate, channels, source = read_wav(args.source)
    frames = len(source)//channels
    if frames < rate//10:
        raise ValueError('source must contain at least 100 ms')
    args.output.mkdir(parents=True)
    template_path = Path(__file__).parents[1]/'examples/experiments/dispersion-impulse.json'
    template = json.loads(template_path.read_text())
    for key in ('dispersionFrequencyHz', 'dispersionMix'):
        template.pop(key)
    template.update(version=14, targetId='tone', sampleRate=rate, channels=channels,
                    renderDurationSamples=frames, filePath=str(args.source.resolve()),
                    fileHash=hashlib.sha256(args.source.read_bytes()).hexdigest(),
                    originalSampleRate=rate, originalChannels=channels,
                    observations='Tone listening preparation; human verdict pending.')
    template['source'].update(kind='file', durationSamples=frames)
    signals = [('dry', source, None)]
    for label, db in (('dark', -6), ('neutral', 0), ('bright', 6)):
        record = dict(template, toneDb=db)
        settings = args.output/(label+'.json')
        settings.write_text(json.dumps(record, indent=2)+'\n')
        destination = args.output/(label+'-render')
        subprocess.run([str(args.renderer.resolve()), '--experiment', str(settings.resolve()),
                        '--output-dir', str(destination.resolve())], check=True)
        actual_rate, actual_channels, audio = read_wav(destination/'rendered.wav')
        if (actual_rate, actual_channels, len(audio)) != (rate, channels, len(source)):
            raise ValueError('render layout mismatch')
        signals.append((label, audio, db))
    start = int(.01*rate)*channels
    rms = [math.sqrt(sum(v*v for v in audio[start:])/len(audio[start:])) for _, audio, _ in signals]
    if min(rms) <= 1e-12:
        raise ValueError('source too quiet for level matching')
    gains = [min(rms)/value for value in rms]
    peak = max(max(abs(v) for v in audio)*gain for (_, audio, _), gain in zip(signals, gains))
    common = min(1., .95/peak)
    rows = []
    for (label, audio, db), gain, value in zip(signals, gains, rms):
        gain *= common
        path = args.output/(label+'-matched.wav')
        write_wav(path, rate, channels, [v*gain for v in audio])
        rows.append(dict(label=label, tone_db=db, gain=gain, rms_before=value,
                         rms_after=value*gain, sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    report = dict(source=str(args.source.resolve()), source_sha256=template['fileHash'],
                  sample_rate=rate, channels=channels, frames=frames,
                  matching_start_frame=start//channels, matching='RMS; attenuation only; common peak ceiling .95',
                  listening_approved=False, comparisons=rows)
    (args.output/'listening.json').write_text(json.dumps(report, indent=2)+'\n')

if __name__ == '__main__':
    main()
