#!/usr/bin/env python3
"""Validate complete output-section evidence, independent transfer functions and hashes."""
import argparse
import cmath
import csv
import hashlib
import itertools
import json
import math
import shutil
import struct
import tempfile
from pathlib import Path

RATES = (8000, 44100, 48000, 96000, 192000, 384000)
CUTOFFS = ((20, 20000), (200, 8000), (1000, 1000), (8000, 200), (20, 20), (20000, 20000))
MODES = ('butterworth', 'chebyshev-1', 'chebyshev-2')
SOURCES = ('tools/OutputSectionMeasureMain.cpp', 'tools/OutputSectionReference.h',
           'src/dsp/OutputSection.h', 'src/dsp/OutputSection.cpp', 'src/dsp/CutoffFilter.h',
           'src/harness/AuditionEngine.cpp', 'src/harness/OutputSectionTarget.h',
           'src/harness/OfflineRender.cpp', 'src/harness/ExperimentRecord.cpp',
           'src/harness/SignalSource.cpp', 'scripts/validate_output_measurements.py')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def close(actual, expected, message, tolerance=2e-10):
    require(math.isfinite(actual) and math.isfinite(expected) and
            math.isclose(actual, expected, rel_tol=tolerance, abs_tol=tolerance),
            f'{message}: {actual} != {expected}')


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def finite_tree(value):
    if isinstance(value, float):
        require(math.isfinite(value), 'nonfinite JSON value')
    elif isinstance(value, dict):
        for child in value.values():
            finite_tree(child)
    elif isinstance(value, list):
        for child in value:
            finite_tree(child)


def read_json(path):
    result = json.loads(path.read_text(encoding='utf-8-sig'))
    finite_tree(result)
    return result


def rows(directory, name):
    with (directory / name).open(encoding='utf-8-sig', newline='') as file:
        result = list(csv.DictReader(file))
    for row in result:
        for key, value in row.items():
            require(key is not None and value is not None, f'{name}: malformed CSV')
            row[key] = float(value)
            require(math.isfinite(row[key]), f'{name}/{key}: nonfinite value')
    return result


def coefficients(high, mode, rate, cutoff):
    # Independent direct-form BLT derived from analog conjugate poles/zeros.
    n2, n0, d1, d0 = 0., 1., math.sqrt(2), 1.
    if mode:
        epsilon = math.sqrt(10 ** .1 - 1) if mode == 1 else 1 / math.sqrt(9999)
        pole = -cmath.sinh(complex(math.asinh(1 / epsilon) / 2, math.pi / 4))
        if mode == 2:
            pole = 1 / pole
        w3 = math.sqrt((1 + 1 / epsilon) / 2) if mode == 1 else math.sqrt(2 / (1 + 1 / epsilon))
        pole /= w3
        d1, d0 = -2 * pole.real, abs(pole) ** 2
        if mode == 1:
            n0 = d0 * 10 ** -.05
        else:
            n2, n0 = .01, d0
    if high:
        n2, n0 = n0, n2
    d2, dc = (d0, 1.) if high else (1., d0)
    g = math.tan(math.pi * min(cutoff, .45 * rate) / rate)
    scale = d2 + d1 * g + dc * g * g
    return ((n2 + n0 * g * g) / scale, 2 * (n0 * g * g - n2) / scale,
            (n2 + n0 * g * g) / scale, 2 * (dc * g * g - d2) / scale,
            (d2 - d1 * g + dc * g * g) / scale)


def response(case, frequency):
    rate, hp, lp, high_cutoff, low_cutoff = case
    z = cmath.exp(-2j * math.pi * frequency / rate)
    result = 1 + 0j
    for high, mode, cutoff in ((True, hp, high_cutoff), (False, lp, low_cutoff)):
        b0, b1, b2, a1, a2 = coefficients(high, mode, rate, cutoff)
        result *= (b0 + b1 * z + b2 * z * z) / (1 + a1 * z + a2 * z * z)
    return result


