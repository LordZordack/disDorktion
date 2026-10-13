"""Validate Q1 measurement artifacts and summarize the declared decision gates.

Standard library only. This checks saved evidence, not human listening or
callback deadlines. Repeat comparison excludes CPU and timestamp/build JSON.
"""
from __future__ import annotations

import argparse
import array
import csv
import gzip
import hashlib
import json
import math
from pathlib import Path
import struct
import sys


def check(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def wav_rms(path: Path) -> float:
    blob = path.read_bytes()
    check(blob[:4] == b"RIFF" and blob[8:12] == b"WAVE", f"Invalid WAV: {path}")
    check(struct.unpack_from("<I", blob, 4)[0] + 8 == len(blob), "RIFF size mismatch")
    offset, payload, fmt = 12, None, None
    while offset + 8 <= len(blob):
        chunk, size = struct.unpack_from("<4sI", blob, offset)
        offset += 8
        check(offset + size <= len(blob), "Truncated WAV chunk")
        if chunk == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", blob, offset)
        if chunk == b"data":
            payload = blob[offset:offset+size]
        offset += size + size % 2
    check(fmt is not None and payload is not None, "WAV chunks missing")
    encoding, channels, rate, _, align, bits = fmt
    check((encoding, channels, rate, align, bits) == (3, 2, 48000, 8, 32), "Unexpected WAV format")
    check(len(payload) == 144000*8, "Unexpected aligned listening length")
    values = array.array("f")
    values.frombytes(payload)
    if sys.byteorder != "little":
        values.byteswap()
    check(all(math.isfinite(x) for x in values), "Nonfinite listening audio")
    check(max(abs(x) for x in values) <= .800001, "Listening headroom exceeded")
    return math.sqrt(sum(x*x for x in values)/len(values))


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024*1024), b""):
            h.update(chunk)
    return h.hexdigest()


