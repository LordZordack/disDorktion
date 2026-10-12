---
type: Method
title: "Wave folding"
description: "Reviewed symmetric triangle wave-folder design, controls, transitions, and pre-implementation acceptance limits."
timestamp: 2026-10-11T00:00:00Z
tags: ["audio-dsp", "wave-folding", "waveshaping", "aliasing"]
resource: "https://github.com/LordZordack/disDorktion/issues/7"
---

# Wave folding

Design accepted for implementation on 2026-10-11 following Antigravity review. This page records the transfer and acceptance limits before implementation; it makes no measurement or listening acceptance claim. It informs the [DisDorktion project](../projects/disdorktion.md), follows the [processing contract](./processing_contract.md), and complements [tanh saturation](./tanh_saturation.md). See the [validation record](../notes/wave_folder_validation.md) for evidence.

The native-rate module is independent of the future shared Q1 oversampling boundary. It can be prepared from 8 through 384 kHz under the F1 contract. Q1/S2 changes to transfer, controls, or anti-aliasing require renewed affected validation.
## Folding design and alternatives

1. **Recommended: symmetric periodic triangle folding.** It has an exact linear region, explicit fold thresholds, a bounded wet output, and a direct independent mathematical oracle. Increasing drive traverses successive folds rather than promising monotonically increasing distortion or loudness. Sharp slope reversals make native-rate aliasing a quality risk to measure.
2. **Sine folding:** `T * sin(a*x/T)` gives smooth periodic turns and small-signal slope `a`, but bends samples below the first turn and has no exact finite linear region. It offers a different sound and is a possible later comparison if listening rejects the triangle design.
3. **Virtual analog Buchla-style modeling:** richer circuit-derived folding with branch weights and filtering, but introduces additional calibration and validation requirements. Issue #7 does not request reproduction of a particular hardware unit.

The triangle function below is a project design derived from reflected linear segments, not the Buchla paper's circuit model. Research context: Esqueda, Pöntynen, Välimäki, and Parker, [Virtual Analog Buchla 259 Wavefolder](https://dafx.de/paper-archive/2017/papers/DAFx17_paper_82.pdf), DAFx-17, pp. 192-199. The inspected source explains circuit-derived folding, corners and aliasing, and polyBLAMP with oversampling; its results do not establish our transfer function, budgets, or quality acceptance. During implementation, preserve verified paper metadata and a focused source breakdown under `docs/okf/papers/virtual_analog_buchla_259_wavefolder.md`.

## Controls and exact behavior

Proposed typed settings: `WaveFolderSettings { driveDb, threshold, outputDb, bypass }`.

| Control | Valid range | Default | Meaning |
| --- | --- | --- | --- |
| `driveDb` | -24 to +36 dB, inclusive | 0 dB | Input gain `a = 10^(driveDb/20)` |
| `threshold` | 0.05 to 1.0, inclusive | 1.0 | First folding threshold and untrimmed wet ceiling `T`, in linear amplitude |
| `outputDb` | -60 to +12 dB, inclusive | 0 dB | Explicit wet trim `g = 10^(outputDb/20)` |
| `bypass` | false / true | false | Smoothed transition to the original input |

Let `z = a*x`. The odd function `F(z,T)` is linear for `abs(z) <= T`. Otherwise evaluate the positive-magnitude phase `r = fmod(abs(z)/T, 4)`, select its segment, and restore the input sign:

```text
F(z,T) = z                                      when abs(z) <= T
positive-magnitude segment value:
    h(r) = r                                    for 0 <= r <= 1
    h(r) = 2-r                                  for 1 < r <= 3
    h(r) = r-4                                  for 3 < r < 4
F(z,T) = sign(z) * T * h(r)                     otherwise
wet = g * F(z,T)
output = b*x + (1-b)*wet
```

`sign(z)` multiplies the signed segment value; do not use `copysign` on `h(r)` because negative segments must remain negative. Fold corners occur at odd multiples of T, zeros at even multiples. At T=1: F(0)=0, F(1)=1, F(2)=0, F(3)=-1, F(4)=0, F(5)=1, with odd symmetry for negative inputs. The transfer is continuous with alternating slopes +1/-1. Settled wet magnitude is bounded by `g*T`; maximum is approximately 3.981072. Lower threshold also lowers the wet ceiling; there is no automatic normalization or loudness compensation. Preserve the harness overload indication.

Implement the modulo branches as `if (r <= 1)`, `else if (r <= 3)`, `else`; the final branch must always assign `r-4`. The positive finite divisor makes fmod's result finite and nonnegative. Sign is multiplied using the original input sign.

At default controls, every input in [-1,1] passes exactly, including both endpoints. Outside that range the default still folds. Bypass excludes all three wet controls and returns the original sample exactly when settled. Require explicit endpoint branches for b=0 and b=1. During bypass transitions the dry signal can exceed the wet ceiling; do not apply a limiter.

Reject the whole parameter update for any nonfinite or out-of-range scalar, retaining prior settings and ramp progress. Cache converted drive/trim gains when their settings change. Use independent double linear ramps for a, T, g, and b over `floor(sampleRate*0.010)` frames. Advance before applying the first frame, once per frame for all channels, including while bypassed. Identical targets preserve progress; interrupted ramps begin from their current values. Reset/preparation snaps to retained targets. Empty blocks do not advance controls. No audio history, zero reported latency, and independent instance envelopes.

Double intermediates keep multiplication and division finite for all finite float input values. Require finite, bounded wet output and exact settled bypass even at float extremes. Curve-accuracy acceptance covers input [-4,4] over all control endpoints; huge finite inputs are safety cases only because periodic phase loses resolution once the quotient exceeds double precision. Arbitrary NaN/Inf audio sanitization remains outside the existing contract. Silence stays exact zero.

