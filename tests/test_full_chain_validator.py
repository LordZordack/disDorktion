"""Negative tests for full-chain artifact trust boundaries (no DSP build needed)."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import array

MODULE = Path(__file__).resolve().parents[1]/"scripts"/"validate_full_chain_measurements.py"
spec = importlib.util.spec_from_file_location("full_chain_validator", MODULE)
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)


class FullChainValidatorTests(unittest.TestCase):
    def test_rejects_nested_nonfinite(self):
        for value in (float("nan"), float("inf"), -float("inf")):
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "nonfinite"):
                validator.finite_tree({"cases": [{"value": value}]})

    def test_rejects_capture_tamper(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path = root/"capture.f32"
            path.write_bytes(b"original capture")
            identity = validator.digest(path)
            path.write_bytes(b"altered capture")
            with self.assertRaisesRegex(ValueError, "hash mismatch"):
                validator.checked_file(root, "capture.f32", identity)

    def test_rejects_path_escape(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)/"capture"
            root.mkdir()
            path = root.parent/"outside.f32"
            path.write_bytes(b"external")
            with self.assertRaisesRegex(ValueError, "escapes"):
                validator.checked_file(root, "../outside.f32", validator.digest(path))

    def test_rejects_missing_measurement_cases(self):
        with self.assertRaisesRegex(ValueError, "incomplete measurement matrix"):
            validator.audit_measurements(Path("."), {"cases": [{}]})

    def test_rejects_reference_claim_from_diagnostic(self):
        with self.assertRaisesRegex(ValueError, "cannot qualify"):
            validator.audit_cpu(Path("."), {"reference_qualified": True})

    def test_rejects_changed_executable(self):
        with tempfile.TemporaryDirectory() as folder:
            executable = Path(folder)/"measure.exe"
            executable.write_bytes(b"rebuilt executable")
            with self.assertRaisesRegex(ValueError, "executable hash mismatch"):
                validator.audit_provenance({"provenance": {"executable_sha256": "0"*64}}, executable, Path(folder))

    def test_wet_impulse_detects_wrong_reported_latency(self):
        # Captured wet delay is 73 samples. A separately delayed dry lane could
        # agree with any reported integer; signed wet impulse delay cannot.
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            checks = []
            for rate in (48000,96000):
                for channels in (1,2):
                    settings = {
                        "input.gain.gainDb":0,"input.gain.mute":False,
                        "input.crusher.bitDepth":64,"input.crusher.captureRateHz":384000,
                        "input.crusher.mix":1,"input.crusher.crush":0,"input.crusher.shape":0,
                        "input.crusher.jitter":0,"input.crusher.jitterSeed":1,
                        "middle.tanh.driveDb":0,"middle.tanh.outputDb":0,
                        "middle.folder.driveDb":0,"middle.folder.threshold":1,"middle.folder.outputDb":0,
                        "output.highPass.cutoffHz":20,"output.lowPass.cutoffHz":20000,
                        "output.highPass.response":0,"output.lowPass.response":0,
                        "mix":1,"bypass":False,"finalGain.gainDb":0,"finalGain.mute":False,"finalGain.bypass":False,
                    }
                    for field in ("input.gain.bypass", "input.crusher.bypass", "middle.tanh.bypass", "middle.folder.bypass", "output.highPass.bypass", "output.lowPass.bypass"):
                        settings[field] = True
                    values = array.array("f", [0.]*(2*channels*4096))
                    for channel in range(channels):
                        amplitude = -.73 if channel else 1.
                        values[channel*4096] = amplitude
                        values[(channels+channel)*4096+73] = amplitude
                    if validator.sys.byteorder != "little":
                        values.byteswap()
                    name = f"latency-{rate}-{channels}.f32"
                    (root/name).write_bytes(values.tobytes())
                    checks.append(dict(rate=rate,channels=channels,frames=4096,settings=settings,lanes="source,bypassed_wet",
                                       file=name,sha256=validator.digest(root/name),sample_count=len(values),latency_samples=73))
            self.assertEqual(len(validator.audit_latency(root,checks)),6)
            checks[0]["latency_samples"] = 74
            with self.assertRaisesRegex(ValueError,"centroid differs"):
                validator.audit_latency(root,checks)


if __name__ == "__main__":
    unittest.main()
