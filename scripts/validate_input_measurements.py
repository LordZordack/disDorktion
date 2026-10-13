#!/usr/bin/env python3
"""Independently validate input interaction artifacts using only the standard library.

WAVs use IEEE float32 (format 3) to retain values above unity. This validator
recomputes aggregates, output/reference equality, projections, settled gain and
neutral/8-bit PCM expectations. It does not recreate expressive crusher code.
"""
import argparse
import csv
import json
import math
import struct
from pathlib import Path

FIXTURES = {'sine', 'transients', 'high-sine', 'two-tone', 'sweep'}
FIELDS = ('gainDb', 'mute', 'gainBypass', 'bitDepth', 'captureRateHz',
          'crusherBypass', 'mix', 'crush', 'shape', 'jitter', 'jitterSeed')
CONTROLS = [
    (0, False, False, 64, 384000, False, 1, 0, 0, 0, 1),
    (24, False, False, 8, 384000, False, 1, 0, 0, 0, 1),
    (12, False, False, 3, 8000, False, 1, .6, -.8, 1, 123),
    (24, False, False, 4, 4000, False, .25, 1, .8, 1, 123),
    (24, False, True, 2, 6000, False, 1, 1, -.8, .7, 17),
    (24, False, False, 1, 100, True, 1, 1, 1, 1, 17),
    (24, False, True, 8, 100, True, 0, 1, -1, 1, 17),
    (24, True, False, 8, 100, False, 1, .5, .8, 1, 123),
]

def require(condition, message):
    if not condition:
        raise ValueError(message)

def close(actual, expected, label, tolerance=2e-12):
    require(math.isfinite(actual) and math.isclose(actual, expected, rel_tol=tolerance, abs_tol=tolerance),
            f'{label}: {actual} != {expected}')

def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]

