---
type: Method
title: "Reusable input and output gain"
description: "Reviewed production gain design, parameter semantics, transition rules, and acceptance limits for DisDorktion M1."
timestamp: 2026-10-10T00:00:00Z
tags: ["gain", "dsp", "smoothing", "real-time-audio"]
resource: "https://github.com/LordZordack/disDorktion/issues/4"
---

# Reusable input and output gain

Design approved on 2026-10-10 before production DSP implementation. This records intended behavior; measured acceptance is recorded in the [gain validation note](../notes/gain_validation.md). Related: [project](../projects/disdorktion.md), [processing contract](./processing_contract.md), and [audition method](./audition_measurement.md).

## Controls and algorithm

One implementation owns independent state for each input/output instance. `GainSettings` contains gain in dB (inclusive -60 to +24, default 0), mute (default false), and bypass (default false). Reject invalid/nonfinite dB values without changing any retained control. Convert `a = 10^(dB/20)` once for a changed target, in double precision. -60 dB is attenuation to approximately 0.001; mute explicitly requests exact zero.

Use double-precision JUCE linear smoothers for gain and bypass, each over `floor(sampleRate * 0.010)` frames. Advance before applying each frame, once for all channels. Repeated identical targets do not restart ramps. New targets ramp from the current value; while muted, retain new dB values for the next unmute. Bypass returns dry unity even when mute is selected. With gain envelope `g` and bypass envelope `b`, apply `y = x * (b + (1-b)*g)`, casting effective gain to float once per frame. Gain state keeps advancing during bypass.

Preparation/reset snap both envelopes to retained targets. Reject unprepared or wrong-layout audio without touching samples/history. Empty valid blocks do not advance envelopes. Dispatch oversized blocks without allocation. Report zero samples of latency. Float audio envelope: magnitude <= float maximum / 16 for one instance, /256 for a pair, /1024 for the plugin including its legacy trim. No clipping or normalization is part of gain.

## Compatibility and alternatives

The foundation [reference contract](https://github.com/LordZordack/DisDorktion/blob/main/docs/DSP_CONTRACT.md) retains linear gain [0,4]. Keep its harness target and version-1 experiments unchanged. Preserve its plugin stage and first two host parameter IDs as a legacy trim preceding the production pair; defaults give unity legacy trim. New controls own separate production stages. This preserves quiet legacy gains without clamping or muting them in a lossy migration; host normalized-float transport keeps its existing precision limits. New plugin state stores all eight controls; version-1 state imports the legacy values and initializes the pair to unity/unmuted/unbypassed.

Multiplicative smoothing offers uniform dB increments but cannot reach zero by itself and needs a separate mute envelope. Linear amplitude smoothing supports exact zero and reuses the foundation processing pattern. Double envelopes avoid float recurrence drift at high rates. Keeping [0,4] linear production controls was rejected in favor of the user's selected dB range.

## Acceptance limits defined before implementation

- Conversion relative error <= 2e-6. Steady/ramp output absolute error <= `2e-5 * max(1, abs(expected))` for nominal audio, using an independent double formula.
- Exact settled unity/bypass, exact settled mute, deterministic reset, and sample-identical split/whole processing for equal control event positions.
- Full mono/stereo matrix: 44.1/48/88.2/96/192 kHz and 1/32/64/127/256/1024 frames; empty, varying, oversized blocks and 8/384 kHz boundary ramps.
- Gain/mute/bypass interruptions, simultaneous ramps, gain while muted/bypassed, channel independence and independently updated/reset instances; zero realtime allocations/deallocations and source inspection for blocking/I/O/UI.
- Release stereo p99 <= 1% of block deadline per production instance, <= 2% for the pair, <= 3% for plugin gains including legacy trim. Follow the [reference procedure](https://github.com/LordZordack/DisDorktion/blob/main/docs/PERFORMANCE.md): all scenarios, 48/96 kHz, 32/64/127/256/1024 frames, three AC-powered pinned runs. An unqualified diagnostic is not a performance pass. This DSP-path benchmark excludes host control/adapter overhead.
- Actual listening observations of recorded gain/mute/bypass changes without unintended clicks, with settings and source provenance. Synthetic measurements cannot replace this gate.

## Verified sources

Read the repository's pinned JUCE 8.0.14 source at commit `2cdfca8feb300fb424002ba2c2751569e5bacb64` before implementation:

- [SmoothedValue source](https://github.com/juce-framework/JUCE/blob/2cdfca8feb300fb424002ba2c2751569e5bacb64/modules/juce_audio_basics/utilities/juce_SmoothedValue.h): reset floors the frame count, getNextValue advances before returning and snaps on the last frame, approximately equal targets are ignored; multiplicative mode cannot reach zero.
- [Decibels source](https://github.com/juce-framework/JUCE/blob/2cdfca8feb300fb424002ba2c2751569e5bacb64/modules/juce_audio_basics/utilities/juce_Decibels.h): power-of-ten conversion; minus-infinity threshold returns zero. Explicit module mute avoids treating -60 dB as zero.
- [APVTS source](https://github.com/juce-framework/JUCE/blob/2cdfca8feb300fb424002ba2c2751569e5bacb64/modules/juce_audio_processors/utilities/juce_AudioProcessorValueTreeState.cpp): parameter adaptation uses host notification, while state copy/replacement uses locking. Keep state operations outside realtime processing and verify raw parameter synchronization.
