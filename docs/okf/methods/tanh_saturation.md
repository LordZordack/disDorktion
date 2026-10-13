---
type: Method
title: "Tanh saturation"
description: "Approved memoryless tanh saturation design, controls, transition behavior, and pre-implementation acceptance limits for DisDorktion."
timestamp: 2026-10-10T00:00:00Z
tags: ["audio-dsp", "waveshaping", "saturation", "aliasing"]
resource: "https://github.com/LordZordack/disDorktion/issues/6"
---

# Tanh saturation

Q1's [shared oversampling boundary](./middle_oversampling.md) adds explicit
internal-rate preparation while preserving the native transfer and host limits.
See [comparison evidence](../notes/middle_oversampling_validation.md) for alias
and latency results; native acceptance alone does not qualify a driven cascade.

## Status and scope

Design approved on 2026-10-10 before DSP implementation. This page records the approved behavior and pre-implementation acceptance limits; measured outcomes are in the [validation note](../notes/tanh_saturation_validation.md). The implementation, automated/native-rate validation, and pinned revision-2 CPU baseline are complete under recorded conditions. Human listening, GUI/VS2022 checks, and Q1 antialiasing/production integration remain pending. The method informs the [DisDorktion project](../projects/disdorktion.md) and follows the [processing contract](./processing_contract.md).

This issue delivers a reusable memoryless saturation module and its native-rate baseline for Q1. Shared oversampling and production plugin-chain integration are outside this module's scope. Any Q1 quality-driven change requires renewed affected validation.

## Controls and transfer function

| Setting | Range | Default | Behavior |
| --- | --- | --- | --- |
| `driveDb` | -24 to +36 dB inclusive | 0 dB | Input drive before tanh |
| `outputDb` | -60 to +12 dB inclusive | 0 dB | Explicit wet output trim |
| `bypass` | false or true | false | Smoothed transition to original input |

For input `x`, drive factor `a = 10^(driveDb/20)`, and output factor `g = 10^(outputDb/20)`, the settled wet transfer is:

```text
w = g * tanh(a * x)
```

At an in-progress bypass blend `b`, output is `y = b*x + (1-b)*w`. Exact endpoint branches are required: `b == 0` returns wet and `b == 1` returns the original input sample. Bypass excludes both drive and output trim. At the default 0 dB drive, the curve is `tanh(x)`; this is intentionally a nonidentity setting.

The recommended normalization is the explicit output trim above with unnormalized tanh. Wet magnitude is bounded by `g`; at the maximum output trim the asymptotic wet bound is approximately 3.981072. Trim above 0 dB may exceed unity and the audition harness must retain its overload indication. Drive affects loudness, so matched-level listening uses the separate monitoring control.

Two alternatives were considered and rejected. `tanh(a*x)/tanh(a)` peak-normalizes at unit input but makes the ceiling drive-dependent and is not loudness matching. `tanh(a*x)/a` preserves small-signal gain but lowers the saturation ceiling as drive increases, reducing its ability to drive a later wave folder.

## Numeric and input behavior

Compute drive multiplication, tanh, output trim, and blend in double precision before conversion to float. All finite float32 input values are supported: maximum finite float multiplied by maximum drive remains finite in double. Nonfinite scalar settings reject the entire update and preserve prior settings and ramp state. Silence must remain exact zero. Settled bypass is sample-identical to input. The module reports zero samples of latency.

## Smoothing and reset

Use independent double-precision linear-amplitude smoothers for drive factor `a`, output factor `g`, and bypass blend `b`. Each ramp lasts `floor(sampleRate * 0.010)` frames. Advance before applying the first frame and reach the target on the final frame. Advance once per frame, shared across channels, including while bypassed. Repeated unchanged settings must not restart a ramp. A new target during a ramp begins from its current value.

Preparation and reset snap the smoothers to their retained targets; there is no startup fade. Empty blocks do not advance the smoothers. The pinned JUCE 8.0.14 source was checked before implementation: `reset` floors the ramp frame count, `getNextValue` advances before returning and snaps at the final frame, and unchanged targets preserve the ramp. The module caches converted targets when the dB setting is unchanged.

## Acceptance limits defined before implementation

These are proposed project acceptance criteria, not results reported by the cited paper.

- Curve coverage includes signed values across [-4, 4], tiny values, zero, +/-1, large finite floats, and drive/output endpoints. The independent reference is `sign(z) * (-expm1(-2*abs(z))) / (2 + expm1(-2*abs(z)))`, with sign snapping above `abs(z) = 40`. It must not call the module transfer helper or use `std::tanh` as its only oracle. Maximum output error: `2e-7 * max(1, g)`.
- Check odd symmetry, monotonic response at fixed settings, finite output, and settled wet bounds using the same numeric tolerance. Require exact silence and settled-bypass equality. At fixed positive input, increasing drive must increase untrimmed wet magnitude; negative input follows odd symmetry.
- Compare drive, trim, and bypass ramps against independent frame interpolation. Cover simultaneous and interrupted ramps, changes while bypassed, repeated controls, empty blocks, reset/reprepare, and partition invariance: equal control-event frame positions must yield sample-identical output regardless of block partitioning.
- Cover mono and stereo at 44.1, 48, 88.2, 96, and 192 kHz with capacities 1, 32, 64, 127, 256, and 1024. Also cover 8 and 384 kHz boundaries, 65536-frame capacity, varying and oversized blocks, rejected preparation/layout, sentinel preservation, deterministic reset, independent instances, and distinct/opposite-sign channel inputs.
- Prepared DSP, harness, and device callback checks must record zero tracked C++ allocation/deallocation for processing, parameter updates, and reset. Inspect those paths for locks, file/logging access, and UI calls. Allocation instrumentation alone does not prove all allocators or callback paths are covered.

