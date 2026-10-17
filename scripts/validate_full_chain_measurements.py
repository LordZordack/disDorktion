"""Audit full-chain captures/raw CPU timings with only the Python standard library.

No listening or reference CPU qualification is inferred from these diagnostics.
"""
import argparse
import array
import cmath
import csv
import hashlib
import json
import math
from pathlib import Path
import sys


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def finite_tree(value):
    if isinstance(value, float):
        require(math.isfinite(value), "nonfinite JSON")
    elif isinstance(value, dict):
        for item in value.values():
            finite_tree(item)
    elif isinstance(value, list):
        for item in value:
            finite_tree(item)


def checked_file(root, name, expected):
    path = (root / name).resolve()
    require(path.is_relative_to(root.resolve()), "path escapes artifact directory")
    require(digest(path) == expected, f"hash mismatch: {name}")
    return path


def audit_provenance(report, executable, source_root):
    provenance = report["provenance"]
    require(digest(executable) == provenance["executable_sha256"], "executable hash mismatch")
    sources = provenance["sources"]
    require(len(sources) >= 10, "incomplete source provenance")
    names = [item["path"] for item in sources]
    require(len(names) == len(set(names)), "duplicate source provenance")
    for required in ("src/dsp/FullChain.cpp", "src/dsp/FullChain.h", "tools/FullChainQualification.h"):
        require(required in names, "missing full-chain source identity")
    for item in sources:
        checked_file(source_root, item["path"], item["sha256"])


def max_error(a, b):
    return max((abs(x-y) for x, y in zip(a, b)), default=0.)


def audit_latency(root, checks):
    require(len(checks) == 4, "incomplete independent latency matrix")
    seen, results = set(), []
    for case in checks:
        rate, channels, frames = case["rate"], case["channels"], case["frames"]
        require(rate in (48000,96000) and channels in (1,2) and frames == 4096, "invalid latency configuration")
        key = (rate,channels)
        require(key not in seen, "duplicate latency check")
        seen.add(key)
        require(case["lanes"] == "source,bypassed_wet", "invalid latency lanes")
        settings = case["settings"]
        expected_settings = {
            "input.gain.gainDb": 0, "input.gain.mute": False, "input.gain.bypass": True,
            "input.crusher.bitDepth": 64, "input.crusher.captureRateHz": 384000, "input.crusher.bypass": True,
            "input.crusher.mix": 1, "input.crusher.crush": 0, "input.crusher.shape": 0,
            "input.crusher.jitter": 0, "input.crusher.jitterSeed": 1,
            "middle.tanh.driveDb": 0, "middle.tanh.outputDb": 0, "middle.tanh.bypass": True,
            "middle.folder.driveDb": 0, "middle.folder.threshold": 1, "middle.folder.outputDb": 0, "middle.folder.bypass": True,
            "output.highPass.cutoffHz": 20, "output.lowPass.cutoffHz": 20000,
            "output.highPass.response": 0, "output.lowPass.response": 0,
            "output.highPass.bypass": True, "output.lowPass.bypass": True,
            "finalGain.gainDb": 0, "finalGain.mute": False, "finalGain.bypass": False, "mix": 1, "bypass": False,
        }
        require(settings == expected_settings, "latency controls differ from bypassed neutral fixture")
        require(len(settings) == 29 and settings["mix"] == 1 and settings["bypass"] is False
                and settings["finalGain.gainDb"] == 0 and settings["finalGain.mute"] is False, "invalid latency settings")
        for field in ("input.gain.bypass", "input.crusher.bypass", "middle.tanh.bypass", "middle.folder.bypass", "output.highPass.bypass", "output.lowPass.bypass"):
            require(settings[field] is True, "latency path effect active")
        values = array.array("f")
        values.frombytes(checked_file(root,case["file"],case["sha256"]).read_bytes())
        if sys.byteorder != "little":
            values.byteswap()
        require(len(values) == case["sample_count"] == 2*channels*frames, "invalid latency sample count")
        require(all(math.isfinite(v) for v in values), "nonfinite latency capture")
        delay = case["latency_samples"]
        require(isinstance(delay,int) and 0 < delay < 256, "invalid latency report")
        omega = 1e-5
        phases = [cmath.exp(-1j*omega*n) for n in range(frames)]
        for channel in range(channels):
            src = values[channel*frames:(channel+1)*frames]
            wet = values[(channels+channel)*frames:(channels+channel+1)*frames]
            amplitude = -.73 if channel else 1.
            require(abs(src[0]-amplitude) < 1e-7 and all(v == 0 for v in src[1:]), "invalid latency impulse")
            mass = math.fsum(wet)
            require(abs(mass) > .1, "missing impulse response")
            centroid = math.fsum(n*v for n,v in enumerate(wet))/mass
            positive = sum(v*p for v,p in zip(wet,phases))
            # +/-omega phase difference estimates DC group delay; negative
            # channel amplitude cancels in the conjugate product.
            negative = positive.conjugate()
            group_delay = -cmath.phase(positive*negative.conjugate())/(2*omega)
            require(abs(centroid-delay) <= .02, f"measured signed centroid differs from reported delay: {centroid} vs {delay}")
            require(abs(group_delay-delay) <= .02, f"measured wet group delay differs from report: {group_delay} vs {delay}")
            require(max(abs(v) for v in wet[-256:]) < 1e-8, "latency impulse truncated")
            results.append(dict(rate=rate,channels=channels,channel=channel,reported_samples=delay,
                                signed_centroid_samples=centroid,dc_group_delay_samples=group_delay))
    return results