def frequencies(case):
    rate, hp, lp, high_cutoff, low_cutoff = case
    high_cutoff, low_cutoff = min(high_cutoff, .45 * rate), min(low_cutoff, .45 * rate)
    result = [0, rate * .01, rate * .1, rate * .25, rate * .49, rate * .5,
              high_cutoff, low_cutoff, math.sqrt(high_cutoff * low_cutoff)]
    for high, mode, cutoff in ((True, hp, high_cutoff), (False, lp, low_cutoff)):
        if mode == 2:
            factor = math.sqrt(2) / math.sqrt(2 / (1 + math.sqrt(9999)))
            result.append(rate / math.pi * math.atan(math.tan(math.pi * cutoff / rate) *
                                                    (1 / factor if high else factor)))
    return sorted(set(result))


def check_identity(row, case, index):
    rate, hp, lp, high_cutoff, low_cutoff = case
    for name, expected in zip(('case', 'rate', 'hp_mode', 'lp_mode', 'hp_cutoff', 'lp_cutoff'),
                              (index, rate, hp, lp, high_cutoff, low_cutoff)):
        require(row[name] == expected, f'case {index}: {name} mismatch')


def wav(path):
    raw = path.read_bytes()
    require(raw[:4] == b'RIFF' and raw[8:12] == b'WAVE', f'{path}: invalid RIFF')
    require(struct.unpack_from('<I', raw, 4)[0] + 8 == len(raw), f'{path}: length mismatch')
    position, fmt, data = 12, None, None
    while position + 8 <= len(raw):
        tag, size = struct.unpack_from('<4sI', raw, position)
        position += 8
        require(position + size <= len(raw), f'{path}: truncated chunk')
        if tag == b'fmt ': fmt = struct.unpack_from('<HHIIHH', raw, position)
        if tag == b'data': data = raw[position:position + size]
        position += size + size % 2
    require(fmt is not None and data is not None, f'{path}: missing chunks')
    require(fmt == (3, 2, 48000, 384000, 8, 32), f'{path}: expected stereo 48k float32')
    require(len(data) == 144000 * 8, f'{path}: frame count')
    values = [sample[0] for sample in struct.iter_unpack('<f', data)]
    require(all(math.isfinite(sample) for sample in values), f'{path}: nonfinite samples')
    return values