## Acceptance limits established before implementation

- **Curve and folds:** maximum sample error `2e-7 * max(1,g*T)` over [-4,4], signed tiny values, all control endpoints, and representable neighbors on both sides of fold corners. Use an independent cell/parity oracle in long double: for v=abs(a*x)/T, k=floor((v+1)/2), q=v-2*k, the positive-input result is `T*q` for even k and `-T*q` for odd k. Restore input sign by multiplication. The reference must not call the production helper or duplicate its fmod branches. Exact silence, exact default identity in [-1,1], exact settled bypass, odd symmetry, and wet bounds are separate checks. Do not add tanh's monotonic-drive assertion to a periodic folder.
- **Continuity:** test signed corners `(2*k+1)*T/a` across the declared curve envelope using float neighbors and independent expected slopes. For fixed controls the mathematical bound is `abs(deltaWet) <= g*a*abs(deltaInput)`; allow twice the curve tolerance for conversion error. Include modulo-wrap points at even multiples as well as peaks. Evaluate input rounding explicitly rather than asserting that an unrepresentable corner is exact float input.
- **Transitions:** compare drive, threshold, trim, and bypass against independent frame interpolation; exercise simultaneous/interrupted ramps, updates while bypassed, repeated targets, empty blocks, and reset/reprepare. Equal control-event frame positions must give sample-identical output for different block partitions.
- **F1 matrix:** mono/stereo at 44.1/48/88.2/96/192 kHz, capacities 1/32/64/127/256/1024; additionally 8/384 kHz boundaries and 65536 capacity. Include varying/oversized blocks, unprepared/layout failures without sample/history modification, failed preparation retaining prior valid preparation, deterministic reset, independent instances, and distinct/opposite-sign stereo input.
- **Realtime:** zero tracked C++ allocation/deallocation during prepared process, parameter updates, and reset in DSP, harness, and device callback coverage. Inspect the paths for locks, file/logging access, UI calls, and unbounded fold-by-fold loops. Allocation tracking does not prove every allocator is intercepted. Keep denormal suppression scoped to existing callback/offline entry points.
- **Native-rate spectra:** at 48/96 kHz use 65536-frame coherent single-sine fixtures at bins 1365 and 9833, amplitude 0.25, drive -24/0/12/36 dB, T=1/0.5/0.05, trim 0 dB. Use a rectangular window for coherent fixtures. Compare significant bins above -80 dBr to the independent sampled transfer reference within 0.1 dB; define dBr relative to the reference's largest non-DC bin to handle fundamental nulls. Require DC and even-index bins <=-100 dBr for settled symmetric single-sine cases. Also record absolute dBFS levels, fundamental level, odd-harmonic levels, and a clearly defined THD ratio when the fundamental is resolvable.
- **Additional signals:** coherent two-tones at bins 997/1499 and 9833/12289, amplitude 0.125 per tone, plus a two-second log sweep 20 Hz to 0.45*rate at mild/aggressive controls. Record sample-reference error, intermodulation, and alias patterns with window/scaling metadata. Native sampled-reference agreement verifies implementation fidelity; it is not a measurement of alias rejection. Identify harmonic foldback frequencies and collision limitations explicitly, and supply exact inputs/settings/rates so Q1 can perform filtered higher-rate comparisons. No native-rate alias suppression pass is claimed.
- **CPU proposal:** stereo at 48/96 kHz, capacities 32/64/127/256/1024, p99 <=5% of the full incoming block deadline for steady, full-range automation, bypass, and oversized cases. Include maximum drive/minimum threshold, ordinary and tiny-input diagnostics, and the declared finite-input envelope where it changes modulo cost. Require three AC-powered P-core-pinned qualification runs; reuse the reviewed equal-wall-time sampling procedure, adaptive 10-120 second windows, 100000-200000 observations, and at least 100 samples for each of 101 automation settings. Include an unpinned diagnostic and record hardware/compiler/power/affinity/thermal limitations. Collector or budget changes require a documented revision before retry.
- **Listening:** generated sine/transients plus licensed drums, bass, guitar, vocals, and synth material, with dry/wet monitoring RMS matched within 0.5 dB. Start comparison candidates at (drive +6 dB, T=0.5) and (+18 dB, T=0.25), trim 0 dB, then record exact useful settings and audible limitations from actual listening. These are proposed candidates, not listening results. Human evidence is required for sound acceptance.


## Reviewed refinements

Version-6 wave-folder records require drive, threshold, output trim, and bypass. Only version 6 serializes threshold; versions 1-5 preserve existing fields and meanings. Parsing rejects foreign controls and malformed fields before changing the destination.

For coherent sines, record nonFundamentalRmsRatio = sqrt(max(0,totalACPower-fundamentalPower)/totalACPower), with an explicit zero-power case. This includes aliased and residual energy. Conventional in-band harmonic/fundamental ratio is valid only when the fundamental is at least -40 dBr; otherwise its JSON value is null and its validity flag is false. Do not emit infinity or silently change denominators.

Bulk restore and structural target selection require callback detachment. Live scalar controls use validated lock-free atomics. Verify the existing device/controller detach-and-restore lifecycle; the module does not detect arbitrary concurrent method calls.

## Source and design provenance

The triangle transfer, ranges, tolerances and CPU share above are project decisions. The circuit-model paper supplies research context: [Virtual Analog Buchla 259 Wavefolder](../papers/virtual_analog_buchla_259_wavefolder.md). It does not qualify this implementation or predict its CPU cost.