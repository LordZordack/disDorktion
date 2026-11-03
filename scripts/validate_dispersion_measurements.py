"""Independent standard-library audit of dispersion impulse/automation artifacts."""
import argparse
import cmath
import hashlib
import json
import math
from pathlib import Path
import struct

RATES = (8000, 44100, 48000, 96000, 192000, 384000)

def require(condition, message):
    if not condition:
        raise ValueError(message)

def coefficient(hz, rate):
    t = math.tan(math.pi * min(hz, .45 * rate) / rate)
    return max(-.99, min(.99, (t - 1) / (t + 1)))

def floats(root, filename, expected_hash):
    path = (root / filename).resolve()
    require(path.parent == root.resolve(), "artifact outside output directory")
    data = path.read_bytes()
    require(hashlib.sha256(data).hexdigest() == expected_hash, "artifact hash mismatch")
    require(len(data) % 4 == 0, "invalid float byte count")
    values = struct.unpack('<' + 'f' * (len(data) // 4), data)
    require(all(math.isfinite(x) for x in values), "nonfinite artifact")
    return values

def audit_case(row, root):
    rate, hz, mix = row['rate'], row['frequency_hz'], row['mix']
    a = coefficient(hz, rate)
    require(abs(a - row['coefficient']) < 1e-13, "coefficient mapping mismatch")
    data = floats(root, row['file'], row['sha256'])
    require(len(data) == row['frames'], "frame count mismatch")
    q = abs(a)
    rho = (1 + q) / 2
    log_c = 32 * math.log((1 - q * rho) / (rho - q))
    peak = math.exp(log_c + len(data) * math.log(rho))
    l1 = peak / (1 - rho)
    moment = peak * (len(data) * (1 - rho) + rho) / (1 - rho) ** 2
    require(l1 <= 1e-8 and moment <= 1e-4, "insufficient impulse capture")
    require(row['reported_latency_samples'] == 0, "latency policy mismatch")
    require(row['peak_after_one_second'] < 1e-6, "one-second tail gate failed")
    after_second = data[int(rate):]
    if after_second:
        require(max(abs(x) for x in after_second) < 1e-6 and peak < 1e-6,
                'raw response or remaining tail exceeds one-second gate')
    else:
        require(math.exp(log_c + rate*math.log(rho)) < 1e-6,
                'one-second tail not justified by independent contour bound')
    require(row['partition_bit_exact'] is True, "partition repeat failed")
    if abs(a) < 1e-15:
        for n, value in enumerate(data):
            expected_sample = (1-mix)*(1 if n == 0 else 0) + mix*(1 if n == 32 else 0)
            require(abs(value-expected_sample) < 2e-6, 'pure-delay impulse mismatch')
    require(len(row['spectrum']) == 129, "incomplete frequency grid")
    for index, bin_ in enumerate(row['spectrum']):
        w = math.pi * index / 128
        require(abs(w - bin_['radians']) < 1e-12, "frequency grid mismatch")
        z = cmath.exp(-1j * w)
        expected = (1 - mix) + mix * ((a + z) / (1 + a * z)) ** 32
        measured = complex(bin_['real'], bin_['imag'])
        require(abs(expected - measured) <= 2e-4, "complex response gate failed")
        # Independently recalculate selected raw-data DFT bins, including both endpoints.
        if index in (0, 1, 16, 64, 127, 128):
            phase, h, nh = 1 + 0j, 0j, 0j
            for n, value in enumerate(data):
                h += value * phase
                nh += n * value * phase
                phase *= z
            require(abs(h - measured) < 2e-9, "raw DFT does not match reported measurement")
            if mix == 1:
                gd = (nh / h).real
                reference = 32 * (1 - a*a) / (1 + 2*a*math.cos(w) + a*a)
                require(abs(gd - reference) <= max(.02, .001 * reference), "raw group delay gate failed")
        if mix == 1:
            require(abs(20 * math.log10(abs(measured))) <= .01, "wet magnitude gate failed")
            reference = 32 * (1 - a*a) / (1 + 2*a*math.cos(w) + a*a)
            require(abs(bin_['group_delay_samples'] - reference) <= max(.02, .001 * reference), "group delay gate failed")

class Ramp:
    def __init__(self, value, frames):
        self.value = self.target = value
        self.frames, self.left, self.step = frames, 0, 0

    def set(self, target):
        if target != self.target:
            self.target, self.left = target, self.frames
            self.step = (target - self.value) / self.frames

    def next(self):
        if self.left:
            self.left -= 1
            self.value = self.target if self.left == 0 else self.value + self.step
        return self.value

def audit_automation(row, root):
    rate = row['rate']
    source = floats(root, row['source_file'], row['source_sha256'])
    wet = floats(root, row['wet_file'], row['wet_sha256'])
    require(len(source) == len(wet) == int(rate * .3), "automation frame count mismatch")
    a, mix, bypass = Ramp(coefficient(row['initial_frequency_hz'], rate), int(rate*.1)), Ramp(1, int(rate*.01)), Ramp(0, int(rate*.01))
    states = [0.] * 32
    events = {event['frame']: event for event in row['events']}
    require(len(events) == 3, "automation events missing")
    error = 0.
    for n, x in enumerate(source):
        if n in events:
            event = events[n]
            a.set(coefficient(event['frequency_hz'], rate))
            mix.set(event['mix'])
            bypass.set(float(event['bypass']))
        current_a, m, b = a.next(), mix.next(), bypass.next()
        g = math.sqrt(1 - current_a * current_a)
        value = x
        for stage, state in enumerate(states):
            output = current_a * value + g * state
            states[stage] = g * value - current_a * state
            value = output
        expected = x + m * (1-b) * (value-x)
        error = max(error, abs(wet[n] - expected))
    require(error <= 2e-6, f"independent automated lattice error {error}")
    return error

def audit(root):
    report = json.loads((root/'result.json').read_text())
    require(report['schema'] == 1 and report['kind'] == 'frequency-dispersion-measurements', "wrong schema")
    expected = {(fs, hz, mix) for fs in RATES for hz in (20, 500, 20000) for mix in (.5, 1)}
    expected.update((fs, fs/4, mix) for fs in RATES if fs/4 <= 20000 for mix in (.5,1))
    actual = {(r['rate'], r['frequency_hz'], r['mix']) for r in report['cases']}
    require(actual == expected and len(report['cases']) == len(expected), "incomplete or duplicate matrix")
    for row in report['cases']:
        audit_case(row, root)
    require({r['rate'] for r in report['automation']} == set(RATES) and len(report['automation']) == 6, "incomplete automation matrix")
    error = max(audit_automation(row, root) for row in report['automation'])
    require(report['listening_approved'] is False, "generated fixtures cannot claim human listening")
    require(len(report['examples']) == 36, "missing listening fixtures")
    for row in report['examples']:
        data = (root / row['wet_file']).read_bytes()
        require(hashlib.sha256(data).hexdigest() == row['wet_sha256'], "WAV hash mismatch")
        require(data[:4] == b'RIFF' and data[8:12] == b'WAVE', "invalid WAV")
        if row['source_file'] == 'two-tone-source.wav':
            values = struct.unpack('<' + 'f' * ((len(data)-44)//4), data[44:])
            a = coefficient(row['frequency_hz'], 48000)
            components = []
            for hz in (997, 1499):
                w = 2*math.pi*hz/48000
                z = cmath.exp(-1j*w)
                h = 1-row['mix'] + row['mix']*((a+z)/(1+a*z))**32
                components.append((w, h))
            # After settling, every sample must match the original two frequencies
            # with only the independently predicted amplitude/phase changes.
            for n in range(48000, len(values)):
                expected_sample = sum(.2*(h*cmath.exp(1j*w*n)).imag for w,h in components)
                require(abs(values[n]-expected_sample) < 2e-6, 'steady two-tone gained unpredicted components')
    return {'passed': True, 'static_cases': len(expected), 'automation_error_max': error, 'human_listening_pending': True}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(audit(args.directory), indent=2))
    except (ValueError, KeyError, OSError) as error:
        raise SystemExit(str(error))
