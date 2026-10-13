---
type: Method
title: "Reusable input section"
description: "Native-rate composition of production gain and expressive bit crushing, with section acceptance limits and independent bypass semantics."
timestamp: 2026-10-13T00:00:00Z
tags: ["gain", "bit-crushing", "integration", "real-time-audio"]
resource: "https://github.com/LordZordack/disDorktion/issues/10"
---

# Reusable input section

Issue #10 composes [production gain](./gain.md) then [bit crushing](./bit_crushing.md)
for the [DisDorktion project](../projects/disdorktion.md). The user authorized
implementation after a supplied-context Antigravity plan review. Actual results
and remaining acceptance gates are recorded in the [validation note](../notes/input_section_validation.md).

## Algorithm and controls

`InputSection` owns one `Gain` and one `BitCrusher`, accepting
`InputSettings { GainSettings gain; BitCrusherSettings crusher; }`. It runs at
the host rate. Each capacity-bounded chunk is processed through gain, measured
for its peak, then passed to the crusher. It adds no scratch audio, limiter,
normalization, oversampling, parameter smoothing, or algorithm changes.

Gain retains its -60 to +24 dB range, mute, 10 ms envelopes and independent
bypass. Crusher retains all eleven depths, 100-384000 Hz requested capture rate
(capped at host rate), mix, crush, shape, jitter, seed and independent bypass.
The crusher's dry input is the **post-gain** signal. Its wet quantizer can clamp
gain overload, while mix=0 or crusher bypass preserves post-gain values above
unity. Gain bypass takes precedence over mute. Muting gain leaves an already
captured crusher sample held until the next capture; after the gain ramp ends,
the next capture becomes zero. These follow the constituent module contracts.

The supported finite input envelope is float maximum /16, with nominal audio
in [-1,1]. +24 dB multiplies by approximately 15.85, so nominal input can
overload the quantizer. Interstage metering makes this visible even when the
final wet output remains within nominal unity.

## Lifecycle and thread ownership

The section validates the complete host `ProcessSpec` before preparing children,
and both control sets before mutating either child. Rejection leaves samples,
histories, envelopes and valid previous preparation unchanged. Empty valid
blocks succeed without advancing state or changing the last interstage peak.
Oversized inputs are processed in successive non-owning chunks. Reset snaps
retained controls, clears the crusher clock/holds/PRNG history, and clears peak.
Latency is zero in all valid configurations and bypass states.

All section calls, including `getPostGainPeak()`, require exclusive processing
ownership. The getter returns the maximum absolute post-gain sample across the
last successful nonempty process call. The harness copies it to a lock-free
atomic and latches overload until consumption; the GUI never reads module state
directly. Its numeric peak is a current-block reading, while the overload
indicator remains visible for two seconds after GUI consumption.

The `input-section` harness target keeps common bypass separately from the
individual selections. Common bypass ORs into both effective child bypasses;
releasing it restores the retained selections. Both children keep processing
while bypassed so smoothers and the capture clock advance. There is no additional
section dry/wet stage. Version-8 experiment records explicitly store gain/mute,
both child bypasses, all crusher settings/seed, and common bypass. Versions 1-7
retain their target-specific schemas. Bulk restoration and target replacement
require the audio callback to be detached; live scalar transport does not
promise an atomic multi-field snapshot.

## Alternatives and acceptance limits

A harness-only chain would omit the reusable boundary needed by full-chain
integration. Fusing gain and crusher could reduce traversal cost but would
change module implementation and require renewed qualification. The small
composition wrapper preserves their accepted behavior and is the chosen approach.

Compare against separately prepared Gain -> BitCrusher instances with identical
events, reset and arithmetic build settings: maximum sample error **0**. Require
exact partition invariance, independent mathematical gain/PCM checks using the
module tolerances, mono/stereo isolation, transactional rejection probes, and
zero allocations/deallocations in processing/control/reset paths.

The proposed section DSP p99 ceiling is **6% of the incoming block deadline**,
combining the existing 1% gain and 5% crusher allocations, including section
control application, dispatch and peak scan. Qualify Release x64 stereo at
48/96 kHz and capacities 32/64/127/256/1024, three AC-powered runs pinned to an
observed performance-core processor. Keep all raw timings and automation/depth/
bypass coverage. Quick, unpinned and insufficient-sample runs are diagnostics.
See the repository's `docs/PERFORMANCE.md` for the timing procedure.

Measure gain overload, quantization, reduced capture rate, expressive settings,
rapid controls, bypass combinations and held-sample mute tails. Generate
deterministic source/intermediate/wet/reference WAVs and record settings,
provenance, spectrum observations and errors. Human listening remains an
explicit acceptance gate; generating files is not evidence of audible quality.
Closed upstream tickets are not automatically reopened. Any constituent
algorithm or semantic change must renew the affected module acceptance.

Full-chain plugin wiring, global dry/wet and output gain belong to later issues.