def audit_measurements(root, report):
    cases = report["cases"]
    require(len(cases) == 272, "incomplete measurement matrix")
    require(report["quality"] == "4x-fir-adaa-v1", "unexpected quality")
    require(report["lanes"] == "source,dry,wet,mix25,mix50,mix75,ordered,common_bypass,variable_partition,dry_changed_drive,wet_final_gain_6db", "unexpected lane schema")
    require(report["sample_format"] == "little-endian IEEE754 float32 planar lane/channel/frame", "unexpected sample format")
    seen = set()
    errors = dict(dry=0., blend=0., ordered=0., bypass=0., partition=0., drive_independence=0., final_gain=0., source=0.)
    spectra = []
    max_peak = 0.
    for index, case in enumerate(cases):
        rate, channels, frames = case["rate"], case["channels"], case["frames"]
        kind, mask, category = case["source_kind"], case["bypass_mask"], case["matrix_kind"]
        require(case["id"] == index, "missing/reordered case")
        require(rate in (48000, 96000) and channels in (1, 2), "unsupported matrix entry")
        require(category in ("sources", "bypasses"), "unexpected category")
        require((category == "sources" and kind in range(4) and mask == 0 and frames == 8192)
                or (category == "bypasses" and kind == 2 and mask in range(64) and frames == 1024), "bad source matrix")
        key = (rate, channels, category, kind if category == "sources" else mask)
        require(key not in seen, "duplicate case")
        seen.add(key)
        settings = case["settings"]
        require(len(settings) == 29, "incomplete settings")
        expected_settings = {
            "input.gain.gainDb": 9, "input.gain.mute": False,
            "input.crusher.bitDepth": 8, "input.crusher.captureRateHz": 12345.5,
            "input.crusher.mix": 1, "input.crusher.crush": .35, "input.crusher.shape": .2,
            "input.crusher.jitter": .1, "input.crusher.jitterSeed": 1,
            "middle.tanh.driveDb": 12, "middle.tanh.outputDb": 3,
            "middle.folder.driveDb": 6, "middle.folder.threshold": .4, "middle.folder.outputDb": 0,
            "output.highPass.cutoffHz": 120, "output.lowPass.cutoffHz": 8000,
            "output.highPass.response": 0, "output.lowPass.response": 0,
            "finalGain.gainDb": 0, "finalGain.mute": False, "finalGain.bypass": False,
            "mix": 1, "bypass": False,
        }
        for field, expected_value in expected_settings.items():
            require(isinstance(settings[field], (int,float)) and abs(settings[field]-expected_value) < 1e-6, "measurement settings mismatch")
        for bit, field in enumerate(("input.gain.bypass", "input.crusher.bypass", "middle.tanh.bypass", "middle.folder.bypass", "output.highPass.bypass", "output.lowPass.bypass")):
            require(settings[field] == bool(mask & (1 << bit)), "bypass settings mismatch")
        require(settings["mix"] == 1 and settings["bypass"] is False and settings["finalGain.gainDb"] == 0, "unexpected measurement endpoint")
        path = checked_file(root, case["file"], case["sha256"])
        values = array.array("f")
        values.frombytes(path.read_bytes())
        if sys.byteorder != "little":
            values.byteswap()
        size = channels * frames
        require(len(values) == case["sample_count"] == size * 11, "capture sample count mismatch")
        require(all(math.isfinite(x) for x in values), "nonfinite captured sample")
        lanes = [values[i*size:(i+1)*size] for i in range(11)]
        for channel in range(channels):
            for n in range(frames):
                t = n/rate
                if kind == 0:
                    x = 1. if n == 0 else 0.
                elif kind == 1:
                    duration, k = frames/rate, math.log(rate*.45/20.)
                    x = .35*math.sin(2*math.pi*20*duration/k*(math.exp(k*t/duration)-1))
                elif kind == 2:
                    x = .3*math.sin(2*math.pi*997*t)+.2*math.sin(2*math.pi*7001*t)
                else:
                    x = 4*math.sin(2*math.pi*311*t) if n < frames//4 else 0.
                errors["source"] = max(errors["source"], abs(lanes[0][channel*frames+n]-x*(-.73 if channel else 1.)))
        errors["final_gain"] = max(errors["final_gain"], max(abs(y-x*10**(.3)) for x,y in zip(lanes[2],lanes[10])))
        require(abs(case["wet_endpoint_peaks"]["output"]-max(abs(x) for x in lanes[2])) < 1e-6, "wet endpoint peak mismatch")
        peak = max(abs(x) for x in values)
        require(abs(peak-case["peak_all_lanes"]) < 1e-6, "peak mismatch")
        max_peak = max(max_peak, peak)
        delay = case["latency_samples"]
        require(isinstance(delay, int) and 0 < delay < 256, "invalid reported latency")
        expected = array.array("f")
        for channel in range(channels):
            expected.extend([0.] * delay)
            expected.extend(lanes[0][channel*frames:channel*frames+frames-delay])
        errors["dry"] = max(errors["dry"], max_error(lanes[1], expected))
        for lane, mix in zip(lanes[3:6], (.25, .5, .75)):
            errors["blend"] = max(errors["blend"], max(abs(y-((1-mix)*d+mix*w)) for d, w, y in zip(lanes[1], lanes[2], lane)))
        for name, left, right in (("ordered", 2, 6), ("bypass", 1, 7), ("partition", 2, 8), ("drive_independence", 1, 9)):
            errors[name] = max(errors[name], max_error(lanes[left], lanes[right]))
        if category == "sources" and kind == 0:
            for frequency in (40., 1000., 8000., rate*.4):
                phases = [cmath.exp(-2j*math.pi*frequency*n/rate) for n in range(frames)]
                projections = [sum(x*p for x, p in zip(lane[:frames], phases)) for lane in lanes[1:6]]
                for mix, measured in zip((.25, .5, .75), projections[2:]):
                    predicted = (1-mix)*projections[0]+mix*projections[1]
                    require(abs(predicted-measured) < 3e-4, "complex blend sum mismatch")
                    spectra.append(dict(case=index, frequency=frequency, mix=mix, real=measured.real, imaginary=measured.imag, phase_radians=cmath.phase(measured), magnitude=abs(measured)))
    for name, error in errors.items():
        require(error <= (3e-5 if name in ("ordered", "partition", "blend", "final_gain", "source") else 0.), f"{name} error: {error}")
    latency_results = audit_latency(root, report["latency_checks"])
    return dict(cases=len(cases), errors=errors, peak_all_captures=max_peak, complex_blends=spectra, independent_wet_latency=latency_results,
                human_listening="pending", reference_cpu_qualified=False)


