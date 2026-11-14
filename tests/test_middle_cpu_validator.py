"""Mutation tests for the independent Waveshaper CPU evidence audit."""
import copy
import importlib.util
import itertools
import unittest
import tempfile
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "middle_cpu", Path(__file__).resolve().parents[1] / "scripts/validate_middle_cpu.py")
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class MatrixTests(unittest.TestCase):
    def matrix(self, runs=3):
        return [dict(id=i, run=r, rate=rate, channels=ch, capacity=cap, scenario=s)
                for i, (r, rate, ch, cap, s) in enumerate(itertools.product(
                    range(runs), (48000, 96000), (1, 2), (32, 64, 127, 256, 1024), range(10)))]

    def test_complete_matrix_is_exactly_600_cases(self):
        audit.validate_matrix(self.matrix(), 3, True)

    def test_missing_case_cannot_claim_complete(self):
        with self.assertRaisesRegex(ValueError, "incomplete"):
            audit.validate_matrix(self.matrix()[:-1], 3, True)

    def test_duplicate_or_reordered_case_is_rejected(self):
        cases = self.matrix()
        cases[4] = copy.deepcopy(cases[3])
        with self.assertRaises(ValueError):
            audit.validate_matrix(cases, 3, True)

    def test_partial_prefix_can_be_audited_without_qualification(self):
        audit.validate_matrix(self.matrix()[:1], 3, False)

    def test_nonprefix_partial_is_rejected(self):
        with self.assertRaises(ValueError):
            audit.validate_matrix(self.matrix()[1:2], 3, False)


class PrimitiveTests(unittest.TestCase):
    def test_quantiles_are_nearest_rank(self):
        self.assertEqual(audit.quantile([1, 2, 3, 100], .99), 100)
        self.assertEqual(audit.quantile([1, 2, 3, 100], .5), 2)

    def test_empty_distribution_rejected(self):
        with self.assertRaises(ValueError):
            audit.quantile([], .99)


