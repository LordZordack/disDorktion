"""Audit a complete Output CPU matrix against its raw observations.

Default mode validates quick diagnostic evidence. --reference enforces the
complete stereo reference matrix, environment, timing, and coverage gates.
Only one raw case is retained in memory. No benchmark implementation is imported.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
from pathlib import Path

RESPONSES = ("butterworth", "chebyshev-1", "chebyshev-2")
SCENARIOS = ("steady", "narrow", "crossed") + tuple(
    f"response-{high}-{low}" for high in RESPONSES for low in RESPONSES
) + ("automation", "bypass", "bypass-neither", "bypass-high-pass", "bypass-low-pass",
     "bypass-both", "common-bypass", "oversized")
RATES = (48000, 96000)
CAPACITIES = (32, 64, 127, 256, 1024)
SOURCES = (
    "CMakeLists.txt", "benchmarks/ReferenceGainBenchmark.cpp",
    "benchmarks/OutputSectionBenchmarkPath.h", "benchmarks/WallTimeSampling.h",
    "benchmarks/AutomationCoverage.h", "benchmarks/TimingWindow.h", "src/dsp/OutputSection.h",
    "src/dsp/OutputSection.cpp", "src/dsp/HighPass.h", "src/dsp/LowPass.h",
    "src/dsp/CutoffFilter.h", "src/dsp/BlockDispatcher.h",
)
RAW_HEADER = (
    "run", "rate_hz", "channels", "prepared_block", "actual_block", "scenario", "observation",
    "control_iteration", "cutoff_residue", "high_pass_cutoff_hz", "low_pass_cutoff_hz",
    "high_pass_response", "low_pass_response", "retained_high_pass_bypass", "retained_low_pass_bypass",
    "common_bypass", "effective_high_pass_bypass", "effective_low_pass_bypass", "elapsed_ns",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def number(row: dict[str, str], field: str) -> float:
    value = float(row[field])
    require(math.isfinite(value) and value >= 0, f"invalid nonnegative finite {field}: {row[field]}")
    return value


def integer(value: str) -> int:
    require(value.isascii() and value.isdigit(), f"invalid unsigned integer: {value!r}")
    return int(value)


def boolean(value: str) -> bool:
    require(value in ("true", "false"), f"invalid boolean: {value!r}")
    return value == "true"


def rounded_match(actual: float, recorded: str, field: str) -> None:
    require(abs(actual - float(recorded)) <= 0.000501, f"{field} differs from raw: {actual} != {recorded}")


def coverage(counts: list[int]) -> dict[str, int | bool]:
    return {"covered": sum(value != 0 for value in counts), "minimum": min(counts),
            "maximum": max(counts), "qualifying": min(counts) >= 100}


def verify_automation_event(iteration: int, high_response: int, low_response: int,
                            high_bypass: int, low_bypass: int) -> None:
    expected_pair = (0, 4, 8, 1, 5, 6, 2, 3, 7)[iteration % 9]
    require(high_response * 3 + low_response == expected_pair,
            f"automated response event differs at callback {iteration}")
    require(high_bypass + 2 * low_bypass == (iteration // 9) % 4,
            f"automated stage bypass event differs at callback {iteration}")


def verify_reference_options(channels: int, runs: int) -> None:
    require(channels == 2, "reference qualification requires stereo")
    require(runs >= 3, "reference qualification requires at least three complete runs")


def verify_capture_mode(row: dict[str, str], reference: bool) -> None:
    count, seconds = integer(row["timing_count"]), number(row, "measurement_seconds")
    # The requested envelope allows 200k, but revision 2 has only 150k slots.
    # Accepting >150k would attest data that this collector cannot produce.
    require(0 < count <= 150000, "timing count exceeds revision 2 collector capacity")
    if reference:
        require(row["mode"] == "baseline" and boolean(row["qualified"]) and row["reason"] == "",
                "reference rows must be baseline, qualified, and have no failure reason")
        require(count >= 100000, "reference case has fewer than 100000 raw observations")
        require(10.0 <= seconds <= 120.001, "reference measurement window must be 10 to 120 seconds")
        require(number(row, "warmup_projected_callbacks") > 0 and number(row, "calibrated_callbacks") > 0,
                "reference calibration must be positive")
    else:
        require(row["mode"] == "quick" and not boolean(row["qualified"]) and "unqualified" in row["reason"],
                "quick evidence incorrectly qualified")
        require(seconds >= 0.2, "short quick measurement window")


def verify_reference_environment(row: dict[str, str]) -> None:
    require(" Release" in row["compiler"] and row["build"].endswith("-Release"),
            "reference requires a recorded Release executable")
    require(row["cpu"].strip() not in ("", "unavailable", "unknown"), "missing reference CPU identity")
    require(row["ac_status"] == "before=AC;after=AC", "reference requires AC before and after every case")
    require(re.fullmatch(r"\{[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}\}", row["power_scheme_guid"]) is not None,
            "missing observed power scheme GUID")
    require(row["conditions"].strip() != "", "missing recorded reference conditions")
    require(row["thermal_measurement"].strip() != "", "missing thermal observation or explicit unavailability")
    affinity = re.fullmatch(
        r"selected_group=0 selected_mask=(0x[0-9a-fA-F]+) previous_thread_mask=(0x[0-9a-fA-F]+) selected_efficiency_class=(\d+)",
        row["affinity"],
    )
    require(affinity is not None, "missing observed reference thread affinity")
    selected_mask, previous_mask, selected_class = (int(affinity[1], 16), int(affinity[2], 16), int(affinity[3]))
    require(selected_mask != 0 and selected_mask & (selected_mask - 1) == 0 and previous_mask != 0,
            "reference affinity must pin exactly one observed logical processor")
    topology, process = row["topology"].rsplit(" process_mask=", 1)
    require(re.fullmatch(r"0x[0-9a-fA-F]+", process) is not None, "invalid observed process affinity mask")
    process_mask = int(process, 16)
    entries, current_class = [], None
    for record in topology.split(";"):
        if not record:
            continue
        match = re.fullmatch(r"(?:class=(\d+))?/group=(\d+)/mask=(0x[0-9a-fA-F]+)", record)
        require(match is not None, "invalid processor topology record")
        if match[1] is not None:
            current_class = int(match[1])
        require(current_class is not None, "processor topology has no efficiency class")
        entries.append((current_class, int(match[2]), int(match[3], 16)))
    require(len({entry[0] for entry in entries}) >= 2, "reference hybrid processor topology was not established")
    require(selected_class == max(entry[0] for entry in entries), "reference affinity is not in the highest reported topology class")
    require(selected_mask & process_mask == selected_mask and any(
        cls == selected_class and group == 0 and mask & selected_mask == selected_mask for cls, group, mask in entries
    ), "reference affinity does not match the observed topology/process mask")


def verify_reference_outcome(row: dict[str, str], raw_p99: int, callback_deadline: float,
                             counts: dict[str, dict]) -> None:
    require(raw_p99 < callback_deadline * 0.1, "raw reference p99 must be strictly below ten percent of callback deadline")
    require(boolean(row["processing_ok"]), "reference processing failed")
    relevant = {"automation": {"cutoffs": 101, "lowCutoffs": 101, "responseBypass": 36},
                "bypass": {"bypass": 4}, "common-bypass": {"retainedCommon": 8}}.get(row["scenario"], {})
    for label, expected_states in relevant.items():
        require(counts[label]["covered"] == expected_states and counts[label]["minimum"] >= 100,
                f"reference {label} coverage requires every state and at least 100 observations per state")


def verify_reference_session(rows: list[dict[str, str]]) -> None:
    require(bool(rows), "reference session is empty")
    for field in ("cpu", "architecture", "physical_memory", "os", "sdk", "compiler", "build", "juce", "topology", "affinity"):
        require(len({row[field] for row in rows}) == 1, f"reference identity changed across runs: {field}")


def audit(args: argparse.Namespace) -> dict:
    auditor_sha256 = sha256(Path(__file__))
    if args.reference:
        verify_reference_options(args.channels, args.runs)
    root = args.source_root.resolve()
    summary_path = args.summary.resolve()
    raw_path = args.raw.resolve() if args.raw else Path(str(summary_path) + ".timings.csv")
    source_hashes = {name: sha256(root / name) for name in SOURCES}
    manifest = "".join(f"{name}={source_hashes[name]};" for name in SOURCES)
    aggregate = hashlib.sha256(manifest.encode("utf-8")).hexdigest()
    exe_hash = sha256(args.executable.resolve())
    with summary_path.open(encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        require(reader.fieldnames is not None and len(set(reader.fieldnames)) == len(reader.fieldnames),
                "missing or duplicate summary headers")
        rows = list(reader)
    expected = {(run, rate, block, scenario) for run in range(1, args.runs + 1)
                for rate in RATES for block in CAPACITIES for scenario in SCENARIOS}
    summaries: dict[tuple, dict[str, str]] = {}
    for row in rows:
        require(None not in row and None not in row.values(), "malformed summary row")
        key = (integer(row["run"]), integer(row["rate_hz"]), integer(row["prepared_block"]), row["scenario"])
        require(key in expected and key not in summaries, f"unexpected or duplicate summary case: {key}")
        summaries[key] = row
        require(row["target"] == "output-section" and row["input_profile"] == "unit-peak", f"wrong target/profile: {key}")
        require(integer(row["stages"]) == 1 and integer(row["latency_samples"]) == 0, f"wrong stages/latency: {key}")
        require(integer(row["channels"]) == args.channels, f"wrong channel count: {key}")
        expected_actual = 3 * key[2] + 1 if key[3] == "oversized" else key[2]
        require(integer(row["actual_block"]) == expected_actual, f"wrong actual block: {key}")
        require(row["collector_revision"] == "2" and row["sampling_mode"] == "equal-wall-time-slots-150000-no-catchup"
                and integer(row["sampling_stride"]) == 0, f"wrong collector: {key}")
        verify_capture_mode(row, args.reference)
        if args.reference:
            verify_reference_environment(row)
        for field in ("warmup_projected_callbacks", "calibrated_callbacks", "measurement_seconds", "median_ns", "p95_ns",
                      "p99_ns", "max_ns", "timer_only_median_ns", "p99_budget_ns", "p99_block_deadline_fraction",
                      "p99_budget_fraction", "post_high_pass_peak"):
            number(row, field)
        require(number(row, "median_ns") <= number(row, "p95_ns") <= number(row, "p99_ns") <= number(row, "max_ns"),
                f"unordered duration statistics: {key}")
        require(number(row, "post_high_pass_peak") <= 4, f"intermediate envelope failure: {key}")
        require(row["executable_sha256"] == exe_hash and row["source_sha256"] == aggregate
                and row["source_hash_manifest"] == manifest, f"stale source/executable provenance: {key}")
        require(bool(row["build"]) and bool(row["juce"]) and bool(row["compiler"]) and bool(row["conditions"]),
                f"missing descriptive provenance: {key}")
    require(set(summaries) == expected, f"incomplete matrix: missing {sorted(expected - set(summaries))}")
    if args.reference:
        # These identify the executable and observed hardware/session. Power is
        # checked per case and may use another valid observed scheme mid-run.
        verify_reference_session(rows)

    completed: set[tuple] = set()
    case_results = []
    current = None
    times: list[int] = []
    residues, low_residues, pairs, bypasses, common = [0] * 101, [0] * 101, [0] * 36, [0] * 4, [0] * 8
    previous_iteration = -1

    def finish_case() -> None:
        if current is None:
            return
        row = summaries[current]
        require(len(times) == integer(row["timing_count"]), f"raw count mismatch: {current}")
        times.sort()
        count = len(times)
        # Exactly the collector formula, rather than interpolated percentiles:
        # sorted[static_cast<size_t>((sorted.size() - 1) * p)].
        measured = {name: times[int((count - 1) * probability)] for name, probability in
                    (("median_ns", 0.5), ("p95_ns", 0.95), ("p99_ns", 0.99))}
        measured["max_ns"] = times[-1]
        for field, value in measured.items():
            rounded_match(value, row[field], f"{current}: {field}")
        callback_deadline = 1e9 * integer(row["actual_block"]) / current[1]
        budget = callback_deadline * 0.1
        fraction = measured["p99_ns"] / callback_deadline
        passes_budget = measured["p99_ns"] < budget
        rounded_match(budget, row["p99_budget_ns"], "p99_budget_ns")
        rounded_match(fraction, row["p99_block_deadline_fraction"], "p99_block_deadline_fraction")
        rounded_match(measured["p99_ns"] / budget, row["p99_budget_fraction"], "p99_budget_fraction")
        require(boolean(row["raw_p99_within_budget"]) == passes_budget, f"raw CPU gate mismatch: {current}")
        counts = {"cutoffs": coverage(residues), "lowCutoffs": coverage(low_residues),
                  "responseBypass": coverage(pairs), "bypass": coverage(bypasses), "retainedCommon": coverage(common)}
        for label, columns in (
            ("cutoffs", ("automation_settings_covered", "automation_min_samples_per_setting", "automation_max_samples_per_setting")),
            ("responseBypass", ("response_bypass_combinations_covered", "response_bypass_min_samples")),
            ("bypass", ("bypass_states_covered", "bypass_min_samples_per_state")),
            ("retainedCommon", ("retained_common_bypass_states_covered", "retained_common_bypass_min_samples")),
        ):
            for column, stat in zip(columns, ("covered", "minimum", "maximum")):
                require(integer(row[column]) == counts[label][stat], f"raw control coverage mismatch {current}: {column}")
        relevant = {"automation": ("cutoffs", "lowCutoffs", "responseBypass"), "bypass": ("bypass",),
                    "common-bypass": ("retainedCommon",)}.get(current[3], ())
        if args.reference:
            verify_reference_outcome(row, measured["p99_ns"], callback_deadline, counts)
        case_results.append({"run": current[0], "rateHz": current[1], "preparedBlock": current[2], "scenario": current[3],
                             "rawCount": count, "p99Ns": measured["p99_ns"], "p99CallbackDeadlineFraction": fraction,
                             "rawP99WithinBudget": passes_budget, "processingOk": boolean(row["processing_ok"]),
                             "coverage": {label: counts[label] for label in relevant},
                             "qualifyingControlCoverage": all(counts[label]["qualifying"] for label in relevant)})
        completed.add(current)

    with raw_path.open(encoding="utf-8", newline="") as stream:
        reader = csv.reader(stream)
        require(tuple(next(reader)) == RAW_HEADER, "raw header differs from expected schema")
        for fields in reader:
            require(len(fields) == len(RAW_HEADER), "malformed raw row")
            key = (integer(fields[0]), integer(fields[1]), integer(fields[3]), fields[5])
            if key != current:
                finish_case()
                require(key in summaries and key not in completed, f"unknown or noncontiguous raw case: {key}")
                current = key
                times = []
                residues, low_residues, pairs, bypasses, common = [0] * 101, [0] * 101, [0] * 36, [0] * 4, [0] * 8
                previous_iteration = -1
            require(integer(fields[2]) == args.channels and integer(fields[4]) == integer(summaries[key]["actual_block"]),
                    f"raw channels/block mismatch: {key}")
            require(integer(fields[6]) == len(times), f"nonsequential raw observations: {key}")
            iteration, residue = integer(fields[7]), integer(fields[8])
            require(iteration > previous_iteration and residue == iteration % 101, f"bad raw iteration/residue: {key}")
            previous_iteration = iteration
            high, low = float(fields[9]), float(fields[10])
            require(math.isfinite(high) and math.isfinite(low) and 20 <= high <= 20000 and 20 <= low <= 20000,
                    f"invalid raw cutoffs: {key}")
            hp_response, lp_response = integer(fields[11]), integer(fields[12])
            require(hp_response < 3 and lp_response < 3, f"invalid raw response: {key}")
            flags = [integer(value) for value in fields[13:18]]
            require(all(flag <= 1 for flag in flags), f"invalid raw bypass flag: {key}")
            hp_bypass, lp_bypass, common_bypass, effective_hp, effective_lp = flags
            require(effective_hp == (hp_bypass | common_bypass) and effective_lp == (lp_bypass | common_bypass),
                    f"common bypass does not retain stage flags: {key}")
            scenario = key[3]
            if scenario == "automation":
                require(common_bypass == 0, f"unexpected common automation flag: {key}")
                verify_automation_event(iteration, hp_response, lp_response, hp_bypass, lp_bypass)
                high_position = round(100 * math.log(high / 20) / math.log(1000))
                low_position = round(100 * math.log(low / 20) / math.log(1000))
                require(high_position == residue and low_position == (residue * 37) % 101, f"raw cutoff permutation mismatch: {key}")
                require(math.isclose(high, 20 * 1000 ** (high_position / 100), rel_tol=2e-6)
                        and math.isclose(low, 20 * 1000 ** (low_position / 100), rel_tol=2e-6), f"raw cutoff settings mismatch: {key}")
                # Count the ACTUAL response fields and flags, independent of the helpers.
                residues[high_position] += 1
                low_residues[low_position] += 1
                pairs[(hp_response * 3 + lp_response) * 4 + hp_bypass + 2 * lp_bypass] += 1
            else:
                require(high == (20 if scenario == "steady" else 1000 if scenario == "narrow" else 8000 if scenario == "crossed" else 200)
                        and low == (20000 if scenario == "steady" else 1000 if scenario == "narrow" else 200 if scenario == "crossed" else 8000),
                        f"wrong fixed cutoff controls: {key}")
                if scenario.startswith("response-"):
                    require(scenario == f"response-{RESPONSES[hp_response]}-{RESPONSES[lp_response]}", f"wrong fixed response controls: {key}")
                else:
                    response = 0 if scenario in ("steady", "narrow", "crossed") else 2
                    require(hp_response == lp_response == response, f"wrong fixed response: {key}")
                if scenario == "bypass":
                    require(common_bypass == 0 and hp_bypass + 2 * lp_bypass == (iteration // 8) % 4,
                            f"wrong bypass event boundary: {key}")
                    bypasses[hp_bypass + 2 * lp_bypass] += 1
                elif scenario == "common-bypass":
                    require(hp_bypass + 2 * lp_bypass == (iteration // 16) % 4 and common_bypass == (iteration // 8) % 2,
                            f"wrong common bypass event boundary: {key}")
                    common[(hp_bypass + 2 * lp_bypass) * 2 + common_bypass] += 1
                else:
                    state = {"bypass-high-pass": 1, "bypass-low-pass": 2, "bypass-both": 3}.get(scenario, 0)
                    require(common_bypass == 0 and hp_bypass + 2 * lp_bypass == state, f"wrong fixed bypass: {key}")
            times.append(integer(fields[18]))
    finish_case()
    require(completed == expected, f"missing raw cases: {sorted(expected - completed)}")
    worst = max(case_results, key=lambda case: case["p99CallbackDeadlineFraction"])
    summary_hash, raw_hash = sha256(summary_path), sha256(raw_path)
    require(auditor_sha256 == sha256(Path(__file__)), "auditor source changed during this audit")
    require(source_hashes == {name: sha256(root / name) for name in SOURCES}, "benchmark source changed during this audit")
    require(exe_hash == sha256(args.executable.resolve()), "benchmark executable changed during this audit")
    return {"schemaVersion": 1, "evidence": "full reference qualification" if args.reference else "quick diagnostic; reference qualification remains open", "auditPassed": True,
            "referenceQualified": args.reference, "channels": args.channels, "runs": args.runs, "caseCount": len(case_results),
            "rawTimingCount": sum(case["rawCount"] for case in case_results),
            "allRawP99WithinBudget": all(case["rawP99WithinBudget"] for case in case_results),
            "allProcessingOk": all(case["processingOk"] for case in case_results),
            "allTimedControlCoverageQualifying": all(case["qualifyingControlCoverage"] for case in case_results),
            "worstP99CallbackDeadlineFraction": worst["p99CallbackDeadlineFraction"],
            "worstCase": {name: worst[name] for name in ("run", "rateHz", "preparedBlock", "scenario", "p99Ns")},
            "percentileConvention": "sorted[floor((count - 1) * p)], exactly matching collector", "hashes": {
                "summarySha256": summary_hash, "rawSha256": raw_hash, "executableSha256": exe_hash,
                "sourceSha256": aggregate, "sourceFiles": source_hashes, "auditorSha256": auditor_sha256},
            "files": {"summary": str(summary_path), "raw": str(raw_path), "rawBytes": raw_path.stat().st_size,
                      "executable": str(args.executable.resolve())},
            "environment": {field: rows[0][field] for field in ("cpu", "architecture", "physical_memory", "os", "sdk", "compiler", "build", "juce", "topology", "affinity", "thermal_measurement")},
            "conditions": sorted({row["conditions"] for row in rows}),
            "observedPowerSchemes": sorted({row["power_scheme_guid"] for row in rows}), "cases": case_results}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summary", type=Path)
    parser.add_argument("--raw", type=Path)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--channels", type=int, choices=(1, 2), default=2)
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--reference", action="store_true", help="require complete stereo Release/pinned/AC reference qualification")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    require(args.runs > 0, "runs must be positive")
    require(not args.output.exists(), "audit destination must not exist")
    try:
        result = audit(args)
    except (ValueError, KeyError, OSError, StopIteration) as error:
        result = {"schemaVersion": 1, "auditPassed": False, "referenceQualified": False, "error": str(error)}
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, separators=(",", ":"), allow_nan=False)
        stream.write("\n")
    success = result["auditPassed"] and result["allRawP99WithinBudget"] and result["allProcessingOk"]
    print(json.dumps({key: value for key, value in result.items() if key not in ("cases", "hashes", "files")}, allow_nan=False))
    return 0 if success else 1


if __name__ == "__main__":
    raise SystemExit(main())
