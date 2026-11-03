"""Independent standard-library audit of tone response and CPU evidence."""
import argparse
import cmath
import csv
import hashlib
import json
import math
from pathlib import Path

RATES = (8000, 44100, 48000, 88200, 96000, 192000, 384000)

def require(ok, message):
    if not ok:
        raise ValueError(message)

def check_response(rate, db, frequency, actual):
    require(math.isfinite(actual.real) and math.isfinite(actual.imag), 'nonfinite response')
    g = 10**(db/20)
    if frequency == rate/2:
        expected = complex(g)
    else:
        s = complex(0, math.tan(math.pi*frequency/rate)/math.tan(math.pi*1000/rate))
        expected = (g*s+1)/(s+g)
    error = abs(actual-expected)
    require(error <= 1e-5, 'complex response exceeds 1e-5')
    require(abs(20*math.log10(abs(actual/expected))) <= .05, 'magnitude error exceeds .05 dB')
    return error

def audit(root):
    report = json.loads((root/'result.json').read_text(encoding='utf-8-sig'))
    require(report['schema'] == 1 and report['kind'] == 'tone-response', 'wrong schema')
    require(report['latency_samples'] == 0 and report['impulse_seconds'] == .1, 'wrong capture policy')
    data = (root/'response.csv').read_bytes()
    require(hashlib.sha256(data).hexdigest() == report['response_sha256'], 'response hash mismatch')
    seen = set()
    counts = {}
    maximum = 0
    for row in csv.DictReader(data.decode().splitlines()):
        fs, db, f = (float(row[key]) for key in ('rate', 'tone_db', 'frequency'))
        key = (fs, db, f)
        require(key not in seen, 'duplicate response')
        seen.add(key)
        counts[fs, db] = counts.get((fs, db), 0)+1
        maximum = max(maximum, check_response(fs, db, f, complex(float(row['real']), float(row['imag']))))
    require(set(counts) == {(fs, db) for fs in RATES for db in (-6, -3, 0, 3, 6)}, 'incomplete matrix')
    require(all(count == 101 for count in counts.values()), 'incomplete grid')
    for fs, db in counts:
        require(all((fs, db, f) in seen for f in (0, 1000, fs/2)), 'missing endpoints or pivot')
    return {'passed': True, 'cases': len(counts), 'bins': len(seen), 'complex_error_max': maximum}

def audit_cpu(root):
    report = json.loads((root/'result.json').read_text(encoding='utf-8-sig'))
    require(report['kind'] == 'tone-cpu' and report['schema'] == 1, 'wrong CPU schema')
    for name in ('timings', 'summary'):
        require(hashlib.sha256((root/(name+'.csv')).read_bytes()).hexdigest() == report[name+'_sha256'], 'CPU hash mismatch')
    groups = {}
    with (root/'timings.csv').open() as stream:
        for row in csv.DictReader(stream):
            ns = int(row['elapsed_ns']);require(ns >= 0, 'negative elapsed time')
            groups.setdefault(int(row['case']), []).append(ns)
    count = 0
    worst = 0
    with (root/'summary.csv').open() as stream:
        for row in csv.DictReader(stream):
            values = sorted(groups.pop(int(row['case'])))
            require(len(values) == int(row['count']), 'raw observation count mismatch')
            p99 = values[math.ceil(.99*len(values))-1]
            require(p99 == int(row['p99_ns']), 'raw p99 mismatch')
            deadline = int(row['frames'])/float(row['rate'])*1e9
            require(abs(deadline-float(row['deadline_ns'])) < 1e-6, 'deadline mismatch')
            fraction = p99/deadline
            require(abs(fraction-float(row['fraction'])) < 1e-12, 'fraction mismatch')
            require(row['processing_ok'] == '1', 'processing failed')
            worst = max(worst, fraction);count += 1
    require(not groups and count == len(report['cases']), 'incomplete CPU rows')
    expected = {(run, rate, frames, scenario) for run in range(report['runs'])
                for rate in (48000, 96000, 192000) for frames in (32, 64, 127, 256, 1024) for scenario in range(7)}
    actual = {(r['run'], r['rate'], r['capacity'], r['scenario']) for r in report['cases']}
    require(actual == expected and len(actual) == count, 'incomplete CPU matrix')
    return {'passed': True, 'cases': count, 'worst_p99_fraction': worst,
            'budget_passed': worst < .02, 'reference_claim': report['reference_qualified'],
            'qualification_note': 'raw timings audited; environment and coverage require reference review'}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--cpu', action='store_true')
    args = parser.parse_args()
    print(json.dumps(audit_cpu(args.directory) if args.cpu else audit(args.directory), indent=2))
