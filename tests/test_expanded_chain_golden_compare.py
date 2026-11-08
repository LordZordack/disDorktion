import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    "golden_compare", Path(__file__).resolve().parents[1] / "scripts/compare_expanded_chain_golden.py")
golden = importlib.util.module_from_spec(spec)
spec.loader.exec_module(golden)


class GoldenCompareTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name) / "base"
        self.candidate = Path(self.temp.name) / "candidate"
        self.base.mkdir()
        self.candidate.mkdir()

    def capture(self, root, values=(0., .5, -.25, 1.)):
        data = struct.pack("<4f", *values)
        (root / "case.f32").write_bytes(data)
        return {"target": "full-chain", "partition": "fixed127", "sample_rate": 8000,
                "channels": 2, "frames": 2, "capacity": 127, "latency_samples": 92,
                "scenario": golden.SCENARIO, "file": "case.f32", "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
                "events": [{"frame": 0, "action": "settings", "settings": {"toneDb": 3}}]}

    def test_identical_capture(self):
        a, b = self.capture(self.base), self.capture(self.candidate)
        self.assertTrue(golden.compare_case(self.base, a, self.candidate, b)["identical"])

    def test_changed_capture_reports_first_sample_even_with_updated_hash(self):
        a = self.capture(self.base)
        b = self.capture(self.candidate, (0., .5, -.5, 1.))
        result = golden.compare_case(self.base, a, self.candidate, b)
        self.assertFalse(result["identical"])
        self.assertEqual(result["first_difference"]["sample"], 2)
        self.assertEqual(result["first_difference"]["frame"], 1)
        self.assertEqual(result["first_difference"]["channel"], 0)

    def test_event_and_metadata_drift_rejected(self):
        a, b = self.capture(self.base), self.capture(self.candidate)
        for key, value in (("capacity", 128), ("events", [{"frame": 1}])):
            changed = copy.deepcopy(b)
            changed[key] = value
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "metadata"):
                golden.compare_case(self.base, a, self.candidate, changed)
        b["events"][0]["settings"]["toneDb"] = 4
        with self.assertRaisesRegex(ValueError, "metadata"):
            golden.compare_case(self.base, a, self.candidate, b)

    def test_truncated_file_and_forged_hash_rejected(self):
        a = self.capture(self.base)
        (self.base / "case.f32").write_bytes(b"1234")
        with self.assertRaisesRegex(ValueError, "length"):
            golden.read_capture(self.base, a)
        a = self.capture(self.base)
        a["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "SHA"):
            golden.read_capture(self.base, a)

    def test_path_escape_and_absolute_path_rejected(self):
        a = self.capture(self.base)
        for path in ("../outside.f32", str((self.base / "case.f32").resolve())):
            a["file"] = path
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "path"):
                golden.read_capture(self.base, a)

    def manifest(self):
        cases = []
        for rate, channels, partition, target in sorted(golden.EXPECTED_CASES):
            row = self.capture(self.base)
            row.update(sample_rate=rate, channels=channels, partition=partition, target=target,
                       frames=2 * rate, bytes=2 * rate * channels * 4,
                       latency_samples=92 if target == "full-chain" else 0)
            row["events"] = [{"frame": frame, "action": "settings", "settings": {"toneDb": 3}}
                             for frame in range(20)]
            cases.append(row)
        return {"kind": golden.KIND, "layout": golden.LAYOUT,
                "source_recipe": golden.RECIPE, "cpu_qualification": False,
                "provenance": {"source": "old"}, "cases": cases}

    def test_complete_expected_matrix_accepted(self):
        m = self.manifest()
        (self.base / "manifest.json").write_text(json.dumps(m), encoding="utf-8")
        self.assertEqual(len(golden.load_manifest(self.base)["cases"]), 48)

    def test_signed_zero_difference_is_not_tolerated(self):
        a = self.capture(self.base)
        b = self.capture(self.candidate, (-0., .5, -.25, 1.))
        result = golden.compare_case(self.base, a, self.candidate, b)
        self.assertFalse(result["identical"])
        self.assertEqual(result["first_difference"]["sample"], 0)

    def test_cli_reports_failure_and_preserves_existing_report(self):
        report = Path(self.temp.name) / "report.json"
        self.assertEqual(golden.main([str(self.base), str(self.candidate), "--output", str(report)]), 1)
        data = report.read_bytes()
        self.assertFalse(json.loads(data)["passed"])
        self.assertEqual(golden.main([str(self.base), str(self.candidate), "--output", str(report)]), 2)
        self.assertEqual(report.read_bytes(), data)

    def test_matrix_rejects_missing_duplicate_and_wrong_latency(self):
        for mutation in (lambda m: m["cases"].pop(),
                         lambda m: m["cases"].__setitem__(1, copy.deepcopy(m["cases"][0])),
                         lambda m: m["cases"][0].__setitem__("latency_samples", 1)):
            m = self.manifest()
            mutation(m)
            (self.base / "manifest.json").write_text(json.dumps(m), encoding="utf-8")
            with self.assertRaises(ValueError):
                golden.load_manifest(self.base)

    def test_provenance_difference_allowed_but_recipe_difference_rejected(self):
        a = self.manifest()
        b = copy.deepcopy(a)
        b["provenance"] = {"source": "optimized", "executable": "new"}
        golden.compare_manifest_metadata(a, b)
        b["source_recipe"] += " changed"
        with self.assertRaisesRegex(ValueError, "manifest metadata"):
            golden.compare_manifest_metadata(a, b)


if __name__ == "__main__":
    unittest.main()