def audit_cpu(root, report):
    require(report["reference_qualified"] is False, "diagnostic cannot qualify reference")
    require(report["provenance"]["build_configuration"] == "Release" and report["provenance"]["ndebug"] is True,
            "CPU budget diagnostics require a Release build")
    checked_file(root, "timings.csv", report["timings_sha256"])
    checked_file(root, "summary.csv", report["summary_sha256"])
    groups = {}
    with (root/"timings.csv").open(newline="") as stream:
        for row in csv.DictReader(stream):
            case = int(row["case"])
            values = groups.setdefault(case, [])
            require(int(row["iteration"]) == len(values), "missing raw timing")
            elapsed = int(row["elapsed_ns"])
            require(elapsed >= 0, "negative raw timing")
            require(all(math.isfinite(float(row[field])) for field in ("mix", "input_gain_db", "folder_drive_db")), "nonfinite control")
            config = report["cases"][case]
            iteration = len(values)
            scenario = config["scenario"]
            initial = config["initial_settings"]
            target = iteration % 101 / 100
            mix = target if scenario in ("automation", "bypass") else initial["mix"]
            bypass = bool((iteration//8)%2) if scenario == "bypass" else initial["bypass"]
            gain = -60+84*target if scenario == "automation" else initial["input.gain.gainDb"]
            drive = -24+60*target if scenario == "automation" else initial["middle.folder.driveDb"]
            require(abs(float(row["mix"])-mix) < 1e-5 and int(row["bypass"]) == int(bypass)
                    and abs(float(row["input_gain_db"])-gain) < 1e-5
                    and abs(float(row["folder_drive_db"])-drive) < 1e-5, "raw control schedule mismatch")
            values.append(elapsed)
    with (root/"summary.csv").open(newline="") as stream:
        summaries = list(csv.DictReader(stream))
    require(len(summaries) == len(report["cases"]) == len(groups) == 120, "incomplete CPU matrix")
    seen, worst = set(), 0.
    for index, (row, config) in enumerate(zip(summaries, report["cases"])):
        require(int(row["case"]) == config["id"] == index, "bad CPU case identity")
        rate, channels, capacity, frames = (int(row[k]) for k in ("rate", "channels", "capacity", "frames"))
        scenario = row["scenario"]
        require(rate in (48000,96000) and channels in (1,2) and capacity in (32,64,127,256,1024), "bad CPU matrix")
        require(scenario in ("neutral","extreme","automation","bypass","oversized","tail"), "bad scenario")
        key = (rate,channels,capacity,scenario)
        require(key not in seen, "duplicate CPU case")
        seen.add(key)
        require(frames == (capacity*2+3 if scenario == "oversized" else capacity), "wrong incoming deadline")
        for field in ("rate", "channels", "capacity", "frames", "scenario"):
            require((config[field] == row[field]) if field == "scenario" else (float(config[field]) == float(row[field])), "CPU config mismatch")
        values = sorted(groups[index])
        require(len(values) == int(row["count"]) == (1000 if report["quick"] else 10000), "incomplete timings")
        p99 = values[math.ceil(.99*len(values))-1]
        require(p99 == int(row["p99_ns"]) and values[len(values)//2] == int(row["p50_ns"]) and values[-1] == int(row["max_ns"]), "raw percentile mismatch")
        deadline = frames/rate*1e9
        fraction = p99/deadline
        require(abs(float(row["deadline_ns"])-deadline) < 1e-6 and abs(float(row["p99_deadline_fraction"])-fraction) < 1e-12, "deadline statistic mismatch")
        require(int(row["within_budget"]) == int(fraction < .25), "budget flag mismatch")
        require(int(row["processing_ok"]) == 1 and fraction < .25, "CPU diagnostic failed")
        worst = max(worst, fraction)
    require(report["processing_and_diagnostic_budget_ok"] is True, "diagnostic failure flag")
    return dict(cases=120, raw_timings=sum(map(len,groups.values())), worst_p99_deadline_fraction=worst, reference_cpu_qualified=False)


def audit(directory, executable, source_root):
    root = Path(directory)
    report = json.loads((root/"result.json").read_text())
    finite_tree(report)
    require(report["schema"] == 1, "unsupported schema")
    audit_provenance(report, executable, Path(source_root))
    if report["kind"] == "full-chain-measurements":
        result = audit_measurements(root, report)
    elif report["kind"] == "full-chain-cpu-diagnostic":
        result = audit_cpu(root, report)
    else:
        raise ValueError("unexpected report kind")
    result.update(validator_sha256=digest(__file__), input_manifest_sha256=digest(root/"result.json"), passed=True)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory")
    parser.add_argument("--executable", required=True)
    parser.add_argument("--source-root", default=str(Path(__file__).resolve().parents[1]))
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        result = audit(args.directory, args.executable, args.source_root)
        with Path(args.output).open("x") as stream:
            json.dump(result, stream, indent=2, allow_nan=False)
            stream.write("\n")
        print(json.dumps({k:v for k,v in result.items() if k != "complex_blends"}))
    except (ValueError, KeyError, OSError, TypeError, IndexError, OverflowError, ZeroDivisionError) as error:
        print(f"audit failed: {error}", file=sys.stderr)
        sys.exit(1)