def validate_numeric_tables(directory: Path, report: dict) -> tuple[list, list]:
    def rows(name):
        with (directory / name).open(encoding="utf-8", newline="") as handle:
            return list(csv.DictReader(handle))

    def close(actual, expected, label):
        check(math.isfinite(actual) and math.isfinite(expected)
              and math.isclose(actual, expected, rel_tol=1e-9, abs_tol=1e-8), label)

    filter_cases = {(rate, factor): {} for rate in (44100, 48000, 96000) for factor in (1, 2, 4, 8, 16)}
    for row in rows("filter_response.csv"):
        key = (int(row["sample_rate"]), int(row["factor"]))
        check(key in filter_cases, "Unexpected filter case")
        frequency = float(row["frequency_hz"])
        point = round(frequency / key[0] / .002)
        close(frequency, point * .002 * key[0], "Filter frequency grid mismatch")
        mix = float(row["mix"])
        check(0 <= point <= 245 and mix in (0, .25, .5, .75, 1), "Filter grid range")
        grid_key = (point, mix)
        check(grid_key not in filter_cases[key], "Duplicate filter grid row")
        values = {name: float(row[name]) for name in ("magnitude_db", "residual_phase_deg", "group_delay_samples", "blend_db", "latency_samples", "impulse_peak_sample")}
        check(all(math.isfinite(x) for x in values.values()), "Nonfinite filter row")
        filter_cases[key][grid_key] = values
    check(len(report["filters"]) == len(filter_cases), "Filter JSON count")
    validated_filters = []
    seen = set()
    for item in report["filters"]:
        key = (int(item["sampleRate"]), int(item["factor"]))
        check(key in filter_cases and key not in seen, "Filter JSON case identity")
        seen.add(key)
        grid = filter_cases[key]
        check(len(grid) == 246*5, "Filter grid coverage gap")
        for value in grid.values():
            check(value["latency_samples"] == item["latencySamples"] and value["impulse_peak_sample"] == item["impulsePeakSample"], "Filter latency/peak mismatch")
        in_band = [value for (point, mix), value in grid.items() if point <= 200 and mix == .5]
        derived = {
            "maxMagnitudeDeviationDbTo04Fs": max(abs(v["magnitude_db"]) for v in in_band),
            "maxResidualPhaseDegreesTo04Fs": max(abs(v["residual_phase_deg"]) for v in in_band),
            "maxResidualGroupDelaySamplesTo04Fs": max(abs(v["group_delay_samples"]-item["latencySamples"]) for v in in_band),
            "maxHalfBlendDeviationDbTo04Fs": max(abs(v["blend_db"]) for v in in_band),
        }
        for name, actual in derived.items():
            close(actual, item[name], "Filter JSON/CSV maximum mismatch: " + name)
        passed = (derived["maxMagnitudeDeviationDbTo04Fs"] <= .1 and derived["maxResidualPhaseDegreesTo04Fs"] <= 5 and derived["maxHalfBlendDeviationDbTo04Fs"] <= .2)
        check(type(item["filterTargetsPassed"]) is bool and item["filterTargetsPassed"] == passed, "Stale filter gate flag")
        validated_filters.append({**item, **derived, "filterTargetsPassed": passed})

    def cpu_key(row):
        return (int(row["sample_rate"]), int(row["factor"]), int(row["channels"]), int(row["block_size"]), row["path"])

    cpu_cases = {(rate, factor, channel, block, path): {} for rate in (48000, 96000) for factor in (1, 2, 4, 8) for channel in (1, 2) for block in (32, 128, 512) for path in ("tanh", "folder", "cascade")}
    for row in rows("cpu_raw.csv"):
        key, batch = cpu_key(row), int(row["batch"])
        check(key in cpu_cases and 0 <= batch < 21, "Unexpected CPU batch")
        check(batch not in cpu_cases[key], "Duplicate CPU batch")
        value = float(row["batch_mean_ns"])
        check(math.isfinite(value) and value > 0, "Invalid CPU timing")
        cpu_cases[key][batch] = value
    summaries = {}
    for row in rows("cpu_summary.csv"):
        key = cpu_key(row)
        check(key in cpu_cases and key not in summaries, "Unexpected/duplicate CPU summary")
        summaries[key] = row
    check(set(summaries) == set(cpu_cases), "CPU summary coverage gap")
    check(len(report["cpu"]) == len(cpu_cases), "CPU JSON count")
    validated_cpu, seen = [], set()
    for item in report["cpu"]:
        key = (int(item["sampleRate"]), int(item["factor"]), int(item["channels"]), int(item["blockSize"]), item["path"])
        check(key in cpu_cases and key not in seen, "CPU JSON case identity")
        seen.add(key)
        check(len(cpu_cases[key]) == 21, "CPU raw batch coverage gap")
        values = sorted(cpu_cases[key].values())
        median, p95 = values[10], values[19]
        fraction = p95 / (1e9 * key[3] / key[0])
        for json_name, csv_name, value in (("medianBatchMeanNs", "median_batch_mean_ns", median), ("p95BatchMeanNs", "p95_batch_mean_ns", p95), ("p95BatchMeanDeadlineFraction", "p95_batch_mean_deadline_fraction", fraction)):
            close(value, float(summaries[key][csv_name]), "CPU raw/summary mismatch")
            close(value, item[json_name], "CPU raw/JSON mismatch")
        passed = fraction < .1
        check(type(item["localBudgetPassed"]) is bool and item["localBudgetPassed"] == passed, "Stale CPU gate flag")
        validated_cpu.append({**item, "medianBatchMeanNs": median, "p95BatchMeanNs": p95, "p95BatchMeanDeadlineFraction": fraction, "localBudgetPassed": passed})
    return validated_filters, validated_cpu


