"""Audit pinned three-run dispersion CPU evidence against raw timing observations."""
import argparse
import csv
import hashlib
import json
import math
import itertools
from pathlib import Path

def require(value, message):
    if not value:
        raise ValueError(message)

def audit(root):
    report = json.loads((root/'result.json').read_text())
    require(report['kind'] == 'frequency-dispersion-cpu' and report['schema'] == 1, 'wrong schema')
    require(report['quick'] is False and report['runs'] == 3, 'diagnostic is not reference evidence')
    require(all(report[key] is True for key in ('pinned','hybrid_established','on_ac','release')), 'reference environment requirements failed')
    for name in ('timings','summary'):
        with (root/(name+'.csv')).open('rb') as artifact:
            digest = hashlib.file_digest(artifact, 'sha256').hexdigest()
        require(digest == report[name+'_sha256'], 'raw evidence hash mismatch')
    with (root/'summary.csv').open() as stream:
        summary = list(csv.DictReader(stream))
    expected = {(run, rate, frames, scenario) for run in range(3) for rate in (44100,48000,96000,192000) for frames in (64,256,1024) for scenario in range(4)}
    actual = {(int(row['run']),int(float(row['rate'])),int(row['frames']),int(row['scenario'])) for row in summary}
    require(actual == expected and len(summary) == 144, 'missing or duplicate CPU matrix rows')
    require(len(report['cases']) == 144, 'missing metadata cases')
    worst = 0.
    summary_by_case = {int(row['case']): row for row in summary}
    seen = set()
    # Cases are contiguous in collector output; keep only one case in memory.
    stream = (root/'timings.csv').open()
    for case, group in itertools.groupby(csv.DictReader(stream), key=lambda row: int(row['case'])):
        require(case not in seen and case in summary_by_case, 'duplicate or unknown raw case')
        seen.add(case)
        row = summary_by_case[case]
        observations = list(group)
        count = len(observations)
        require(100000 <= count <= 200000 and count == int(row['count']), 'sampling count gate failed')
        require(10 <= float(row['window_seconds']) <= 120, 'wall-time coverage gate failed')
        for key in ('processing_ok','coverage_ok','power_stable'):
            require(row[key] == '1', key+' failed')
        timings = sorted(int(obs['elapsed_ns']) for obs in observations)
        require(timings[0] >= 0, 'negative timing')
        p99 = timings[math.ceil(.99*count)-1]
        require(p99 == int(row['p99_ns']), 'p99 differs from raw observations')
        deadline = int(row['frames']) / float(row['rate']) * 1e9
        fraction = p99/deadline
        require(abs(fraction-float(row['fraction'])) < 1e-12, 'deadline calculation mismatch')
        require(fraction < .05, 'p99 callback budget failed')
        if int(row['scenario']) == 1:
            residues = [0]*101
            for obs in observations:
                residues[int(obs['control_residue'])] += 1
            require(min(residues) >= 100, 'automation coverage failed')
        if int(row['scenario']) == 2:
            rate, frames = float(row['rate']), int(row['frames'])
            ramp = math.ceil(.01*rate/frames)
            dwell = ramp+8
            counts = [0,0]
            for obs in observations:
                iteration = int(obs['iteration'])
                target = (iteration//dwell)%2
                require(target == int(obs['bypass_target']), 'bypass target mismatch')
                if iteration%dwell >= ramp:
                    counts[target] += 1
            require(min(counts) >= 100, 'settled bypass coverage failed')
        worst = max(worst, fraction)
    stream.close()
    require(len(seen) == 144, 'missing raw cases')
    require(report['reference_qualified'] is True, 'collector did not qualify reference')
    return {'passed':True, 'rows':144, 'runs':3, 'worst_p99_deadline_fraction':worst}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(audit(args.directory),indent=2))
    except (ValueError,KeyError,OSError) as error:
        raise SystemExit(str(error))
