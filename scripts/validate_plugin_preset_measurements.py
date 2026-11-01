#!/usr/bin/env python3
"""Validate issue-14 preset fixture coverage and declared level limits."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path

PRESETS = ("initialize", "subtle-warmth", "crunch", "folded", "digital-destruction")
FIXTURES = ("sine997", "two-tone997-1499", "repeated-transient")


def validate(path):
    with path.open(encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source))
    expected = set()
    for rate in (48000, 96000):
        for channels in (1, 2):
            expected.update((rate, channels, fixture, preset, preset, "settled")
                            for preset in PRESETS for fixture in FIXTURES)
            expected.update((rate, channels, "sine997", before, after, "transition")
                            for before in PRESETS for after in PRESETS)
    measured = {}
    for row in rows:
        key = (int(row["rate"]), int(row["channels"]), row["fixture"],
               row["from"], row["to"], row["phase"])
        if key not in expected or key in measured:
            raise ValueError(f"Unexpected or duplicate measurement: {key}")
        values = tuple(float(row[field]) for field in ("peak", "rms", "max_delta"))
        if any(not math.isfinite(value) or value < 0 for value in values):
            raise ValueError(f"Invalid numeric measurement: {key}")
        peak, rms, _ = values
        if peak > 1.0 or not 0 < rms <= peak:
            raise ValueError(f"Fixture exceeds unity peak or has invalid RMS: {key}: {values}")
        measured[key] = values
    if measured.keys() != expected:
        raise ValueError(f"Incomplete coverage: missing {len(expected - measured.keys())} rows")

    offsets = []
    for key, values in measured.items():
        rate, channels, fixture, before, after, phase = key
        if channels == 2:
            mono = measured[(rate, 1, fixture, before, after, phase)]
            if any(abs(a - b) > 1e-6 for a, b in zip(values, mono)):
                raise ValueError(f"Identical-channel mono/stereo disagreement: {key}")
        if phase == "settled":
            baseline = measured[(rate, channels, fixture, "initialize", "initialize", phase)][1]
            offset = 20 * math.log10(values[1] / baseline)
            if abs(offset) > 3.0:
                raise ValueError(f"Settled fixture exceeds +/-3 dB reference tolerance: {key}: {offset}")
            offsets.append({"rate": rate, "channels": channels, "fixture": fixture,
                            "preset": after, "relative_rms_db": offset})
    return {
        "status": "pass", "row_count": len(rows),
        "csv_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "maximum_peak": max(values[0] for values in measured.values()),
        "maximum_sample_delta": max(values[2] for values in measured.values()),
        "maximum_absolute_settled_rms_offset_db": max(abs(row["relative_rms_db"]) for row in offsets),
        "limits": {"peak": 1.0, "absolute_settled_rms_offset_db": 3.0,
                   "mono_stereo_absolute_difference": 1e-6},
        "conditions": {"rates": [48000, 96000], "channels": [1, 2], "block_size": 127,
                       "warm_seconds": 0.1, "measurement_seconds": 0.25,
                       "sine_peak": 0.25, "two_tone_component_peak": 0.125,
                       "transient_peak": 0.5},
        "limitations": "Checks aggregate fixture data, coverage and level bounds; does not independently recompute samples or establish listening/DAW acceptance.",
        "settled_offsets": offsets,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--executable", type=Path)
    args = parser.parse_args()
    result = validate(args.csv)
    if args.executable:
        result["executable_sha256"] = hashlib.sha256(args.executable.read_bytes()).hexdigest()
    args.summary.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: {result['row_count']} rows; peak {result['maximum_peak']:.6f}; "
          f"maximum RMS offset {result['maximum_absolute_settled_rms_offset_db']:.3f} dB")


if __name__ == "__main__":
    main()
