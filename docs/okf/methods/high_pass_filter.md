---
type: Method
title: "Selectable Second-Order High-Pass Filter"
description: "Design for a zero-latency high-pass module with selectable Butterworth, Chebyshev I, and Chebyshev II responses and smoothed transitions."
timestamp: 2026-10-12T00:00:00Z
tags: ["high-pass", "filter", "tpt", "chebyshev"]
resource: "https://docs.juce.com/master/classjuce_1_1dsp_1_1StateVariableTPTFilter.html"
---

# High-Pass Filter

## Design

The module is independently selectable and uses a second-order topology in one of three responses:

- **Butterworth (default):** maximally flat passband, with a nominal 12 dB/octave asymptote.
- **Chebyshev I:** fixed 1 dB passband ripple, with a nominal 12 dB/octave asymptote.
- **Chebyshev II:** fixed 40 dB minimum stopband attenuation, with finite transmission zeros and a -40 dB far-stopband floor. It must not be described as an indefinitely falling 12 dB/octave response.

Cutoff has one meaning in every response: the frequency where the high-pass magnitude is -3.0103 dB (half power). The requested range is 20–20000 Hz, clamped to `min(requested, 0.45 × sampleRate)` for processing; defaults are 20 Hz and Butterworth. Bypass defaults off. The module reports zero algorithmic latency; its phase delay is distinct and nonzero.

Use three continuously running, fixed-storage TPT banks, one per response, with independent natural-frequency scaling, damping, output mix, and double-precision history per channel. Smooth all banks' prewarped natural frequencies over finite 10 ms geometric ramps, snapping exactly to target. Crossfade response selection and bypass with linear 10 ms weights, retargeting from the current blend; shared mode progress preserves a unit-sum blend. Keep inactive and bypassed histories warm; full bypass returns exact dry signal. Process finite input in [-4,4]. Invalid settings reject transactionally; failed prepare preserves the prior valid configuration. Reset clears histories and snaps controls to retained targets.

## Specified acceptance limits

| Check | Specified limit |
| --- | --- |
| Complex impulse response | Absolute error ≤ 3e-5 versus independently generated direct-form bilinear-transform (BLT) biquad expectations. |
| Half-power cutoff | -3.0103 dB within 0.02 dB. |
| Stepped-sine response | Within 0.05 dB where expected magnitude is above -60 dB. |
| Chebyshev I | Passband endpoint -1 dB; peak 0 dB. |
| Chebyshev II | Transmission-zero depth ≥ 80 dB; far-stopband floor -40 dB. |
| Rapid automation | Finite output and peak < 8 for nominal unit-peak input. |
| Silence tail | Below 1e-6 after 2 s at 20 Hz. |
| Realtime | No allocation or deallocation in processing, parameter updates, reset, or rejected calls. |

Use the contract matrix of 8–384 kHz, with checks at 44.1/48/96/192 kHz and block sizes 32/64/128/256/512/1024, mono and stereo, plus rate endpoints, one-frame, and oversized blocks. Compare each response to independent expectations: Butterworth response and direct-form BLT biquad impulse; Chebyshev polynomial magnitude and independently generated pole/zero biquad impulses for Types I and II, including extrema, cutoff, and Type II zeros and floor. See [issue #8](https://github.com/LordZordack/DisDorktion/issues/8) for the full acceptance specification.

These limits are specified acceptance criteria, not measured results. Record measured outcomes in the linked [high-pass validation note](../notes/high_pass_validation.md). No human listening result is claimed.

## References and related design

- [JUCE StateVariableTPTFilter](https://docs.juce.com/master/classjuce_1_1dsp_1_1StateVariableTPTFilter.html) documents the topology used as a TPT reference.
- CutoffFilter implementation (`src/dsp/CutoffFilter.h`) is the shared DSP source.
- [W3C Audio EQ Cookbook](https://www.w3.org/TR/audio-eq-cookbook/) provides an independent biquad coefficient reference for response checks.
- [SciPy `cheby1`](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.cheby1.html) and [filter design source](https://github.com/scipy/scipy/blob/v1.18.0/scipy/signal/_filter_design.py) inform Chebyshev I expectations.
- [SciPy `cheby2`](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.cheby2.html) and [filter design source](https://github.com/scipy/scipy/blob/v1.18.0/scipy/signal/_filter_design.py) inform Chebyshev II expectations.
- [DisDorktion project](../projects/disdorktion.md)

Primary-source links checked 2026-10-12.
