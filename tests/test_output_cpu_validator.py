"""Bounded rejection checks for Output CPU reference evidence (no benchmark run)."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import validate_output_cpu as validator


def reference_row() -> dict[str, str]:
    # A valid observed-format fixture, deliberately independent of the actual
    # reference-machine model, power scheme, or performance measurements.
    return {
        "mode": "baseline", "qualified": "true", "reason": "", "timing_count": "100000",
        "measurement_seconds": "10.000", "warmup_projected_callbacks": "200000.000",
        "calibrated_callbacks": "190000.000", "compiler": "MSVC 1951 Release x64",
        "build": "test-abcdef-Release", "cpu": "Fixture Hybrid CPU",
        "ac_status": "before=AC;after=AC", "power_scheme_guid": "{01234567-89ab-cdef-0123-456789abcdef}",
        "conditions": "Fixture conditions; isolated run", "thermal_measurement": "unavailable: no thermal sensor measurement",
        "affinity": "selected_group=0 selected_mask=0x1 previous_thread_mask=0xf selected_efficiency_class=1",
        "topology": "class=1/group=0/mask=0x3;class=0/group=0/mask=0xc; process_mask=0xf",
        "processing_ok": "true", "raw_p99_within_budget": "true", "scenario": "automation",
    }


def complete_coverage() -> dict[str, dict]:
    return {name: validator.coverage([100] * size) for name, size in
            (("cutoffs", 101), ("lowCutoffs", 101), ("responseBypass", 36), ("bypass", 4), ("retainedCommon", 8))}


class ReferenceCpuGates(unittest.TestCase):
    def test_reference_requires_stereo_and_three_complete_runs(self) -> None:
        validator.verify_reference_options(2, 3)
        validator.verify_reference_options(2, 4)
        for channels, runs in ((1, 3), (2, 2), (2, 0)):
            with self.subTest(channels=channels, runs=runs), self.assertRaises(ValueError):
                validator.verify_reference_options(channels, runs)

    def test_capture_accepts_boundaries_and_preserves_quick_diagnostic(self) -> None:
        row = reference_row()
        validator.verify_capture_mode(row, True)
        row.update(timing_count="150000", measurement_seconds="120.001")
        validator.verify_capture_mode(row, True)
        row.update(mode="quick", qualified="false", reason="quick smoke run; unqualified",
                   timing_count="1", measurement_seconds="0.200")
        validator.verify_capture_mode(row, False)
        with self.assertRaises(ValueError):
            validator.verify_capture_mode(row, True)

    def test_reference_capture_rejects_forged_qualification_short_windows_and_counts(self) -> None:
        mutations = (
            {"qualified": "false"}, {"qualified": "True"}, {"reason": "filtered case run"},
            {"mode": "diagnostic"}, {"mode": "quick"}, {"timing_count": "99999"},
            {"timing_count": "150001"}, {"timing_count": "200000"},
            {"measurement_seconds": "9.999"}, {"measurement_seconds": "120.002"},
            {"measurement_seconds": "nan"}, {"measurement_seconds": "inf"},
            {"warmup_projected_callbacks": "0"}, {"calibrated_callbacks": "0"},
        )
        for mutation in mutations:
            row = reference_row() | mutation
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                validator.verify_capture_mode(row, True)

    def test_environment_accepts_recorded_model_and_unavailable_thermals(self) -> None:
        validator.verify_reference_environment(reference_row())
        validator.verify_reference_environment(reference_row() | {"cpu": "Another documented reference CPU"})

    def test_environment_rejects_debug_battery_and_missing_provenance(self) -> None:
        mutations = (
            {"compiler": "MSVC Debug x64"}, {"build": "test-abcdef-Debug"}, {"cpu": ""},
            {"cpu": "unavailable"}, {"ac_status": "before=AC;after=battery"},
            {"ac_status": "before=battery;after=AC"}, {"ac_status": "before=unknown;after=AC"},
            {"power_scheme_guid": "unavailable"}, {"power_scheme_guid": "malformed"},
            {"conditions": " "}, {"thermal_measurement": ""}, {"affinity": "unpinned diagnostic"},
            {"affinity": "selected_group=0 selected_mask=0x3 previous_thread_mask=0xf selected_efficiency_class=1"},
            {"affinity": "selected_group=0 selected_mask=0x0 previous_thread_mask=0xf selected_efficiency_class=1"},
            {"affinity": "selected_group=0 selected_mask=0x4 previous_thread_mask=0xf selected_efficiency_class=0"},
            {"affinity": "selected_group=0 selected_mask=0x8 previous_thread_mask=0xf selected_efficiency_class=1"},
            {"affinity": "selected_group=1 selected_mask=0x1 previous_thread_mask=0xf selected_efficiency_class=1"},
            {"affinity": "selected_group=0 selected_mask=0x1 previous_thread_mask=0x0 selected_efficiency_class=1"},
            {"topology": "class=1/group=0/mask=0x3; process_mask=0xf"},
            {"topology": "class=1/group=0/mask=0x3;class=0/group=0/mask=0xc; process_mask=0xc"},
            {"topology": "malformed"},
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                validator.verify_reference_environment(reference_row() | mutation)

    def test_raw_cpu_gate_is_strict_and_processing_failure_is_rejected(self) -> None:
        row, counts = reference_row(), complete_coverage()
        validator.verify_reference_outcome(row, 9999, 100000, counts)
        for p99 in (10000, 10001):
            with self.subTest(p99=p99), self.assertRaises(ValueError):
                validator.verify_reference_outcome(row, p99, 100000, counts)
        with self.assertRaises(ValueError):
            validator.verify_reference_outcome(row | {"processing_ok": "false"}, 9999, 100000, counts)

    def test_each_raw_control_gate_rejects_a_sparse_or_missing_state(self) -> None:
        for scenario, names in (("automation", ("cutoffs", "lowCutoffs", "responseBypass")),
                                ("bypass", ("bypass",)), ("common-bypass", ("retainedCommon",))):
            row = reference_row() | {"scenario": scenario}
            validator.verify_reference_outcome(row, 9999, 100000, complete_coverage())
            for name in names:
                for missing_count in (0, 99):
                    counts = complete_coverage()
                    slots = {"cutoffs": 101, "lowCutoffs": 101, "responseBypass": 36, "bypass": 4, "retainedCommon": 8}[name]
                    counts[name] = validator.coverage([100] * (slots - 1) + [missing_count])
                    with self.subTest(scenario=scenario, gate=name, missing_count=missing_count), self.assertRaises(ValueError):
                        validator.verify_reference_outcome(row, 9999, 100000, counts)

    def test_forged_automation_schedule_is_rejected(self) -> None:
        for event in ((0, 0, 0, 0, 0), (8, 2, 1, 0, 0), (9, 0, 0, 1, 0), (35, 2, 1, 1, 1)):
            validator.verify_automation_event(*event)
        for event in ((0, 1, 1, 0, 0), (0, 0, 0, 1, 0), (9, 0, 0, 0, 0)):
            with self.subTest(event=event), self.assertRaises(ValueError):
                validator.verify_automation_event(*event)

    def test_reference_identity_must_match_across_all_three_runs(self) -> None:
        row = reference_row() | {"architecture": "processor_architecture=9; logical_processors=4",
                                 "physical_memory": "32000000000 bytes", "os": "Windows 10.0.26300",
                                 "sdk": "10.0.26100.0", "juce": "JUCE v8.0.14"}
        validator.verify_reference_session([row.copy() for _ in range(3)])
        with self.assertRaises(ValueError):
            validator.verify_reference_session([])
        for field in ("cpu", "architecture", "physical_memory", "os", "sdk", "compiler", "build", "juce", "topology", "affinity"):
            rows = [row.copy() for _ in range(3)]
            rows[2][field] = "changed identity"
            with self.subTest(field=field), self.assertRaises(ValueError):
                validator.verify_reference_session(rows)


if __name__ == "__main__":
    unittest.main()