class ScheduleAndRawTests(unittest.TestCase):
    def config(self, scenario=0):
        return dict(id=0, scenario=scenario, frames=32, capacity=32, channels=1,
                    rate=48000, start_iteration=0, warmup_frames=188,
                    window_seconds=10, collection_elapsed_ns=10000000000,
                    warmup_seconds=2, probe_seconds=2)

    def rows(self, count=200, scenario=0):
        result = []
        for i in range(count):
            phase, fixture, residue = audit.schedule(scenario, i, 48000, 32)
            result.append(dict(iteration=str(i), wall_ns=str(i * 100000),
                               duration_ns="100", residue=str(residue),
                               phase=str(phase), fixture=str(fixture)))
        return result

    def test_transition_callbacks_can_cover_multiple_classes(self):
        # ramp=480, warm-up=188, 32-frame callbacks; dwell=29.
        self.assertEqual(audit.schedule(8, 29 + 5, 48000, 32)[0], 4 | 8)
        self.assertEqual(audit.schedule(8, 29 + 20, 48000, 32)[0], 8 | 16)

    def test_raw_stats_are_independent_of_claims(self):
        result = audit.audit_samples(self.rows(), self.config())
        self.assertEqual(result["p99_ns"], 100)
        self.assertFalse(result["coverage_ok"])

    def test_rejects_forged_schedule(self):
        rows = self.rows()
        rows[0]["phase"] = "1"
        with self.assertRaisesRegex(ValueError, "schedule"):
            audit.audit_samples(rows, self.config())

    def test_rejects_duplicate_wall_time(self):
        rows = self.rows()
        rows[1]["wall_ns"] = rows[0]["wall_ns"]
        with self.assertRaisesRegex(ValueError, "wall"):
            audit.audit_samples(rows, self.config())

    def test_rejects_two_samples_from_same_wall_time_slot(self):
        rows = self.rows()
        rows[1]["wall_ns"] = "1000"
        with self.assertRaisesRegex(ValueError, "sample slot"):
            audit.audit_samples(rows, self.config())

    def test_rejects_negative_duration(self):
        rows = self.rows()
        rows[0]["duration_ns"] = "-1"
        with self.assertRaisesRegex(ValueError, "duration"):
            audit.audit_samples(rows, self.config())

    def test_rejects_impossible_callback_overlap(self):
        rows = self.rows()
        rows[0]["duration_ns"] = "200000"
        with self.assertRaisesRegex(ValueError, "overlap"):
            audit.audit_samples(rows, self.config())

    def test_short_measured_collection_cannot_qualify(self):
        config = self.config()
        config["collection_elapsed_ns"] = 100000000
        self.assertFalse(audit.audit_samples(self.rows(), config)["collection_coverage_ok"])

    def test_samples_packed_at_start_cannot_cover_window(self):
        self.assertFalse(audit.audit_samples(self.rows(), self.config())["collection_coverage_ok"])

    def test_full_window_endpoints_are_observable_independently(self):
        rows = self.rows()
        for i, row in enumerate(rows):
            row["wall_ns"] = str(i * 50000000)
        self.assertTrue(audit.audit_samples(rows, self.config())["collection_coverage_ok"])

    def test_first_record_is_first_collection_callback(self):
        rows = self.rows()[1:]
        with self.assertRaisesRegex(ValueError, "first callback"):
            audit.audit_samples(rows, self.config())

    def test_rare_transition_tail_cannot_hide_in_aggregate(self):
        rows = self.rows(10000, 8)
        for row in rows:
            if int(row["phase"]) & 4:
                row["duration_ns"] = "100000"
        result = audit.audit_samples(rows, self.config(8))
        self.assertFalse(result["callback_budget_ok"])

    def test_batch_requires_all_21_complete_batches(self):
        with self.assertRaisesRegex(ValueError, "batch"):
            audit.audit_batches([], self.config())

    def test_batch_gate_is_strict_at_ten_percent(self):
        config = self.config()
        config["rate"] = 96000
        rows = []
        for i in range(21 * 128):
            phase, fixture, _ = audit.schedule(0, i + 128, 96000, 32)
            rows.append(dict(batch=str(i // 128), block=str(i % 128),
                             iteration=str(i + 128), batch_elapsed_ns=str(33334 * 128),
                             phase=str(phase), fixture=str(fixture)))
        self.assertFalse(audit.audit_batches(rows, config)["batch_budget_ok"])

    def test_initial_zero_mix_is_already_suspended(self):
        self.assertEqual(audit.schedule(8, 0, 48000, 32)[0], 2)

    def test_mix_fixture_rotates_across_complete_state_cycles(self):
        for cycle in range(3):
            for epoch in range(3):
                self.assertEqual(audit.schedule(8, (cycle * 3 + epoch) * 29, 48000, 32)[1], cycle)


class IntegrityTests(unittest.TestCase):
    def test_path_escape_is_rejected_even_if_hash_matches(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "root"
            root.mkdir()
            outside = Path(temp) / "outside"
            outside.write_bytes(b"test")
            with self.assertRaisesRegex(ValueError, "escapes"):
                audit.checked(root, "../outside", audit.digest(outside))

    def test_changed_raw_file_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "raw.csv"
            path.write_bytes(b"before")
            original = audit.digest(path)
            path.write_bytes(b"after")
            with self.assertRaisesRegex(ValueError, "hash mismatch"):
                audit.checked(Path(temp), path.name, original)

    def snapshot(self, mask):
        return {"thread_group_affinity": {"available": True, "error": 0,
                "group": 0, "mask": mask, "observed_processor_group": 0,
                "observed_processor_number": 0}}

    def test_affinity_restoration_needs_actual_group_mask_equality(self):
        report = {"affinity_before": self.snapshot("0xff"),
                  "affinity_restored": self.snapshot("0x1"),
                  "affinity_restoration_ok": True}
        with self.assertRaisesRegex(ValueError, "affinity restoration"):
            audit.audit_affinity_restoration(report)

    def test_failed_restoration_is_retained_without_false_success(self):
        report = {"affinity_before": self.snapshot("0xff"),
                  "affinity_restored": self.snapshot("0x1"),
                  "affinity_restoration_ok": False}
        self.assertFalse(audit.audit_affinity_restoration(report))

    def test_false_percentile_claim_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "p99_ns"):
            audit.compare_claims({"p99_ns": 1}, {"p99_ns": 100})

    def test_false_budget_pass_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "callback_budget_ok"):
            audit.compare_claims({"callback_budget_ok": True}, {"callback_budget_ok": False})


if __name__ == "__main__":
    unittest.main()