def validate(directory, check_local=True):
    report = read_json(directory / 'result.json')
    require(report['schemaVersion'] == 1 and report['passed'] is True, 'schema/pass flag')
    for key in ('build', 'reference', 'listening', 'groupDelayPolicy'):
        require(isinstance(report.get(key), str) and report[key], f'missing {key}')
    require(report['referenceHardwareQualified'] is False and report['listening'] == 'pending human evaluation', 'qualification status')
    for key, count in {'staticCases': 324, 'orderedCases': 1296, 'toneCases': 288,
                       'stressCases': 24, 'tailCases': 36, 'fixtureCases': 72, 'latencySamples': 0}.items():
        require(report[key] == count, f'{key}: incomplete')
    manifest = read_json(directory / 'manifest.json')
    actual_files = {str(file.relative_to(directory)).replace('\\', '/') for file in directory.rglob('*')
                    if file.is_file() and file.name != 'manifest.json'}
    require(set(manifest) == actual_files, 'artifact manifest incomplete or unexpected files')
    require(all(digest(directory / name) == expected for name, expected in manifest.items()), 'artifact hash mismatch')
    require(set(report['sourceSha256']) == set(SOURCES), 'source hash completeness')
    for value in [*manifest.values(), *report['sourceSha256'].values(), report['executableSha256']]:
        require(isinstance(value, str) and len(value) == 64 and all(c in '0123456789abcdef' for c in value), 'invalid SHA256')
    if check_local:
        source_root = Path(report['sourceRoot'])
        require(all(digest(source_root / name) == expected for name, expected in report['sourceSha256'].items()), 'current source hash mismatch')
        require(digest(Path(report['executablePath'])) == report['executableSha256'], 'current executable hash mismatch')
    cases = [(r, hp, lp, *cutoffs) for r, hp, lp, cutoffs in itertools.product(RATES, range(3), range(3), CUTOFFS)]
    impulses = rows(directory, 'impulses.csv')
    require(len(impulses) == len(cases), 'impulse completeness')
    for index, (row, case) in enumerate(zip(impulses, cases)):
        check_identity(row, case, index)
        require(row['frames'] == 131072 and 0 <= row['max_error'] <= 6e-6 and
                0 <= row['interstage_peak'] <= 4 and row['latency_samples'] == 0, 'impulse limits')
    spectra = rows(directory, 'spectra.csv')
    expected_points = [(index, case, f) for index, case in enumerate(cases) for f in frequencies(case)]
    require(len(spectra) == len(expected_points), 'spectra completeness')
    for row, (index, case, frequency) in zip(spectra, expected_points):
        require(row['case'] == index and row['latency_samples'] == 0, 'spectrum identity/latency')
        close(row['frequency'], frequency, 'frequency')
        expected = response(case, frequency)
        recorded = complex(row['expected_real'], row['expected_imaginary'])
        require(abs(recorded - expected) < 1e-8, 'independent complex reference mismatch')
        measured = complex(row['measured_real'], row['measured_imaginary'])
        error = abs(measured - recorded)
        close(row['complex_error'], error, 'complex error')
        require(error <= 6e-5, 'complex response limit')
        phase_defined = abs(recorded) >= 1e-9 and abs(measured) >= 1e-9
        require(row['phase_defined'] == int(phase_defined), 'phase definition')
        close(row['phase_radians'], cmath.phase(measured) if phase_defined else 0, 'phase')
        angle = 2 * math.pi * frequency / case[0]
        lower, upper = max(0, angle - 1e-6), min(math.pi, angle + 1e-6)
        a, b = response(case, lower * case[0] / (2 * math.pi)), response(case, upper * case[0] / (2 * math.pi))
        measured_lower = complex(row['measured_lower_real'], row['measured_lower_imaginary'])
        measured_upper = complex(row['measured_upper_real'], row['measured_upper_imaginary'])
        defined = all(abs(value) > .001 for value in (recorded, measured, a, b, measured_lower, measured_upper))
        require(row['group_delay_defined'] == int(defined), 'group delay definition')
        reference_delay = -cmath.phase(b * a.conjugate()) / (upper - lower) if defined else 0
        measured_delay = -cmath.phase(measured_upper * measured_lower.conjugate()) / (upper - lower) if defined else 0
        close(row['reference_group_delay_samples'], reference_delay, 'reference group delay', 2e-5)
        close(row['group_delay_samples'], measured_delay, 'measured impulse group delay', 2e-8)
        close(row['group_delay_error_samples'], abs(row['group_delay_samples'] - row['reference_group_delay_samples']), 'group delay diagnostic error')
    ordered = rows(directory, 'ordered.csv')
    require(len(ordered) == 1296, 'ordered completeness')
    for index, row in enumerate(ordered):
        check_identity(row, cases[index // 4], index)
        require(row['bypass'] == index % 4 and row['frames'] == 2051 and
                0 <= row['max_error'] <= 1e-6 and 0 <= row['interstage_peak'] <= 4 and
                row['latency_samples'] == 0, 'ordered bounds/bypass')
    tones = rows(directory, 'tones.csv')
    tone_specs = itertools.product(range(3), range(3), CUTOFFS[:4], (40, 125, 500, 1000, 2000, 4000, 8000, 16000))
    require(len(tones) == 288, 'tone completeness')
    for index, (row, (hp, lp, pair, frequency)) in enumerate(zip(tones, tone_specs)):
        require(row['case'] == index and (row['hp_mode'], row['lp_mode'], row['hp_cutoff'], row['lp_cutoff'], row['frequency']) == (hp, lp, *pair, frequency), 'tone identity')
        expected = abs(response((48000, hp, lp, *pair), frequency))
        close(row['expected_magnitude'], expected, 'tone independent magnitude', 1e-8)
        require(row['measured_magnitude'] >= 0, 'negative tone magnitude')
        close(row['absolute_error'], abs(row['measured_magnitude'] - row['expected_magnitude']), 'tone absolute error')
        gate = row['expected_magnitude'] > .001
        require(row['db_gate'] == int(gate), 'tone gate')
        if gate:
            close(row['db_error'], 20 * math.log10(row['measured_magnitude'] / row['expected_magnitude']), 'tone dB')
            require(abs(row['db_error']) <= .1, 'tone dB limit')
        else:
            require(row['db_error'] == 0 and row['absolute_error'] <= 6e-5, 'deep stopband absolute limit')
    stability = rows(directory, 'stability.csv')
    require(len(stability) == 24, 'stability completeness')
    for index, row in enumerate(stability):
        require(row['case'] == index and row['rate'] == RATES[index // 4] and row['source_kind'] == index % 4 and row['blocks'] == 160 and row['finite'] == 1 and row['latency_samples'] == 0, 'stability identity')
        require(0 <= row['ordered_error'] <= 1e-6 and 0 <= row['peak'] < 8 and 0 <= row['interstage_peak'] <= 4, 'stability bounds')
    tails = rows(directory, 'tails.csv')
    require(len(tails) == 36, 'tail completeness')
    for index, row in enumerate(tails):
        require((row['case'], row['hp_mode'], row['lp_mode'], row['bypass'], row['rate'], row['silence_frames'], row['latency_samples']) == (index, index // 12, (index // 4) % 3, index % 4, 384000, 768000, 0), 'tail identity')
        require(0 <= row['tail_peak'] < 1e-6, 'tail limit')
    aggregates = {'maxImpulseError': max(r['max_error'] for r in impulses),
                  'maxComplexError': max(r['complex_error'] for r in spectra),
                  'maxToneDbError': max(abs(r['db_error']) for r in tones if r['db_gate']),
                  'maxToneAbsoluteError': max(r['absolute_error'] for r in tones if not r['db_gate']),
                  'maxOrderedError': max([r['max_error'] for r in ordered] + [r['ordered_error'] for r in stability]),
                  'maxOutputPeak': max(r['peak'] for r in stability),
                  'maxInterstagePeak': max(r['interstage_peak'] for r in impulses + ordered + stability),
                  'maxTailPeak': max(r['tail_peak'] for r in tails)}
    for key, value in aggregates.items():
        close(report[key], value, f'report {key}')
    fixture_specs = itertools.product(range(3), range(3), CUTOFFS[:4], range(2))
    for index, (hp, lp, pair, kind) in enumerate(fixture_specs):
        fixture = directory / f'fixture-{index}'
        rendered = read_json(fixture / 'result.json')
        record = rendered['experiment']
        require(record['version'] == 10 and record['targetId'] == 'output-section', 'fixture record schema')
        require(rendered['samples'] == 144000 and rendered['latencySamples'] == 0, 'fixture frame/latency')
        settings = {key: record[key] for key in ('hpCutoffHz', 'hpResponse', 'hpBypass',
                                                'lpCutoffHz', 'lpResponse', 'lpBypass')}
        require(settings == {'hpCutoffHz': pair[0], 'hpResponse': MODES[hp], 'hpBypass': False,
                             'lpCutoffHz': pair[1], 'lpResponse': MODES[lp], 'lpBypass': False}, 'fixture settings')
        require(record['bypass'] is False and record['sampleRate'] == 48000 and record['channels'] == 2 and
                record['renderDurationSamples'] == 144000, 'fixture layout/common bypass')
        require(record['source'] == {'kind': 'impulse' if kind else 'sweep', 'frequency': 20,
                                     'frequency2': 20000, 'amplitude': .25, 'phase': 0,
                                     'durationSamples': 144000, 'seed': 1}, 'fixture source identity')
        source, wet = wav(fixture / 'source.wav'), wav(fixture / 'rendered.wav')
        require(source[::2] == source[1::2] and wet[::2] == wet[1::2], 'fixture channel independence')
        for frame in range(0, 144000, 997):
            if kind:
                expected_source = .25 if frame == 0 else 0.
            else:
                log_span = math.log(1000)
                cycles = (math.exp(math.log(20) + log_span * frame / 143999) - 20) * 143999 / 48000 / log_span
                expected_source = .25 * math.sin(2 * math.pi * cycles)
            close(source[frame * 2], expected_source, 'independent fixture source', 3e-7)
        if kind:
            high_coefficients = coefficients(True, hp, 48000, pair[0])
            low_coefficients = coefficients(False, lp, 48000, pair[1])
            state = [[0., 0., 0., 0.], [0., 0., 0., 0.]]
            maximum_error = 0.
            for frame in range(144000):
                value = source[2 * frame]
                for stage, coefficient in enumerate((high_coefficients, low_coefficients)):
                    b0, b1, b2, a1, a2 = coefficient
                    x1, x2, y1, y2 = state[stage]
                    output = b0 * value + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
                    state[stage] = [value, x1, output, y1]
                    value = struct.unpack('<f', struct.pack('<f', output))[0]
                maximum_error = max(maximum_error, abs(value - wet[frame * 2]))
            require(maximum_error <= 6e-6, 'independent fixture impulse sample limit')
        for label, audio in (('input', source), ('output', wet)):
            peak, rms = max(map(abs, audio)), math.sqrt(sum(v * v for v in audio) / len(audio))
            close(rendered[label + 'Peak'], peak, f'{label} peak', 1e-7)
            close(rendered[label + 'Rms'], rms, f'{label} rms', 1e-7)
    return manifest


def rejection_checks(directory):
    # Copy once and mutate/restore one artifact per case. Updating its hash makes
    # the nonfinite and incomplete cases exercise numeric/completeness checks.
    with tempfile.TemporaryDirectory(prefix='output-section-validator-') as temporary:
        destination = Path(temporary) / 'evidence'
        shutil.copytree(directory, destination)
        validate(destination, check_local=False)
        manifest_path = destination / 'manifest.json'
        manifest_bytes = manifest_path.read_bytes()
        for filename, mutation, update_hash in (
            ('result.json', lambda b: b.replace(b'"maxImpulseError":', b'"extraNonfinite": NaN, "maxImpulseError":', 1), True),
            ('impulses.csv', lambda b: b[:b.rfind(b'\n', 0, len(b) - 1) + 1], True),
            ('fixture-0/rendered.wav', lambda b: b[:-1] + bytes([b[-1] ^ 1]), False),
        ):
            path = destination / filename
            original = path.read_bytes()
            path.write_bytes(mutation(original))
            if update_hash:
                manifest = json.loads(manifest_bytes)
                manifest[filename] = digest(path)
                manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
            rejected = False
            try:
                validate(destination, check_local=False)
            except (OSError, ValueError, KeyError, TypeError, OverflowError):
                rejected = True
            require(rejected, f'validator accepted mutation: {filename}')
            path.write_bytes(original)
            manifest_path.write_bytes(manifest_bytes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--repeat', type=Path)
    parser.add_argument('--self-test', action='store_true', help='prove nonfinite/incomplete/tampered evidence rejection')
    parser.add_argument('--artifacts-only', action='store_true', help='validate archived evidence without current source/executable files')
    args = parser.parse_args()
    try:
        manifest = validate(args.directory, not args.artifacts_only)
        if args.repeat:
            repeated = validate(args.repeat, not args.artifacts_only)
            require(manifest == repeated, 'repeat artifacts differ')
        if args.self_test:
            rejection_checks(args.directory)
        print('PASS: complete numeric gates, independent complex reference, provenance, float WAVs' +
              (', deterministic repeat' if args.repeat else '') +
              (', nonfinite/incomplete/tamper rejection' if args.self_test else '') + '. Human listening and reference CPU remain pending.')
    except (OSError, ValueError, KeyError, TypeError, OverflowError, ZeroDivisionError, struct.error) as error:
        parser.exit(1, f'FAIL: {error}\n')


if __name__ == '__main__':
    main()
