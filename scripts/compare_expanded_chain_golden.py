"""Compare local ExpandedChainGolden captures exactly; never modify captures."""

import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import re
import struct
import sys


KIND = "expanded-chain-golden-v1"
LAYOUT = "interleaved-float32-little-endian"
RECIPE = "ExpandedChainGolden.cpp excitation(), absolute sample frame; no random input"
SCENARIO = "interrupted-controls-bypass-lifecycle-v1"
EXPECTED_CASES = set(itertools.product(
    (8000, 44100, 48000, 96000, 192000, 384000), (1, 2),
    ("fixed127", "irregular"), ("full-chain", "output-section")))


def case_key(case):
    return tuple(case[name] for name in ("sample_rate", "channels", "partition", "target"))


def integer(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value == int(value)


def load_manifest(root):
    root = Path(root).resolve(strict=True)
    if not root.is_dir():
        raise ValueError(f"Capture directory required: {root}")
    manifest_path = root / "manifest.json"
    if not manifest_path.resolve(strict=True).is_relative_to(root):
        raise ValueError("Manifest path escapes capture directory")
    with manifest_path.open(encoding="utf-8") as source:
        manifest = json.load(source)
    if not isinstance(manifest, dict):
        raise ValueError("Manifest must be an object")
    for name, expected in (("kind", KIND), ("layout", LAYOUT), ("source_recipe", RECIPE)):
        if manifest.get(name) != expected:
            raise ValueError(f"Unexpected manifest {name}")
    if manifest.get("cpu_qualification") is not False:
        raise ValueError("Golden captures must not claim CPU qualification")
    cases = manifest.get("cases")
    if not isinstance(cases, list) or len(cases) != 48:
        raise ValueError("Expected 48 golden cases")
    seen = set()
    for case in cases:
        if not isinstance(case, dict):
            raise ValueError("Case must be an object")
        try:
            if not integer(case["sample_rate"]) or not integer(case["channels"]):
                raise ValueError("Invalid case sample rate or channels")
            key = case_key(case)
            if key not in EXPECTED_CASES or key in seen:
                raise ValueError(f"Unexpected or duplicate case: {key}")
            seen.add(key)
            if (case["scenario"] != SCENARIO or case["capacity"] != 127
                    or not integer(case["capacity"]) or not integer(case["frames"])
                    or case["frames"] != 2 * case["sample_rate"]):
                raise ValueError(f"Invalid case recipe metadata: {key}")
            latency = 92 if case["target"] == "full-chain" else 0
            if not integer(case["latency_samples"]) or case["latency_samples"] != latency:
                raise ValueError(f"Invalid declared latency: {key}")
            if not isinstance(case["events"], list) or len(case["events"]) != 20:
                raise ValueError(f"Invalid event schedule: {key}")
            for event in case["events"]:
                if (not isinstance(event, dict) or not integer(event.get("frame"))
                        or not 0 <= event["frame"] < case["frames"]
                        or not isinstance(event.get("action"), str)
                        or not isinstance(event.get("settings"), dict)):
                    raise ValueError(f"Invalid event metadata: {key}")
            # Required here; actual local content is checked during comparison.
            for field in ("file", "bytes", "sha256"):
                if field not in case:
                    raise ValueError(f"Missing case field {field}: {key}")
        except (KeyError, TypeError) as error:
            raise ValueError(f"Malformed case metadata: {error}") from error
    if seen != EXPECTED_CASES:
        raise ValueError("Incomplete golden case matrix")
    return manifest


def compare_manifest_metadata(baseline, candidate):
    def metadata(manifest):
        return {key: value for key, value in manifest.items() if key not in ("cases", "provenance")}
    if metadata(baseline) != metadata(candidate):
        raise ValueError("Golden manifest metadata differ (only provenance may change)")


def read_capture(root, case):
    root = Path(root).resolve(strict=True)
    filename = case.get("file")
    if not isinstance(filename, str) or not filename or Path(filename).is_absolute():
        raise ValueError(f"Invalid capture path: {filename!r}")
    path = (root / filename).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise ValueError(f"Capture path escapes directory or is not a file: {filename}")
    if (not integer(case.get("frames")) or case["frames"] <= 0
            or not integer(case.get("channels")) or case["channels"] not in (1, 2)):
        raise ValueError("Invalid capture dimensions")
    expected = int(case["frames"] * case["channels"] * 4)
    if not integer(case.get("bytes")) or case["bytes"] != expected or path.stat().st_size != expected:
        raise ValueError(f"Capture length mismatch: {filename}")
    data = path.read_bytes()
    if len(data) != expected:
        raise ValueError(f"Capture length changed during read: {filename}")
    declared = case.get("sha256")
    if (not isinstance(declared, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", declared)
            or hashlib.sha256(data).hexdigest() != declared.lower()):
        raise ValueError(f"Capture SHA256 mismatch: {filename}")
    return data


def compare_case(baseline_root, baseline, candidate_root, candidate):
    def metadata(case):
        return {key: value for key, value in case.items() if key != "sha256"}
    if metadata(baseline) != metadata(candidate):
        raise ValueError(f"Case metadata differ: {case_key(baseline)}")
    a = read_capture(baseline_root, baseline)
    b = read_capture(candidate_root, candidate)
    result = {"case": list(case_key(baseline)), "file": baseline["file"],
              "bytes": len(a), "identical": a == b}
    if a != b:
        offset = next(i for i, (x, y) in enumerate(zip(a, b)) if x != y)
        sample = offset // 4
        start = sample * 4
        # Bits make signed-zero differences explicit and remain JSON-safe even
        # when a corrupted float contains NaN or infinity.
        result["first_difference"] = {
            "byte": offset, "sample": sample, "frame": sample // baseline["channels"],
            "channel": sample % baseline["channels"],
            "baseline_value": repr(struct.unpack_from("<f", a, start)[0]),
            "candidate_value": repr(struct.unpack_from("<f", b, start)[0]),
            "baseline_bits": a[start:start + 4].hex(), "candidate_bits": b[start:start + 4].hex()}
    return result


def compare_directories(baseline_root, candidate_root):
    a, b = load_manifest(baseline_root), load_manifest(candidate_root)
    compare_manifest_metadata(a, b)
    candidates = {case_key(case): case for case in b["cases"]}
    results = [compare_case(baseline_root, case, candidate_root, candidates[case_key(case)])
               for case in a["cases"]]
    return {"kind": "expanded-chain-golden-comparison-v1", "passed": all(r["identical"] for r in results),
            "compared_cases": len(results), "cases": results,
            "baseline_provenance": a.get("provenance"), "candidate_provenance": b.get("provenance"),
            "cpu_qualification": False}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline_directory", type=Path)
    parser.add_argument("candidate_directory", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    report = {"kind": "expanded-chain-golden-comparison-v1", "passed": False, "cpu_qualification": False}
    try:
        report = compare_directories(args.baseline_directory, args.candidate_directory)
    except (OSError, ValueError, KeyError, TypeError) as error:
        report["error"] = str(error)
    report.update(baseline_directory=str(args.baseline_directory.resolve()),
                  candidate_directory=str(args.candidate_directory.resolve()))
    # Exclusive creation preserves existing evidence, including on failed comparisons.
    try:
        with args.output.open("x", encoding="utf-8") as destination:
            json.dump(report, destination, indent=2, allow_nan=False)
            destination.write("\n")
    except (OSError, ValueError) as error:
        print(f"Cannot create comparison report: {error}", file=sys.stderr)
        return 2
    print("Golden captures identical" if report["passed"] else "Golden comparison failed")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
