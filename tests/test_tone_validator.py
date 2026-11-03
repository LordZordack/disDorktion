import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('tone_validator', Path(__file__).parents[1]/'scripts/validate_tone_measurements.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class ToneValidatorTests(unittest.TestCase):
    def test_bad_response_is_rejected(self):
        with self.assertRaises(ValueError):
            module.check_response(48000, 6, 1000, complex(2, 0))

    def test_nonfinite_response_is_rejected(self):
        with self.assertRaises(ValueError):
            module.check_response(48000, 0, 1000, complex(float('nan'), 0))

    def test_neutral_and_pivot(self):
        self.assertEqual(module.check_response(48000, 0, 1000, complex(1, 0)), 0)
        g = 10**(6/20)
        phase = complex(g, 1) # construct independently from analog numerator/denominator
        expected = complex(1, g)/phase
        self.assertLess(module.check_response(48000, 6, 1000, expected), 1e-12)

if __name__ == '__main__':
    unittest.main()
