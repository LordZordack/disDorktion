"""Independent audit of middle-cpu-v1 raw evidence; no host/listening inference."""
import argparse
import csv
import hashlib
import importlib.util
import itertools
import json
import math
import subprocess
from pathlib import Path


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def checked(root, name, expected):
    root = Path(root).resolve()
    path = (root / name).resolve()
    require(path.is_relative_to(root), "path escapes evidence root")
    require(digest(path) == expected, f"hash mismatch: {name}")
    return path


def quantile(sorted_values, probability):
    require(sorted_values, "empty distribution")
    return sorted_values[math.ceil(probability * len(sorted_values)) - 1]


def validate_matrix(configs, runs, complete, filtered=False):
    require(type(runs) is int and runs in (1, 3), "invalid run count")
    expected = list(itertools.product(range(runs), (48000, 96000), (1, 2),
                                     (32, 64, 127, 256, 1024), range(10)))
    actual = [(c["run"], c["rate"], c["channels"], c["capacity"], c["scenario"])
              for c in configs]
    if filtered:
        require(not complete and len(configs) == 1 and actual[0] in expected,
                "invalid filtered matrix")
        require(expected.index(actual[0]) == configs[0]["id"], "filtered case id mismatch")
        return
    require(actual and actual == expected[:len(actual)], "matrix is not an ordered prefix")
    require(all(c["id"] == i for i, c in enumerate(configs)), "case id mismatch")
    require(not complete or actual == expected, "incomplete CPU matrix")


PHASE_BITS = (1, 2, 4, 8, 16, 32, 64)
SCENARIOS = ("tanh", "fold", "cascade", "extreme", "driver", "ramps",
             "off-toggle", "both-off", "mix-reactivate", "oversized")


def required_phases(scenario):
    return 63 if scenario == 8 else 2 if scenario == 7 else 32 if 4 <= scenario <= 6 else 64


