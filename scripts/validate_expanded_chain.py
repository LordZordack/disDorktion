"""Independent raw CPU/stress audit. Never infers listening or host acceptance."""
import argparse
import array
import csv
import hashlib
import itertools
import json
import math
import re
import sys
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def checked(root, name, expected):
    path = (root / name).resolve()
    require(path.is_relative_to(root.resolve()), "path escapes evidence root")
    require(digest(path) == expected, f"hash mismatch: {name}")
    return path


def audit_case(raw, config, summary, session_power=None):
    """Recompute statistics and schedule coverage, rather than trusting flags."""
    samples, residues, settled, decay_phases = [], [0] * 101, [0, 0], [0] * 4
    previous = -1
    scenario = config["scenario"]
    for row in raw:
        index, ns, residue, bypass = (int(row[k]) for k in ("iteration", "elapsed_ns", "residue", "bypass"))
        require(index > previous and ns >= 0 and residue == index % 101, "invalid raw schedule")
        expected = int(scenario == "warm-bypass" or scenario in ("automation", "bypass") and (index // config["dwell"]) % 2 != 0)
        require(bypass == expected, "bypass schedule mismatch")
        if index % config["dwell"] >= config["ramp"]:
            settled[bypass] += 1
        residues[residue] += 1
        if scenario == "quiet-decay":
            frame = index * config["frames"] % (31 * int(config["rate"]))
            phase = 0 if frame < config["rate"] else 1 if frame < 2 * config["rate"] else 2 if frame < 20 * config["rate"] else 3
            decay_phases[phase] += 1
        samples.append(ns)
        previous = index
    require(samples, "missing timings")
    samples.sort()
    p99 = samples[math.ceil(.99 * len(samples)) - 1]
    deadline = config["frames"] / config["rate"] * 1e9
    fraction = p99 / deadline
    require(len(samples) == int(summary["count"]), "count mismatch")
    require(p99 == int(summary["p99_ns"]), "p99 mismatch")
    require(math.isclose(fraction, float(summary["fraction"]), abs_tol=1e-12), "fraction mismatch")
    require(math.isclose(deadline, float(summary["deadline_ns"]), abs_tol=1e-6), "deadline mismatch")
    coverage = 100000 <= len(samples) <= 200000
    coverage &= scenario != "automation" or min(residues) >= 100
    coverage &= scenario != "bypass" or min(settled) >= 100
    coverage &= scenario != "quiet-decay" or min(decay_phases) >= 100
    require(coverage == bool(int(summary["coverage_ok"])), "coverage flag mismatch")
    power = config["ac_before"] and config["ac_after"] and config["power_before"] == config["power_after"]
    power &= session_power is None or config["power_before"] == session_power
    require(power == bool(int(summary["power_stable"])), "power flag mismatch")
    require(config["frames"] == (2 * config["capacity"] + 3 if scenario == "oversized" else config["capacity"]), "frames mismatch")
    require(config["latency_samples"] == 92, "latency changed")
    controls = config["initial_settings"]
    for key in ("combBypass", "dispersionBypass", "toneBypass"):
        require(controls[key] == (1 if scenario == "warm-bypass" else 0), "new effects not configured")
    require(controls["combFeedback"] > .94 and controls["combDelayMs"] == 40, "missing resonance")
    return {"id": config["id"], "count": len(samples), "p99_fraction": fraction,
            "budget_ok": fraction < .25, "coverage_ok": coverage, "power_ok": bool(power),
            "processing_ok": bool(int(summary["processing_ok"]))}


def audit_provenance(report, executable, source_root):
    provenance = report["provenance"]
    require(digest(executable) == provenance["executable_sha256"], "executable mismatch")
    names = set()
    for item in provenance["sources"]:
        checked(source_root, item["path"], item["sha256"])
        require(item["path"] not in names, "duplicate source")
        names.add(item["path"])
    require({"tools/ExpandedChainQualification.cpp", "src/dsp/FullChain.cpp", "src/dsp/OutputSection.cpp"} <= names, "incomplete provenance")


def audit_stress_capture(path, case):
    require(case["latency_samples"] == 92, "latency changed")
    require(path.stat().st_size == case["frames"] * 2 * 4, "capture length mismatch")
    peak, residual, offset = 0., 0., 0
    quiet_start = case["frames"] - int(case["rate"] / 10)
    with path.open("rb") as f:
        while data := f.read(8 * 32768):
            values = array.array("f")
            values.frombytes(data)
            if sys.byteorder != "little":
                values.byteswap()
            left, right = values[::2], values[1::2]
            require(all(math.isfinite(v) for v in values), "nonfinite stress samples")
            require(all(v == 0 for v in right), "channel leakage")
            peak = max(peak, max(map(abs, left), default=0.))
            if offset + len(left) > quiet_start:
                residual = max(residual, max(map(abs, left[max(0, quiet_start - offset):]), default=0.))
            offset += len(left)
    require(math.isclose(peak, case["peak"], abs_tol=1e-12), "stress peak mismatch")
    require(math.isclose(residual, case["residual_after_20s"], abs_tol=1e-16), "stress residual mismatch")
    require(peak < case["peak_limit"] == 8192 and case["finite"] and case["channel_isolated"], "stress gate failed")
    return True


def audit_decay_trace(path, config):
    expected_frame, durations = 0, []
    with path.open(newline="") as f:
        for row in csv.DictReader(f):
            require(int(row["frame"]) == expected_frame, "noncontiguous decay trace")
            duration, peak = int(row["elapsed_ns"]), float(row["peak"])
            require(duration >= 0 and math.isfinite(peak) and peak >= 0, "invalid decay trace")
            durations.append(duration)
            expected_frame += config["frames"]
    require(expected_frame == math.ceil(31 * config["rate"] / config["frames"]) * config["frames"], "incomplete decay trace")
    return len(durations)


def audit_stress(root, report):
    cases = report["cases"]
    expected = set(itertools.product((8000, 44100, 48000, 96000, 192000, 384000), (-1, 1), (False, True), (False, True)))
    actual = {(c["rate"], -1 if c["feedback"] < 0 else 1, c["dynamic"], c["upstream_bypassed"]) for c in cases}
    require(actual == expected and len(cases) == 48, "incomplete stress matrix")
    for c in cases:
        require(c["frames"] == 21 * c["rate"] and c["layout"] == "stereo-interleaved-float32-le", "stress format mismatch")
        controls = c["initial_settings"]
        require(controls["inputGainDb"] == 24, "upstream drive missing")
        for field in ("crusherBypass", "tanhBypass", "folderBypass"):
            require(controls[field] == int(c["upstream_bypassed"]), "upstream bypass mismatch")
        audit_stress_capture(checked(root, c["capture_file"], c["capture_sha256"]), c)
    require(report["passed"], "collector stress gate failed")
    return {"stress_passed": True, "cases": len(cases), "maximum_peak": max(c["peak"] for c in cases),
            "maximum_residual_after_20s": max(c["residual_after_20s"] for c in cases)}


def validate_matrix(configs, runs, complete):
    expected = list(itertools.product(range(runs), (48000, 96000), (32, 64, 127, 256, 1024),
                                     ("warm-bypass", "active-static", "automation", "bypass", "oversized", "quiet-decay")))
    actual = [(c["run"], c["rate"], c["capacity"], c["scenario"]) for c in configs]
    require(actual and actual == expected[:len(actual)], "CPU matrix is not an ordered prefix")
    require(not complete or actual == expected, "incomplete CPU matrix")
    require(all(c["id"] == i for i, c in enumerate(configs)), "case ordering mismatch")


def audit_method(report):
    """Recompute declared process-policy gates; preserve pre-method archives."""
    if "method_id" not in report:
        new_fields = {"process_policy", "affinity_verified", "collection_policy_ok", "scheduling_before",
                      "scheduling_after", "scheduling_after_restore", "policy_ok", "method_id"}
        require(not new_fields.intersection(report)
                and all(not new_fields.intersection(c) for c in report["cases"]),
                "method identity missing for policy evidence")
        return True
    method = report["method_id"]
    require(method in ("windows-system-managed-v2", "windows-explicit-highqos-v1"), "unknown measurement method")
    require(all(c.get("method_id") == method for c in report["cases"]), "mixed case measurement methods")
    try:
        policy = report["process_policy"]
        if method == "windows-system-managed-v2":
            require(policy["requested"] is False and policy["set_attempted"] is False
                    and policy["set_succeeded"] is False and policy["restore_attempted"] is False
                    and policy["restore_succeeded"] is False, "default method policy was mutated")
            require(policy["application_ok"] is True and policy["restoration_ok"] is True
                    and report["collection_policy_ok"] is True
                    and all(c["policy_ok"] is True for c in report["cases"]), "default policy claim mismatch")
            return True
        require("tools/ExpandedProcessPolicy.h" in {s["path"] for s in report["provenance"]["sources"]},
                "missing process policy source provenance")

        def process(snapshot):
            flags = snapshot["process_power_throttling"]
            if flags["available"] is not True or flags["error"] != 0:
                return None
            values = tuple(flags[k] for k in ("version", "control_mask", "state_mask"))
            require(values[0] == 1 and all(type(v) is int and 0 <= v <= 0xffffffff for v in values),
                    "invalid process policy flags/version")
            return values

        def affinity(snapshot):
            # Thread QoS queries may be unavailable; the actual group affinity must be readable.
            thread = snapshot["thread_power_throttling"]
            require(type(thread["available"]) is bool, "invalid thread policy availability")
            if thread["available"]:
                require(thread["error"] == 0 and thread["version"] == 1
                        and all(type(thread[k]) is int and 0 <= thread[k] <= 0xffffffff
                                for k in ("version", "control_mask", "state_mask")), "invalid thread policy flags")
            else:
                require((type(thread.get("reason")) is str and bool(thread["reason"]))
                        or (type(thread.get("error")) is int and 0 <= thread["error"] <= 0xffffffff),
                        "missing unavailable thread policy reason/error")
            value = snapshot["thread_group_affinity"]
            if value["available"] is not True or value["error"] != 0:
                return None
            group, mask = value["group"], int(value["mask"], 16)
            number = value["observed_processor_number"]
            if (type(group) is not int or not 0 <= group <= 65535 or not 0 < mask < 2 ** 64
                    or type(number) is not int or not 0 <= number < 64
                    or value["observed_processor_group"] != group or not mask & (1 << number)):
                return None
            return group, mask

        before, applied, restored = (policy[k] for k in ("before_request", "after_request", "after_restore"))
        require(report["scheduling_before"] == before and report["scheduling_after_restore"] == restored,
                "process policy snapshot mismatch")
        saved = process(before)
        desired = None if saved is None else (saved[0], saved[1] | 1, saved[2] & ~1)
        expected_affinity = affinity(applied)
        application = (policy["requested"] is True and policy["set_attempted"] is True
                       and policy["set_succeeded"] is True and policy["set_error"] == 0
                       and desired is not None and process(applied) == desired
                       and expected_affinity is not None and affinity(before) == expected_affinity)
        require(application == policy["application_ok"], "process policy application claim mismatch")
        restoration = (policy["restore_attempted"] is True and policy["restore_succeeded"] is True
                       and policy["restore_error"] == 0 and saved is not None and process(restored) == saved
                       and affinity(before) is not None and affinity(restored) == affinity(before))
        require(restoration == policy["restoration_ok"], "process policy restoration claim mismatch")
        selected = re.search(r"(?:^| )selected_group=(\d+) selected_mask=(0x[0-9a-fA-F]+)(?: |$)", report["affinity"])
        pinned_affinity = (report["pinned"] is True and selected is not None and expected_affinity is not None
                           and expected_affinity == (int(selected[1]), int(selected[2], 16)))
        require(pinned_affinity == report["affinity_verified"], "process policy affinity claim mismatch")
        final_snapshot = report["scheduling_after"]
        collection = (application and process(final_snapshot) == desired and expected_affinity is not None
                      and affinity(final_snapshot) == expected_affinity)
        require(collection == report["collection_policy_ok"], "collection policy claim mismatch")
        cases_ok = True
        for case in report["cases"]:
            valid = application
            for key in ("scheduling_before", "scheduling_after"):
                snapshot = case[key]
                valid &= (desired is not None and process(snapshot) == desired
                          and expected_affinity is not None and affinity(snapshot) == expected_affinity)
            require(valid == case["policy_ok"], "case policy claim mismatch")
            cases_ok &= valid
        return bool(application and restoration and pinned_affinity and collection and cases_ok)
    except (KeyError, TypeError, OverflowError) as error:
        raise ValueError("missing or invalid process policy metadata") from error


def audit(root, executable, source_root):
    report = json.loads((root / ("stress.json" if (root / "stress.json").exists() else "result.json")).read_text())
    audit_provenance(report, executable, source_root)
    if report["kind"] == "expanded-chain-stress":
        return audit_stress(root, report)
    require(report["kind"] == "expanded-chain-cpu", "wrong evidence kind")
    method_ok = audit_method(report)
    provenance = report["provenance"]
    configs = report["cases"]
    runs = 1 if report["diagnostic"] else 3
    complete = report.get("complete", True)
    validate_matrix(configs, runs, complete)
    raw_path = checked(root, "timings.csv", report["timings_sha256"])
    summary_path = checked(root, "summary.csv", report["summary_sha256"])
    with summary_path.open(newline="") as f:
        summaries = list(csv.DictReader(f))
    require(len(summaries) == len(configs), "incomplete summary")
    results = []
    with raw_path.open(newline="") as f:
        groups = itertools.groupby(csv.DictReader(f), key=lambda r: int(r["case"]))
        for config, summary in zip(configs, summaries):
            case_id, rows = next(groups)
            require(case_id == config["id"] == int(summary["case"]), "case ordering mismatch")
            results.append(audit_case(rows, config, summary, report["power"]))
            if config["scenario"] == "quiet-decay":
                trace = checked(root, config["decay_file"], config["decay_sha256"])
                audit_decay_trace(trace, config)
        require(next(groups, None) is None, "extra timings")
    qualified = (complete and not report["quick"] and not report["diagnostic"] and report["runs"] == 3
                 and provenance["build_configuration"] == "Release" and report["pinned"]
                 and report["hybrid_established"] and report["on_ac"] and report["power"] != "unavailable"
                 and method_ok)
    qualified &= all(r["budget_ok"] and r["coverage_ok"] and r["power_ok"] and r["processing_ok"] for r in results)
    require(qualified == report["reference_qualified"], "reference qualification claim mismatch")
    return {"reference_qualified": bool(qualified), "complete": complete, "cases": results,
            "method_id": report.get("method_id", "windows-system-managed-v2"), "policy_ok": method_ok,
            "worst_p99_fraction": max(r["p99_fraction"] for r in results)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = audit(args.root, args.executable, args.source_root)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "cases"}))
