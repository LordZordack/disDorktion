"""Independent comb audit negative tests; no native executable needed."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
spec = importlib.util.spec_from_file_location('comb_validator', Path(__file__).resolve().parents[1]/'scripts/validate_comb_measurements.py')
v = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v)

class CombValidatorTests(unittest.TestCase):
    def test_nonfinite(self):
        with self.assertRaisesRegex(ValueError, 'nonfinite'): v.finite_tree({'x':[float('nan')]})
    def test_tamper(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'x';p.write_bytes(b'old');h=v.digest(p);p.write_bytes(b'new')
            with self.assertRaisesRegex(ValueError,'hash mismatch'):v.checked_file(Path(d),'x',h)
    def test_escape(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaisesRegex(ValueError,'escapes'):v.checked_file(Path(d),'../x','0'*64)
    def test_matrix(self):
        with self.assertRaisesRegex(ValueError,'matrix'):v.audit_measurements(Path('.'),{'cases':[]})
    def test_forged_qualification(self):
        with self.assertRaisesRegex(ValueError,'cannot qualify'):v.audit_cpu(Path('.'),{'reference_qualified':True})
    def test_reference_impulse_signed_feedback(self):
        s=dict(delayMs=2,feedback=-.5,feedforward=.25,dampingHz=6000,mix=1,dampingEnabled=False,bypass=False)
        y=v.reference([1.]+[0.]*9,1000,s)
        self.assertEqual(y,[1,0,-.25,0,.125,0,-.0625,0,.03125,0])
    def test_response_notch(self):
        s=dict(delayMs=2,feedback=0,feedforward=-1,dampingHz=6000,mix=1,dampingEnabled=False,bypass=False)
        self.assertEqual(v.transfer(1000,s,0),0j)
    def test_residual_window_rejects_short_capture(self):
        s=dict(delayMs=40,feedback=.95,feedforward=1,dampingHz=20,mix=1,dampingEnabled=True,bypass=False)
        self.assertGreater(v.tail_bound(48000,s),11)
    def test_large_delay_decay_pole_does_not_overflow(self):
        import math
        for enabled in (False,True):
            with self.subTest(damping_enabled=enabled):
                s=dict(delayMs=40,feedback=.95,feedforward=0,dampingHz=20,mix=1,dampingEnabled=enabled,bypass=False)
                pole=v.decay_rate(384000,s)
                self.assertTrue(0<pole<1)
                a=v.coefficient(384000,s)
                characteristic_log=math.log(.95)+math.log1p(-a)-math.log1p(-a/pole)-15360*math.log(pole)
                self.assertLess(abs(characteristic_log),1e-10)
                if not enabled:self.assertAlmostEqual(pole,math.exp(math.log(.95)/15360),places=15)
                self.assertTrue(math.isfinite(v.tail_bound(384000,s)))
    def test_impulse_corruption_detected(self):
        with self.assertRaisesRegex(ValueError,'impulse'):v.compare_impulse([1,0,.2],[1,0,.5])
    def test_quick_reference_rejected(self):
        with self.assertRaisesRegex(ValueError,'three full runs'):
            v.audit_cpu(Path('.'),dict(reference_qualified=True,kind='resonant-comb-cpu-reference',runs=3,quick=True))
    def test_unpinned_reference_rejected(self):
        report=dict(reference_qualified=True,kind='resonant-comb-cpu-reference',runs=3,quick=False,environment=dict(pinned=False,hybrid_established=True,on_ac=True))
        with self.assertRaisesRegex(ValueError,'pinned AC'):v.audit_cpu(Path('.'),report)
    def test_fractional_transfer_matches_independent_impulse_transform(self):
        import cmath
        import math
        s=dict(delayMs=2.5,feedback=-.7,feedforward=.3,dampingHz=100,mix=.8,dampingEnabled=True,bypass=False)
        data=v.reference([1.]+[0.]*2047,1000,s)
        for hz in (0,71,211,450):
            actual=sum(y*cmath.exp(-2j*math.pi*hz*n/1000) for n,y in enumerate(data))
            self.assertLess(abs(actual-v.transfer(1000,s,hz)),1e-12)

if __name__=='__main__':unittest.main()