def schedule(scenario, iteration, rate, frames):
    """Independent arithmetic implementation of the published schedule v1."""
    require(type(scenario) is int and 0 <= scenario < 10, "invalid scenario")
    ramp = int(rate * .01)
    dwell = (ramp + 188 + frames - 1) // frames + 8
    phase = 2 if scenario == 7 else 32 if 4 <= scenario <= 6 else 64
    if scenario == 8:
        epoch, offset = (iteration // dwell) % 3, (iteration % dwell) * frames
        intervals = (((0, ramp, 1), (ramp, dwell * frames, 2)),
                     ((0, 188, 4), (188, 188 + ramp, 8), (188 + ramp, dwell * frames, 16)),
                     ((0, ramp, 32), (ramp, dwell * frames, 16)))
        phase = sum(bit for start, end, bit in intervals[epoch]
                    if offset < end and offset + frames > start)
        if iteration < dwell:
            phase = 2
    fixture = (iteration // (dwell * 3 if scenario == 8 else dwell)) % 3
    return phase, fixture, iteration % 101


def audit_samples(rows, config):
    scenario, frames, rate = (config[k] for k in ("scenario", "frames", "rate"))
    window_ns = int(config["window_seconds"] * 1e9)
    samples, phases = [], {str(bit): [] for bit in PHASE_BITS}
    fixtures, residues = [0] * 3, [0] * 101
    previous_index, previous_wall, previous_slot, previous_end = config["start_iteration"] - 1, -1, -1, -1
    first_wall = None
    for row in rows:
        index, wall, duration, residue, phase, fixture = (
            int(row[k]) for k in ("iteration", "wall_ns", "duration_ns", "residue", "phase", "fixture"))
        require(index > previous_index, "raw iteration order mismatch")
        require(previous_wall < wall < window_ns and wall >= 0, "raw wall time mismatch")
        require(wall >= previous_end, "sampled callback overlap")
        slot = wall * 150000 // window_ns
        require(slot > previous_slot, "duplicate equal-wall-time sample slot")
        require(duration >= 0, "negative duration")
        if first_wall is None:
            require(index == config["start_iteration"], "first callback missing from raw samples")
            first_wall = wall
        require((phase, fixture, residue) == schedule(scenario, index, rate, frames),
                "raw schedule mismatch")
        require(index >= config["start_iteration"], "raw starts before collection")
        samples.append(duration)
        for bit in PHASE_BITS:
            if phase & bit:
                phases[str(bit)].append(duration)
        fixtures[fixture] += 1
        residues[residue] += 1
        previous_index, previous_wall, previous_slot = index, wall, slot
        previous_end = wall + duration
    require(samples, "missing raw timings")
    samples.sort()
    deadline = frames / rate * 1e9
    required = required_phases(scenario)
    phase_counts = {key: len(value) for key, value in phases.items()}
    class_p99 = {key: quantile(sorted(value), .99) if value else 0 for key, value in phases.items()}
    elapsed = config["collection_elapsed_ns"]
    require(type(elapsed) is int and elapsed >= previous_end, "impossible collection elapsed time")
    margin = max(1000000, window_ns // 100)
    collection_coverage = (elapsed >= window_ns and first_wall <= margin
                           and previous_wall >= window_ns - margin)
    coverage = (collection_coverage and 100000 <= len(samples) <= 200000 and min(fixtures) >= 100
                and all(phase_counts[str(bit)] >= 100 for bit in PHASE_BITS if required & bit)
                and (scenario not in (4, 5) or min(residues) >= 100))
    p99 = quantile(samples, .99)
    budget = p99 / deadline < .1 and all(
        phase_counts[key] > 0 and class_p99[key] / deadline < .1
        for key in map(str, (bit for bit in PHASE_BITS if required & bit)))
    return dict(count=len(samples), p50_ns=quantile(samples, .5),
                p95_ns=quantile(samples, .95), p99_ns=p99, max_ns=samples[-1],
                deadline_ns=deadline, p99_fraction=p99 / deadline,
                phase_counts=phase_counts, class_p99_ns=class_p99,
                fixture_counts=fixtures, residue_counts=residues,
                callback_budget_ok=budget, coverage_ok=coverage,
                collection_coverage_ok=collection_coverage,
                deadline_overruns=sum(ns > deadline for ns in samples))


def audit_batches(rows, config):
    rows = list(rows)
    require(len(rows) == 21 * 128, "incomplete batch collection")
    phases, fixtures, totals = {str(bit): 0 for bit in PHASE_BITS}, [0] * 3, [None] * 21
    for i, row in enumerate(rows):
        batch, block, index, duration, phase, fixture = (
            int(row[k]) for k in ("batch", "block", "iteration", "batch_elapsed_ns", "phase", "fixture"))
        require((batch, block, index) == (i // 128, i % 128, i + 128), "batch schedule mismatch")
        require(duration > 0, "invalid batch duration")
        expected_phase, expected_fixture, _ = schedule(config["scenario"], index, config["rate"], config["frames"])
        require((phase, fixture) == (expected_phase, expected_fixture), "batch state mismatch")
        if totals[batch] is None:
            totals[batch] = duration
        require(totals[batch] == duration, "inconsistent batch aggregate")
        for bit in PHASE_BITS:
            phases[str(bit)] += bool(phase & bit)
        fixtures[fixture] += 1
    fraction = quantile(sorted(total / 128 for total in totals), .95) / (config["frames"] / config["rate"] * 1e9)
    required = required_phases(config["scenario"])
    coverage = min(fixtures) >= 1 and all(phases[str(bit)] >= 1 for bit in PHASE_BITS if required & bit)
    return dict(batch_p95_fraction=fraction, batch_budget_ok=fraction < .1,
                batch_coverage_ok=coverage, batch_phase_counts=phases, batch_fixture_counts=fixtures)


def compare_claims(config, computed):
    for key, expected in computed.items():
        require(key in config, f"missing {key} claim")
        actual = config[key]
        if type(expected) is float:
            require(type(actual) in (float, int) and math.isfinite(actual)
                    and math.isclose(actual, expected, rel_tol=1e-12, abs_tol=1e-6),
                    f"{key} mismatch")
        else:
            require(type(actual) is type(expected) and actual == expected, f"{key} mismatch")


def affinity_value(snapshot):
    value = snapshot["thread_group_affinity"]
    if value["available"] is not True or value.get("error") != 0:
        return None
    group, mask = value["group"], int(value["mask"], 16)
    number = value["observed_processor_number"]
    require(type(group) is int and 0 <= group <= 65535 and 0 < mask < 2 ** 64,
            "invalid affinity group/mask")
    require(type(number) is int and 0 <= number < 64
            and value["observed_processor_group"] == group and mask & (1 << number),
            "observed processor outside affinity")
    return group, mask


def audit_affinity_restoration(report):
    before, restored = (affinity_value(report[k]) for k in ("affinity_before", "affinity_restored"))
    valid = before is not None and before == restored
    require(type(report["affinity_restoration_ok"]) is bool
            and report["affinity_restoration_ok"] == valid, "affinity restoration claim mismatch")
    return valid


def audit_case(raw, batches, config, session_power=None):
    for key in ("ac_before", "ac_after", "processing_ok", "replay_ok", "power_stable", "policy_ok"):
        require(type(config[key]) is bool, f"invalid {key}")
    require(config["frames"] == (2 * config["capacity"] + 3 if config["scenario"] == 9 else config["capacity"]),
            "incoming frames mismatch")
    require(config["scenario_name"] == SCENARIOS[config["scenario"]], "scenario name mismatch")
    require(config["latency_samples"] == 92 and config["warmup_frames"] == 188,
            "latency/warm-up contract changed")
    computed = audit_samples(raw, config)
    computed.update(audit_batches(batches, config))
    computed["power_stable"] = bool(config["ac_before"] and config["ac_after"]
                                   and config["power_before"] == config["power_after"]
                                   and (session_power is None or config["power_before"] == session_power))
    compare_claims(config, computed)
    require(type(config["processing_ok"]) is bool, "invalid processing claim")
    computed["processing_ok"] = config["processing_ok"]
    computed["replay_ok"] = config["replay_ok"]
    computed["windows_ok"] = (config["warmup_seconds"] >= 2 and config["probe_seconds"] >= 2
                              and 10 <= config["window_seconds"] <= 1200)
    return computed


REQUIRED_SOURCES = {"benchmarks/MiddleSectionBenchmark.cpp", "benchmarks/MiddleCpuSchedule.h",
                    "benchmarks/ExpandedTimingWindow.h", "tools/CombBenchmarkPlatform.h",
                    "tools/ExpandedProcessPolicy.h", "tools/FullChainQualification.h",
                    "src/dsp/MiddleSection.cpp", "src/dsp/MiddleSection.h",
                    "src/dsp/AdaaWaveFolder.cpp", "src/dsp/TanhSaturation.cpp",
                    "src/dsp/DriverCompensation.h", "scripts/validate_middle_cpu.py",
                    "scripts/validate_expanded_chain.py",
                    "CMakeLists.txt", "tests/test_middle_cpu_validator.py"}


def audit_provenance(report, executable, source_root, cmake_cache=None):
    before, after = report["provenance_before"], report["provenance_after"]
    unchanged = before == after
    require(type(report["source_unchanged"]) is bool and report["source_unchanged"] == unchanged,
            "source integrity claim mismatch")
    for provenance in (before, after):
        require(digest(executable) == provenance["executable_sha256"], "executable hash mismatch")
        names = set()
        for item in provenance["sources"] + provenance["extra_sources"]:
            require(item["path"] not in names, "duplicate source provenance")
            checked(source_root, item["path"], item["sha256"])
            names.add(item["path"])
        require(REQUIRED_SOURCES <= names, "required source provenance missing")
        head = subprocess.check_output(["git", "-C", str(source_root), "rev-parse", "HEAD"], text=True).strip()
        dirty = subprocess.check_output(["git", "-C", str(source_root), "status", "--porcelain", "--untracked-files=no"], text=True).strip()
        require(head == provenance["git_head"] and dirty == provenance["git_dirty"], "source revision mismatch")
        cache = Path(cmake_cache) if cmake_cache else Path(provenance["cmake_cache_path"])
        require(digest(cache) == provenance["cmake_cache_sha256"], "CMake cache mismatch")
    return unchanged


def audit_policy(report):
    # Reuse independent Windows policy arithmetic, adapting only the declared
    # method identity; matrix, timing and budgets are audited here separately.
    path = Path(__file__).with_name("validate_expanded_chain.py")
    spec = importlib.util.spec_from_file_location("expanded_policy_audit", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    adapter = dict(report, method_id=report["environment_method"],
                   provenance=report["provenance_before"],
                   cases=[dict(case, method_id=report["environment_method"]) for case in report["cases"]])
    require(report["environment_method"] in ("windows-explicit-highqos-v1", "windows-system-managed-v2"),
            "unknown environment method")
    valid = module.audit_method(adapter)
    if report["environment_method"] == "windows-system-managed-v2":
        # The legacy default-policy audit only checked flags. Check actual
        # observed process policy and affinity stability for the new schema.
        baseline = report["process_policy"]["before_request"]
        def same(snapshot):
            return (snapshot["process_power_throttling"] == baseline["process_power_throttling"]
                    and affinity_value(snapshot) == affinity_value(baseline))
        for case in report["cases"]:
            actual = same(case["scheduling_before"]) and same(case["scheduling_after"])
            require(actual == case["policy_ok"], "default case policy claim mismatch")
            valid &= actual
        collection = same(report["scheduling_after"])
        require(collection == report["collection_policy_ok"], "default collection policy mismatch")
        valid &= collection
    pinned = affinity_value(report["affinity_pinned"])
    require(pinned == affinity_value(report["process_policy"]["before_request"]),
            "pinning snapshot mismatch")
    return bool(valid and audit_affinity_restoration(report))


def audit(root, executable, source_root, cmake_cache=None):
    root, executable, source_root = Path(root), Path(executable), Path(source_root)
    report = json.loads((root / "result.json").read_text(encoding="utf-8-sig"))
    require(report["schema_version"] == 1 and report["kind"] == "middle-cpu"
            and report["method_id"] == "middle-cpu-v1", "unsupported CPU evidence identity")
    for key in ("quick", "diagnostic", "filtered", "complete", "reference_qualified", "on_ac", "pinned", "hybrid_established"):
        require(type(report[key]) is bool, f"invalid {key}")
    require(not report["quick"] or report["diagnostic"], "quick evidence must be diagnostic")
    require(not report["filtered"] or report["diagnostic"], "filtered evidence must be diagnostic")
    require(report["environment_method"] == "windows-explicit-highqos-v1" or report["diagnostic"],
            "system-managed evidence must be diagnostic")
    require(report["runs"] == (1 if report["diagnostic"] else 3), "run count mismatch")
    validate_matrix(report["cases"], report["runs"], report["complete"], report["filtered"])
    unchanged = audit_provenance(report, executable, source_root, cmake_cache)
    policy_ok = audit_policy(report)
    require(report["timer_overhead_subtracted"] is False and report["timer_only_median_ns"] >= 0,
            "timer overhead accounting mismatch")
    cases = []
    for config in report["cases"]:
        require(config["timing_file"] == f"timing-{config['id']}.csv"
                and config["batch_file"] == f"batch-{config['id']}.csv", "raw file identity mismatch")
        raw_path = checked(root, config["timing_file"], config["timing_sha256"])
        batch_path = checked(root, config["batch_file"], config["batch_sha256"])
        with raw_path.open(newline="") as raw, batch_path.open(newline="") as batch:
            cases.append(audit_case(csv.DictReader(raw), csv.DictReader(batch), config, report["power"]))
    eligible = (report["complete"] and not report["quick"] and not report["diagnostic"]
                and not report["filtered"] and report["runs"] == 3
                and report["provenance_before"]["build_configuration"] == "Release"
                and report["pinned"] and report["hybrid_established"] and report["on_ac"]
                and report["power"] != "unavailable" and policy_ok and unchanged
                and all(all(case[k] for k in ("windows_ok", "coverage_ok", "batch_coverage_ok",
                        "callback_budget_ok", "batch_budget_ok", "processing_ok", "replay_ok", "power_stable")) for case in cases))
    require(bool(eligible) == report["reference_qualified"], "reference qualification claim mismatch")
    return dict(reference_qualified=bool(eligible), complete=report["complete"],
                method_id="middle-cpu-v1", policy_ok=policy_ok, cases=cases,
                worst_p99_fraction=max(case["p99_fraction"] for case in cases),
                worst_batch_p95_fraction=max(case["batch_p95_fraction"] for case in cases))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--source-root", default=Path.cwd(), type=Path)
    parser.add_argument("--cmake-cache", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = audit(args.root, args.executable, args.source_root, args.cmake_cache)
    require(not args.output.exists(), "audit output already exists")
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "cases"}))