def wav(path):
    raw = path.read_bytes()
    require(raw[:4] == b'RIFF' and raw[8:12] == b'WAVE', f'{path}: invalid RIFF')
    require(struct.unpack_from('<I', raw, 4)[0] + 8 == len(raw), f'{path}: RIFF length mismatch')
    position = 12
    fmt = data = fact = None
    while position + 8 <= len(raw):
        tag, size = struct.unpack_from('<4sI', raw, position)
        position += 8
        require(position + size <= len(raw), f'{path}: truncated chunk')
        payload = raw[position:position + size]
        if tag == b'fmt ': fmt = struct.unpack('<HHIIHH', payload)
        if tag == b'data': data = payload
        if tag == b'fact': fact = struct.unpack('<I', payload)[0]
        position += size + size % 2
    require(fmt is not None and data is not None, f'{path}: missing format/data')
    encoding, channels, rate, byte_rate, align, bits = fmt
    require((encoding, channels, bits, align, byte_rate) == (3, 2, 32, 8, rate * 8), f'{path}: unexpected WAV format')
    require(len(data) % 8 == 0 and fact == len(data) // 8, f'{path}: frame count mismatch')
    values = [x[0] for x in struct.iter_unpack('<f', data)]
    require(all(math.isfinite(x) for x in values), f'{path}: nonfinite sample')
    return rate, values

def aggregate(values):
    return {'peak': max(map(abs, values)),
            'rms': math.sqrt(sum(x * x for x in values) / len(values)),
            'overloadSamples': sum(abs(x) > 1 for x in values)}

def expected_events(rate):
    events = [dict(frame=n * rate, **dict(zip(FIELDS, settings))) for n, settings in enumerate(CONTROLS)]
    for n in range(1, 17):
        e = dict(events[2], frame=2 * rate + n * (rate // 1000))
        e.update(gainDb=24 if n % 2 else -12, mix=1 if n % 2 else .2,
                 shape=.8 if n % 2 else -.8, bitDepth=2 if n % 2 else 16)
        events.append(e)
    return sorted(events, key=lambda e: e['frame'])

def source_sample(fixture, frame, rate):
    t = frame / rate
    phase = 6000 * t if fixture == 'high-sine' else 440 * t
    if fixture == 'sweep': phase = 80 * t + (12000 - 80) * t * t / 16
    envelope = math.exp(-(frame % (rate // 4)) / 1000) if fixture == 'transients' else 1
    sample = .25 * envelope * math.sin(math.tau * phase)
    if fixture == 'two-tone': sample += .125 * math.sin(math.tau * 6000 * t)
    return (f32(sample), f32(-.7 * sample))

def validate(directory):
    report = json.loads((directory / 'result.json').read_text(encoding='utf-8-sig'))
    require(report['schemaVersion'] == 1 and report['passed'] is True, 'schema/pass flag')
    require(all(report.get(key) for key in ('build', 'juce', 'gitRevision', 'sourceIdentity', 'reference', 'blocks', 'listening')), 'missing provenance')
    expected = {f'{fixture}-{rate}' for fixture in FIXTURES for rate in (48000, 96000)}
    cases = report['cases']
    require(len(cases) == 10 and {c['id'] for c in cases} == expected, 'missing or duplicate cases')
    rows = list(csv.DictReader((directory / 'metrics.csv').open(encoding='utf-8-sig', newline='')))
    require(len(rows) == 10 and {r['id'] for r in rows} == expected, 'CSV completeness')
    rows = {r['id']: r for r in rows}
    for case in cases:
        name, rate = case['fixture'], case['rateHz']
        require(name in FIXTURES and rate in (48000, 96000) and case['id'] == f'{name}-{rate}', 'case identity')
        require(case['frames'] == rate * 8 and case['channels'] == 2 and case['passed'] is True, 'case layout/pass')
        expected_schedule = expected_events(rate)
        events = case['events']
        require(len(events) == len(expected_schedule), 'event schedule count')
        for e, target in zip(events, expected_schedule):
            require(set(e) == set(target), 'event fields')
            for key, value in target.items():
                if type(value) is bool: require(e[key] is value, f'event {key}')
                else: close(e[key], value, f'event {key}', 5e-7)
        audio = {}
        for suffix in ('source', 'wet', 'reference', 'post-gain'):
            wav_rate, samples = wav(directory / f"{case['id']}-{suffix}.wav")
            require(wav_rate == rate and len(samples) == rate * 8 * 2, 'WAV layout')
            audio[suffix] = samples
        for suffix, metric in (('source', 'source'), ('wet', 'output'), ('post-gain', 'postGain')):
            measured = aggregate(audio[suffix])
            for field, value in measured.items():
                if field == 'overloadSamples': require(case[metric][field] == value, 'overload count')
                else: close(case[metric][field], value, f'{case["id"]}/{metric}/{field}', 2e-10 if field == 'rms' else 2e-12)
        error = max(abs(a - b) for a, b in zip(audio['wet'], audio['reference']))
        require(error == case['compositionMaxError'] == 0, 'composition must be sample exact')
        require(case['postGainMeterMaxError'] == 0 and case['latencySamples'] == 0, 'meter/latency')
        # Sparse independent source formula check, spanning every section.
        for frame in range(0, rate * 8, 997):
            for channel, value in enumerate(source_sample(name, frame, rate)):
                close(audio['source'][frame * 2 + channel], value, 'source formula', 2e-7)
        # Independent settled gain and PCM checks (after the 10 ms ramps).
        for index, event in enumerate(events):
            end = events[index + 1]['frame'] if index + 1 < len(events) else rate * 8
            start = event['frame'] + rate // 100 + 2
            gain = f32(1 if event['gainBypass'] else 0 if event['mute'] else 10 ** (event['gainDb'] / 20))
            for frame in range(start, end, 23):
                for channel in range(2):
                    offset = frame * 2 + channel
                    predicted = f32(audio['source'][offset] * gain)
                    close(audio['post-gain'][offset], predicted, 'settled gain', 2e-6)
                    # Neutral high-rate 64-bit and overloaded classical 8-bit sections.
                    if event['frame'] in (0, rate):
                        scale = 2 ** (event['bitDepth'] - 1)
                        v = max(-1, min(1, predicted)) * scale
                        code = math.floor(v + .5) if v >= 0 else math.ceil(v - .5)
                        expected_wet = f32(max(-scale, min(scale - 1, code)) / scale)
                        close(audio['wet'][offset], expected_wet, 'independent PCM oracle', 2e-7)
                    if event['frame'] == 6 * rate:
                        require(audio['wet'][offset] == audio['source'][offset], 'settled double bypass')
        projections = case['spectralProjections']
        require(len(projections) == 32, 'spectral completeness')
        require({(p['startFrame'], p['frequencyHz']) for p in projections} ==
                {((s + 1) * rate - 2048, f) for s in range(8) for f in (440, 2000, 6000, 10000)}, 'spectral band identities')
        for p in projections:
            require(p['frames'] == 2048, 'spectral window')
            real = imaginary = 0.0
            for n in range(2048):
                phase = math.tau * p['frequencyHz'] * n / rate
                value = audio['wet'][(p['startFrame'] + n) * 2]
                real += value * math.cos(phase)
                imaginary -= value * math.sin(phase)
            close(p['amplitude'], 2 * math.hypot(real, imaginary) / 2048, 'spectral amplitude', 2e-10)
        row = rows[case['id']]
        for key, value in {'rate_hz': rate, 'frames': rate * 8, 'composition_max_error': error,
                           'meter_max_error': 0, 'latency_samples': 0}.items():
            close(float(row[key]), value, f'CSV {key}')
        for stage, metric in (('post_gain', 'postGain'), ('output', 'output')):
            for csv_field, json_field in (('peak', 'peak'), ('rms', 'rms'), ('overload_samples', 'overloadSamples')):
                close(float(row[f'{stage}_{csv_field}']), case[metric][json_field], 'CSV aggregate')
        require(row['passed'] == 'true', 'CSV pass')
    require(any(c['postGain']['overloadSamples'] > 0 for c in cases), 'missing measured overload')
    print('PASS: 10 complete cases; float WAV aggregates, exact composition, independent source/gain/PCM oracles, CSV and spectra validated. Human listening remains pending.')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        validate(args.directory)
    except (OSError, ValueError, KeyError, TypeError, OverflowError) as error:
        parser.exit(1, f'FAIL: {error}\n')

if __name__ == '__main__':
    main()
