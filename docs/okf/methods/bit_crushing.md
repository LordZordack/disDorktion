---
type: Method
title: Bit crushing
description: Signed PCM amplitude quantization and sample holding for bit-crushing effects.
timestamp: 2026-10-10T00:00:00Z
tags: ["audio-dsp", "quantization", "bit-crushing"]
resource: https://github.com/LordZordack/DisDorktion/issues/5
---

# Bit crushing

## Status

The legacy algorithm and expressive-control extension are implemented.
Expressive Debug/Release/headless suites and renders passed; CPU qualification,
listening, GUI inspection, VS2022 CI, and complete graph analysis remain open.
The validation note separates those results from the older legacy evidence.

## Purpose

Bit crushing reduces the amplitude resolution of a signal by quantizing samples to a finite set of signed PCM codes. The control changes quantization depth, while a separate sample-rate control can reduce how often the held sample is updated.

## Approved design

- Quantizer depths: 1–8 bits, plus 16, 32, and 64 bits. Default: 64 bits.
- Signed PCM rounding: nearest integer, with ties away from zero.
- Saturation range at depth B: [-1, 1 − 2^(1−B)]. At 1 bit, the available endpoints are −1 and 0.
- The interface uses float32 samples; integer quantization codes use int64.
- Capture rate: 100–384000 Hz, capped at the host sample rate. Default: 384000 Hz.
- Capture-rate and bypass transitions use 10 ms smoothers. Bypass defaults off.
- A depth change requantizes the held raw sample on the next frame and does not reset capture phase.
- Channels have independent held samples and share one capture clock.
- The first frame captures immediately; capture phase persists across processing blocks.
- Processing is zero latency.
- Expressive settings: mix [0,1] default 1; crush [0,1] default 0; shape [-1,1] default 0; jitter [0,1] default 0; uint32 jitter seed default 1.

## Expressive quantization and capture

At neutral crush=0 and shape=0, retain the existing PCM path exactly. Let
d0=2^(1-B) for selected depth B. Continuous crush uses
d=d0+(1-d0)*crush^3. For the enhanced path, clamp raw x to [-1,1], shape it
as sign(x)*abs(x)^p with p=2^(2*shape), round to the nearest d grid with ties
away from zero, clamp to [-1,1], then inverse-shape with power 1/p. Blend
legacy and enhanced results using w=max(crush,abs(shape)). The enhanced
quantizer is symmetric; fractional grids can put saturation endpoints off-grid
and do not promise exactly 2^B distinct codes. The blend preserves the exact
legacy path at neutral controls and transitions continuously as crush/shape
leave zero. Silence remains zero.

Mix linearly blends dry input and the wet result with exact mix endpoints.
Bypass remains independent and dominant, with exact dry output. Crush, shape, mix, and jitter
use double-precision 10 ms ramps advanced once per frame; depth remains
discrete. Smoothing changes requantize the held raw sample without capturing a
new sample between capture events.

Jitter uses a local xorshift32 PRNG (seed 0 maps to a fixed nonzero state),
shared random timing across stereo and independent held channel histories. A
uniform u=(uint32+0.5)/2^31-1 in [-1,1) is held between capture events. The threshold is
hostRate + u*0.9*jitter*(hostRate-effectiveRate), with effectiveRate capped
at host rate. It is never below effectiveRate, fades to zero at full rate, and
allows at most one capture per host frame. The nominal mean interval under
stable settings is hostRate/effectiveRate; jitter has less effect near full
rate. Jitter zero uses the original compensated clock exactly. A seed change
restarts the PRNG on the next frame without resetting phase.

## References

- The approved design cites the fundamentals in [The Scientist and Engineer's Guide to Digital Signal Processing, Chapter 3](https://www.dspguide.com/CH3.PDF) and [Julius Smith's Sampling Theory](https://www.dsprelated.com/freebooks/mdft/Sampling_Theory.html).
- [Native Instruments BITE documentation](https://docs.native-instruments.com/online-guides/crush-pack-manual/en/bite.html) is a reference for the broader creative use of bit depth and sample-rate degradation. It does not specify or validate the custom crush, shape, or jitter formulas above.
- JUCE smoother behavior was checked against the pinned [JUCE 8.0.14 `SmoothedValue` source](https://github.com/juce-framework/JUCE/blob/2cdfca8feb300fb424002ba2c2751569e5bacb64/modules/juce_audio_basics/utilities/juce_SmoothedValue.h): reset floors the sample count from sample rate × 10 ms, each `getNextValue()` advances once and snaps on the final ramp frame, and a target equal to the current target is ignored.

## Validation status

Legacy depth/capture-rate measurements and performance results are recorded in [bit crusher validation](../notes/bit_crusher_validation.md); they do not validate the expressive extension. Extension verification and listening remain pending. See also the [processing contract](./processing_contract.md) and [DisDorktion project](../projects/disdorktion.md).
