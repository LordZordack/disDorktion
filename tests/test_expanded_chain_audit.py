import importlib.util
from pathlib import Path
import unittest
import array
import tempfile
import copy
import csv
import json

spec = importlib.util.spec_from_file_location("audit", Path(__file__).resolve().parents[1] / "scripts/validate_expanded_chain.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class RawAuditTests(unittest.TestCase):
    def fixture(self):
        config = {"id": 0, "scenario": "active-static", "dwell": 23, "ramp": 15,
                  "frames": 32, "capacity": 32, "rate": 48000, "ac_before": True,
                  "ac_after": True, "power_before": "scheme", "power_after": "scheme",
                  "latency_samples": 92,
                  "initial_settings": {"combBypass": 0, "dispersionBypass": 0, "toneBypass": 0,
                                       "combFeedback": .95, "combDelayMs": 40}}
        raw = [{"iteration": str(i), "elapsed_ns": "100", "residue": str(i % 101), "bypass": "0"} for i in range(200)]
        summary = {"count": "200", "p99_ns": "100", "fraction": str(100 / (32 / 48000 * 1e9)),
                   "deadline_ns": str(32 / 48000 * 1e9), "coverage_ok": "0", "power_stable": "1", "processing_ok": "1"}
        return raw, config, summary

    def test_short_distribution_cannot_qualify(self):
        result = audit.audit_case(*self.fixture())
        self.assertFalse(result["coverage_ok"])

    def test_rejects_forged_percentile(self):
        raw, config, summary = self.fixture()
        summary["p99_ns"] = "1"
        with self.assertRaisesRegex(ValueError, "p99 mismatch"):
            audit.audit_case(raw, config, summary)

    def test_rejects_wrong_bypass_schedule(self):
        raw, config, summary = self.fixture()
        raw[0]["bypass"] = "1"
        with self.assertRaisesRegex(ValueError, "bypass schedule"):
            audit.audit_case(raw, config, summary)

    def test_rejects_inactive_effect_claim(self):
        raw, config, summary = self.fixture()
        config["initial_settings"]["combBypass"] = 1
        with self.assertRaisesRegex(ValueError, "effects not configured"):
            audit.audit_case(raw, config, summary)

    def test_session_power_change_remains_auditable_failure(self):
        raw, config, summary = self.fixture()
        summary["power_stable"] = "0"
        result = audit.audit_case(raw, config, summary, session_power="different-scheme")
        self.assertFalse(result["power_ok"])

    def test_partial_matrix_must_be_a_collection_prefix(self):
        config = {"id": 0, "run": 2, "rate": 96000, "capacity": 1024, "scenario": "quiet-decay"}
        with self.assertRaisesRegex(ValueError, "prefix"):
            audit.validate_matrix([config], 3, False)

    def test_partial_prefix_never_counts_as_complete(self):
        config = {"id": 0, "run": 0, "rate": 48000, "capacity": 32, "scenario": "warm-bypass"}
        audit.validate_matrix([config], 3, False)
        with self.assertRaisesRegex(ValueError, "incomplete"):
            audit.validate_matrix([config], 3, True)

    def test_exactly_25_percent_remains_a_failure(self):
        raw, config, summary = self.fixture()
        config["rate"] = 8000
        for row in raw:
            row["elapsed_ns"] = "1000000"
        summary.update(p99_ns="1000000", fraction="0.25", deadline_ns="4000000")
        self.assertFalse(audit.audit_case(raw, config, summary)["budget_ok"])


class MethodAuditTests(unittest.TestCase):
    def policy_report(self):
        before = {"process_power_throttling": {"available": True, "error": 0, "version": 1,
                                              "control_mask": 4, "state_mask": 5},
                  "thread_power_throttling": {"available": False, "reason": "unsupported"},
                  "thread_group_affinity": {"available": True, "error": 0, "group": 0,
                                            "mask": "0x1", "observed_processor_group": 0,
                                            "observed_processor_number": 0}}
        applied = copy.deepcopy(before)
        applied["process_power_throttling"].update(control_mask=5, state_mask=4)
        return {"method_id": "windows-explicit-highqos-v1", "pinned": True, "affinity_verified": True,
                "scheduling_before": copy.deepcopy(before), "scheduling_after": copy.deepcopy(applied),
                "scheduling_after_restore": copy.deepcopy(before), "collection_policy_ok": True,
                "provenance": {"sources": [{"path": "tools/ExpandedProcessPolicy.h"}]},
                "affinity": "selected_group=0 selected_mask=0x1 previous_thread_mask=0xffff",
                "process_policy": {"requested": True, "set_attempted": True, "set_succeeded": True,
                                   "set_error": 0, "before_request": before, "after_request": applied,
                                   "application_ok": True, "restore_attempted": True,
                                   "restore_succeeded": True, "restore_error": 0,
                                   "after_restore": copy.deepcopy(before), "restoration_ok": True},
                "cases": [{"method_id": "windows-explicit-highqos-v1", "policy_ok": True,
                           "scheduling_before": copy.deepcopy(applied),
                           "scheduling_after": copy.deepcopy(applied)}]}

    def test_legacy_method_remains_auditable(self):
        self.assertTrue(audit.audit_method({"cases": [{}]}))

    def test_unknown_or_null_method_is_rejected(self):
        for method in ("future-unreviewed-policy", None):
            with self.subTest(method=method), self.assertRaisesRegex(ValueError, "method"):
                audit.audit_method({"method_id": method})

    def test_explicit_policy_checks_process_not_unavailable_thread_query(self):
        self.assertTrue(audit.audit_method(self.policy_report()))

    def test_process_policy_method_version_is_fixed(self):
        report = self.policy_report()
        snapshots = [report[name] for name in ("scheduling_before", "scheduling_after", "scheduling_after_restore")]
        snapshots += [report["process_policy"][name] for name in ("before_request", "after_request", "after_restore")]
        snapshots += [report["cases"][0][name] for name in ("scheduling_before", "scheduling_after")]
        for snapshot in snapshots:
            snapshot["process_power_throttling"]["version"] = 0
        with self.assertRaisesRegex(ValueError, "policy"):
            audit.audit_method(report)

    def test_thread_query_metadata_is_retained_without_requiring_availability(self):
        report = self.policy_report()
        thread = report["cases"][0]["scheduling_after"]["thread_power_throttling"]
        thread.clear()
        thread.update(available=False, error=50)
        self.assertTrue(audit.audit_method(report))
        thread.update(available=True, error=0, version=1, control_mask=0, state_mask=0)
        self.assertTrue(audit.audit_method(report))
        for field in ("error", "version", "control_mask", "state_mask"):
            broken = copy.deepcopy(report)
            del broken["cases"][0]["scheduling_after"]["thread_power_throttling"][field]
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "policy"):
                audit.audit_method(broken)
        thread.clear()
        thread["available"] = False
        with self.assertRaisesRegex(ValueError, "policy"):
            audit.audit_method(report)

    def test_declared_default_method_remains_auditable(self):
        report = self.policy_report()
        report["method_id"] = "windows-system-managed-v2"
        report["cases"][0]["method_id"] = report["method_id"]
        report["process_policy"].update(requested=False, set_attempted=False, set_succeeded=False,
                                        restore_attempted=False, restore_succeeded=False)
        self.assertTrue(audit.audit_method(report))

    def test_unrelated_flags_and_system_managed_zero_are_restored(self):
        report = self.policy_report()
        policy = report["process_policy"]
        policy["before_request"]["process_power_throttling"].update(control_mask=0, state_mask=0)
        policy["after_restore"]["process_power_throttling"].update(control_mask=0, state_mask=0)
        report["scheduling_before"]["process_power_throttling"].update(control_mask=0, state_mask=0)
        report["scheduling_after_restore"]["process_power_throttling"].update(control_mask=0, state_mask=0)
        for snapshot in [policy["after_request"], report["cases"][0]["scheduling_before"],
                         report["cases"][0]["scheduling_after"], report["scheduling_after"]]:
            snapshot["process_power_throttling"].update(control_mask=1, state_mask=0)
        self.assertTrue(audit.audit_method(report))

    def test_forged_application_or_restoration_claims_are_rejected(self):
        for field, value in (("requested", False), ("set_attempted", False), ("set_succeeded", False),
                             ("set_error", 5), ("restore_attempted", False),
                             ("restore_succeeded", False), ("restore_error", 5)):
            report = self.policy_report()
            report["process_policy"][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "policy"):
                audit.audit_method(report)

    def test_process_flag_tampering_is_rejected_at_every_snapshot(self):
        for snapshot in ("after_request", "after_restore", "scheduling_before", "scheduling_after"):
            for field, value in (("available", False), ("error", 5), ("version", 2),
                                 ("control_mask", 1), ("state_mask", 1)):
                report = self.policy_report()
                owner = report["process_policy"] if snapshot.startswith("after_") else report["cases"][0]
                owner[snapshot]["process_power_throttling"][field] = value
                with self.subTest(snapshot=snapshot, field=field), self.assertRaisesRegex(ValueError, "policy"):
                    audit.audit_method(report)

    def test_actual_affinity_tampering_is_rejected_at_every_snapshot(self):
        for snapshot in ("after_request", "after_restore", "scheduling_before", "scheduling_after"):
            for field, value in (("available", False), ("error", 5), ("group", 1), ("mask", "0x2")):
                report = self.policy_report()
                owner = report["process_policy"] if snapshot.startswith("after_") else report["cases"][0]
                owner[snapshot]["thread_group_affinity"][field] = value
                with self.subTest(snapshot=snapshot, field=field), self.assertRaisesRegex(ValueError, "policy"):
                    audit.audit_method(report)

    def test_wrong_actual_pinned_processor_is_unqualified(self):
        report = self.policy_report()
        report["affinity"] = "selected_group=0 selected_mask=0x2"
        report["affinity_verified"] = False
        self.assertFalse(audit.audit_method(report))

    def test_stripping_method_cannot_hide_explicit_policy(self):
        report = self.policy_report()
        del report["method_id"]
        with self.assertRaisesRegex(ValueError, "method"):
            audit.audit_method(report)
        del report["process_policy"]
        del report["cases"][0]["method_id"]
        with self.assertRaisesRegex(ValueError, "method"):
            audit.audit_method(report)

    def test_default_method_cannot_claim_an_explicit_request(self):
        report = self.policy_report()
        report["method_id"] = "windows-system-managed-v2"
        report["cases"][0]["method_id"] = report["method_id"]
        with self.assertRaisesRegex(ValueError, "policy"):
            audit.audit_method(report)

    def test_explicit_method_requires_policy_source_provenance(self):
        report = self.policy_report()
        report["provenance"]["sources"] = []
        with self.assertRaisesRegex(ValueError, "provenance"):
            audit.audit_method(report)

    def test_observed_processor_outside_actual_mask_is_rejected(self):
        report = self.policy_report()
        report["cases"][0]["scheduling_after"]["thread_group_affinity"]["observed_processor_number"] = 1
        with self.assertRaisesRegex(ValueError, "policy"):
            audit.audit_method(report)

    def test_unsupported_process_api_remains_auditable_failure(self):
        report = self.policy_report()
        policy = report["process_policy"]
        policy.update(set_attempted=False, set_succeeded=False, set_error=50, application_ok=False,
                      restore_attempted=False, restore_succeeded=False, restoration_ok=False)
        for name in ("before_request", "after_request", "after_restore"):
            policy[name]["process_power_throttling"] = {"available": False, "reason": "unsupported"}
        for name in ("scheduling_before", "scheduling_after", "scheduling_after_restore"):
            report[name]["process_power_throttling"] = {"available": False, "reason": "unsupported"}
        for name in ("scheduling_before", "scheduling_after"):
            report["cases"][0][name]["process_power_throttling"] = {"available": False, "reason": "unsupported"}
        report["cases"][0]["policy_ok"] = False
        report["collection_policy_ok"] = False
        self.assertFalse(audit.audit_method(report))

    def test_mixed_method_cases_and_missing_snapshots_are_rejected(self):
        report = self.policy_report()
        report["cases"][0]["method_id"] = "windows-system-managed-v2"
        with self.assertRaisesRegex(ValueError, "method"):
            audit.audit_method(report)
        report = self.policy_report()
        del report["cases"][0]["scheduling_after"]
        with self.assertRaisesRegex(ValueError, "policy"):
            audit.audit_method(report)

    def test_honest_failed_application_remains_auditable_failure(self):
        report = self.policy_report()
        report["process_policy"].update(set_succeeded=False, set_error=5, application_ok=False)
        report["cases"][0]["policy_ok"] = False
        report["collection_policy_ok"] = False
        self.assertFalse(audit.audit_method(report))

    def test_final_collection_policy_is_recomputed(self):
        report = self.policy_report()
        report["scheduling_after"]["process_power_throttling"]["state_mask"] = 5
        with self.assertRaisesRegex(ValueError, "policy"):
            audit.audit_method(report)
        report["collection_policy_ok"] = False
        self.assertFalse(audit.audit_method(report))

    def test_final_readback_must_match_recorded_restore_snapshot(self):
        report = self.policy_report()
        report["scheduling_after_restore"]["process_power_throttling"]["control_mask"] = 0
        with self.assertRaisesRegex(ValueError, "policy"):
            audit.audit_method(report)

    def test_honest_failed_restoration_remains_auditable_failure(self):
        report = self.policy_report()
        report["process_policy"].update(restore_succeeded=False, restore_error=5, restoration_ok=False)
        self.assertFalse(audit.audit_method(report))

    def test_case_policy_claim_is_recomputed(self):
        report = self.policy_report()
        report["cases"][0]["policy_ok"] = False
        with self.assertRaisesRegex(ValueError, "policy"):
            audit.audit_method(report)

    def test_report_policy_gate_and_legacy_raw_gate_are_integrated(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        root = Path(temp.name)
        raw, config, summary = RawAuditTests.fixture(self)
        config.update(run=0, scenario="warm-bypass")
        config["initial_settings"].update(combBypass=1, dispersionBypass=1, toneBypass=1)
        for row in raw:
            row.update(case="0", bypass="1")
        summary["case"] = "0"
        for name, rows in (("timings.csv", raw), ("summary.csv", [summary])):
            with (root / name).open("w", newline="") as f:
                writer = csv.DictWriter(f, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
        executable = root / "collector.exe"
        executable.write_bytes(b"frozen test binary")
        sources = []
        for name in ("tools/ExpandedChainQualification.cpp", "src/dsp/FullChain.cpp", "src/dsp/OutputSection.cpp"):
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("frozen test source")
            sources.append({"path": name, "sha256": audit.digest(path)})
        report = {"kind": "expanded-chain-cpu", "cases": [config], "diagnostic": False,
                  "quick": False, "complete": False, "runs": 3, "pinned": True,
                  "hybrid_established": True, "on_ac": True, "power": "scheme",
                  "reference_qualified": False, "timings_sha256": audit.digest(root / "timings.csv"),
                  "summary_sha256": audit.digest(root / "summary.csv"),
                  "provenance": {"executable_sha256": audit.digest(executable), "sources": sources,
                                 "build_configuration": "Release"}}
        (root / "result.json").write_text(json.dumps(report))
        self.assertFalse(audit.audit(root, executable, root)["reference_qualified"])
        report["method_id"] = "future-unreviewed-policy"
        (root / "result.json").write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError, "method"):
            audit.audit(root, executable, root)


class StressCaptureAuditTests(unittest.TestCase):
    def capture(self, values, **changes):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        path = Path(temp.name) / "capture.f32"
        path.write_bytes(array.array("f", values).tobytes())
        case = {"rate": 10, "frames": 210, "latency_samples": 92, "peak": .5,
                "peak_limit": 8192, "residual_after_20s": 0, "finite": True, "channel_isolated": True}
        case.update(changes)
        return path, case

    def test_raw_stress_capture_recomputes_peaks(self):
        path, case = self.capture([.5, 0] + [0] * 418)
        self.assertTrue(audit.audit_stress_capture(path, case))

    def test_latency_regression_rejected(self):
        path, case = self.capture([.5, 0] + [0] * 418, latency_samples=93)
        with self.assertRaisesRegex(ValueError, "latency"):
            audit.audit_stress_capture(path, case)

    def test_channel_leak_rejected(self):
        path, case = self.capture([.5, .01] + [0] * 418)
        with self.assertRaisesRegex(ValueError, "channel"):
            audit.audit_stress_capture(path, case)

    def test_forged_stress_peak_rejected(self):
        path, case = self.capture([.75, 0] + [0] * 418)
        with self.assertRaisesRegex(ValueError, "peak"):
            audit.audit_stress_capture(path, case)

    def test_truncated_capture_rejected(self):
        path, case = self.capture([.5, 0])
        with self.assertRaisesRegex(ValueError, "length"):
            audit.audit_stress_capture(path, case)

    def test_incomplete_decay_trace_rejected(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        path = Path(temp.name) / "decay.csv"
        path.write_text("frame,elapsed_ns,peak\n310,100,0\n")
        with self.assertRaisesRegex(ValueError, "decay"):
            audit.audit_decay_trace(path, {"rate": 10, "frames": 2})


if __name__ == "__main__":
    unittest.main()
