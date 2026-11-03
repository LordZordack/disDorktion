import cmath
import hashlib
import importlib.util
import math
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('dispersion_validator', Path(__file__).parents[1]/'scripts/validate_dispersion_measurements.py')
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)

class DispersionValidatorTests(unittest.TestCase):
    def fixture(self, root):
        values = [0.] * 128
        values[32] = 1.
        data = struct.pack('<128f', *values)
        (root/'impulse.f32').write_bytes(data)
        spectrum = []
        for index in range(129):
            w = math.pi*index/128
            h = cmath.exp(-32j*w)
            spectrum.append({'radians': w, 'real': h.real, 'imag': h.imag, 'group_delay_samples': 32})
        return {'rate': 8000, 'frequency_hz': 2000, 'mix': 1, 'coefficient': validator.coefficient(2000,8000), 'file': 'impulse.f32', 'sha256': hashlib.sha256(data).hexdigest(), 'frames':128, 'reported_latency_samples':0, 'peak_after_one_second':0, 'partition_bit_exact':True, 'spectrum':spectrum}

    def test_exact_delay_passes_independent_audit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            validator.audit_case(self.fixture(root), root)

    def test_corrupt_group_delay_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            row = self.fixture(root)
            row['spectrum'][0]['group_delay_samples'] = 33
            with self.assertRaisesRegex(ValueError, 'group delay'):
                validator.audit_case(row, root)

    def test_hash_tampering_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            row = self.fixture(root)
            (root/'impulse.f32').write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError, 'hash'):
                validator.audit_case(row, root)

    def test_insufficient_capture_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            row = self.fixture(root)
            row['frequency_hz'] = 20
            row['coefficient'] = validator.coefficient(20, 8000)
            with self.assertRaisesRegex(ValueError, 'capture'):
                validator.audit_case(row, root)

    def test_ramp_retarget_and_identical_target(self):
        ramp = validator.Ramp(0, 4)
        ramp.set(1)
        self.assertEqual(ramp.next(), .25)
        ramp.set(1)
        self.assertEqual(ramp.next(), .5)
        ramp.set(0)
        self.assertEqual([ramp.next() for _ in range(4)], [.375, .25, .125, 0])

if __name__ == '__main__':
    unittest.main()