## Native-rate spectral baseline proposal

Aliasing is a known concern for memoryless nonlinearities. The cited DAFx paper discusses aliasing and antialiasing approaches for nonlinear waveshaping; it does not establish this project's settings, thresholds, CPU budget, or implementation results. The baseline here characterizes native-rate behavior and is not an antialiasing pass.

At 48 and 96 kHz, use 65536-frame coherent sines at bin 1365, amplitude 0.25, drive -24/0/12/36 dB, and output trim 0 dB. Compare significant bins above -80 dBc to a separately evaluated mathematical reference with deviation no greater than 0.1 dB. For the settled symmetric single-sine fixture, require DC and even harmonics at or below -100 dBc. Record odd harmonics and THD with exact bin definitions.

Also characterize a high-frequency coherent sine at bin 9833; coherent two-tone fixtures at bins 997/1499 and 9833/12289 with amplitude 0.125 per tone; and a two-second logarithmic sweep from 20 Hz to 0.45 times the sample rate at mild and extreme drive. Compare samples against the independent transfer reference and record intermodulation and alias spectra. Define windows, normalization, bins, thresholds, and reference algorithm in fixture metadata. Folded components are measured baseline observations; they do not establish production antialiasing quality.

## Performance and listening acceptance

Reference CPU acceptance target: stereo at 48 and 96 kHz, blocks 32/64/127/256/1024, p99 no greater than 5% of the incoming block deadline for steady state, full-range automation, bypass, and oversized-block cases. Record zero reported latency. Require three AC-powered, P-core-pinned runs following the existing warmup, probe, and sampling procedure, preserve failures, and include an unpinned diagnostic. Any collector adjustment or budget change requires an explicit documented revision before retrying.

### Tanh collector revision 2 and qualification

The first baseline attempt exhausted its 200,000-timing storage cap in the 48 kHz/127-frame automation case after throughput recovered from probe-time slowdown. Its partial distributions and sub-budget p99 do not qualify. Revision 2 changed only the tanh collector: it uses an adaptive 10-120 second measurement window and 150,000 equal-wall-time sampling slots. Each slot records at most one callback; missed slots are skipped without catch-up. The overall minimum remains 100,000 timings and the p99 deadline budget remains 5%. Automation must cover all 101 control residues with at least 100 collected samples per residue; CSV reports minimum and maximum residue counts to reveal gaps or periodic sampling alias. Gain and bit-crusher collector behavior is unchanged. The [revision-2 pinned baseline](https://github.com/LordZordack/DisDorktion/blob/main/docs/measurements/tanh_saturation_cpu_baseline_revision2.csv) qualified all 120 rows across three runs. Per-case timing counts ranged from 148,469 to 149,711; each automation case covered all 101 residues, with 987 to 2,065 samples per residue. Worst p99 was 5,000 ns (0.377952756% of deadline) for 96 kHz/127-frame bypass, below the unchanged 5% budget. The full unpinned revision-2 [diagnostic CSV](https://github.com/LordZordack/DisDorktion/blob/main/docs/measurements/tanh_saturation_cpu_diagnostic_revision2.csv) contains all 40 rows and exited 1 as expected because all rows were marked unpinned/reference qualification unavailable; no other errors or budget exceedances occurred. Its timing counts ranged from 146,994 to 149,815, automation covered all 101 residues with 994 to 2,062 samples per residue, and worst p99 was 1,600 ns (0.48% of deadline) for 96 kHz/32-frame automation. This remains intentionally unqualified. The observed machine reported AC power and balanced power profile; thermal sensing and ambient conditions were unavailable or unconfirmed. See the [performance procedure](https://github.com/LordZordack/DisDorktion/blob/main/docs/PERFORMANCE.md#tanh-saturation-measurements).

Listening should cover mild and extreme settings, drive/bypass transitions, mono/stereo, generated transients, and licensed drums, bass, guitar, vocal, and synth material. Match dry/wet monitoring RMS within 0.5 dB and record monitor gain, source provenance/hash, settings, rate, and observations. User listening evidence is required for sound acceptance; automated renders cannot replace it.

## References

- Parker, Zavalishin, and Le Bivic, [“Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution”](https://www.dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf), DAFx-16. Relevant as research context on aliasing from nonlinear waveshaping and antialiasing techniques; the control ranges and project acceptance budgets above are independent design choices.
- The repository-pinned JUCE 8.0.14 [`SmoothedValue` source](https://github.com/juce-framework/JUCE/blob/2cdfca8feb300fb424002ba2c2751569e5bacb64/modules/juce_audio_basics/utilities/juce_SmoothedValue.h) is the implementation reference to verify for frame-count flooring, advance timing, endpoint behavior, and unchanged targets.

## Validation status

The DSP implementation, automated correctness checks, native-rate spectral fixtures, deterministic experiment renders, and revision-2 pinned CPU baseline are complete; results and conditions are recorded in the [tanh saturation validation note](../notes/tanh_saturation_validation.md). The separate unpinned diagnostic completed with the expected unqualified status. Human listening, GUI/VS2022 checks, and Q1 antialiasing/production integration remain pending. The CPU baseline passed under recorded conditions, with thermal sensing and ambient temperature unavailable or unconfirmed.