def validate(directory: Path, repeat: Path | None = None) -> dict:
    report = json.loads((directory / "result.json").read_text(encoding="utf-8-sig"))
    check(len(report["spectra"]) == 900 and len(report["filters"]) == 15, "Spectral/filter coverage gap")
    check(len(report["cpu"]) == 144 and len(report["listening"]) == 18, "CPU/listening coverage gap")
    check("Pending" in report["listeningStatus"], "Machine report must not claim human acceptance")
    validated_filters, validated_cpu = validate_numeric_tables(directory, report)
    rows = list(csv.DictReader((directory / "alias_summary.csv").open(encoding="utf-8", newline="")))
    check(len(rows) == 900 and len({row["id"] for row in rows}) == 900, "Alias summary coverage gap")
    by_id = {item["id"]: item for item in report["spectra"]}
    groups = {}
    for row in rows:
        item = by_id[row["id"]]
        check(int(row["latency"]) == item["latencySamples"], "Latency summary mismatch")
        if row["kind"].startswith("two"):
            check(row["alias_lower_bound_dbc"] == "" and item["aliasLowerBoundDbc"] is None, "IM classified as alias")
            continue
        alias = float(row["alias_lower_bound_dbc"])
        check(math.isfinite(alias) and abs(alias-item["aliasLowerBoundDbc"]) < 1e-8, "Alias JSON/CSV mismatch")
        key = tuple(row[field] for field in ("sample_rate", "path", "drive_db", "threshold", "kind"))
        groups.setdefault(key, {})[int(row["factor"])] = alias
    improvements = []
    for key, factors in groups.items():
        check(set(factors) == {1, 2, 4, 8, 16}, "Rate-comparison coverage gap")
        if key[2] == "12" and key[4] == "sine-high" and factors[1] > -80:
            improvements.append({"case": key, "improvementDb": factors[1]-factors[4]})
    # Decompress to the end to verify the gzip CRC and per-fixture ordered bins.
    expected_ids = [item["id"] for item in report["spectra"]]
    line_count = 0
    with gzip.open(directory / "spectra.csv.gz", "rt", encoding="utf-8") as handle:
        check(next(handle).strip() == "id,bin,frequency_hz,amplitude_dbfs", "Spectrum header")
        for line_count, line in enumerate(handle, 1):
            identity, bin_text, _, amplitude = line.rstrip().split(",")
            fixture, bin_index = divmod(line_count-1, 8193)
            check(fixture < len(expected_ids) and identity == expected_ids[fixture] and int(bin_text) == bin_index, "Spectrum bin sequence")
            check(math.isfinite(float(amplitude)), "Nonfinite saved spectrum")
    check(line_count == 900*8193, "Full spectrum coverage gap")
    audio_count = 0
    for group in report["listening"]:
        check({m["factor"] for m in group["matches"]} == {1, 2, 4, 8, 16}, "Listening factor gap")
        levels = []
        for match in group["matches"]:
            measured = wav_rms(directory / match["file"])
            check(abs(measured-match["matchedRms"]) < 1e-9, "Saved WAV RMS disagrees with report")
            levels.append(measured)
            audio_count += 1
        check(20*math.log10(max(levels)/min(levels)) < .001, "Listening RMS delta exceeds .001 dB")
    filter4 = [r for r in validated_filters if r["factor"] == 4]
    cpu4 = [r for r in validated_cpu if r["factor"] == 4]
    result = {
        "artifactValidationPassed": True,
        "spectralCases": 900, "spectrumBins": line_count, "validatedWavs": audio_count,
        "fourTimesFilters": filter4,
        "fourTimesFilterGatePassed": all(r["filterTargetsPassed"] for r in filter4),
        "representativeAliasImprovements": improvements,
        "representativeAliasGatePassed": bool(improvements) and all(r["improvementDb"] >= 15 for r in improvements),
        "fourTimesWorstCpuBudgetFraction": max(r["p95BatchMeanDeadlineFraction"] for r in cpu4),
        "fourTimesLocalCpuGatePassed": all(r["localBudgetPassed"] for r in cpu4),
        "fourTimesStressAliasRangeDbc": [min(r["aliasLowerBoundDbc"] for r in report["spectra"] if r["factor"] == 4 and r["driveDb"] == 36 and r["kind"] == "sine-high"), max(r["aliasLowerBoundDbc"] for r in report["spectra"] if r["factor"] == 4 and r["driveDb"] == 36 and r["kind"] == "sine-high")],
        "humanListening": "pending", "productionAcceptance": "pending human listening and residual alias decision",
    }
    if repeat is not None:
        files = ["alias_summary.csv", "filter_response.csv", "spectra.csv.gz"]
        files += [m["file"] for group in report["listening"] for m in group["matches"]]
        for filename in files:
            check(digest(directory / filename) == digest(repeat / filename), f"Determinism mismatch: {filename}")
        result["byteIdenticalRepeatFiles"] = len(files)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--repeat", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = validate(args.directory, args.repeat)
        if args.output:
            args.output.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
        print(json.dumps(result, indent=2))
        return 0
    except (ValueError, OSError, KeyError, StopIteration) as error:
        print(f"Artifact validation failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
