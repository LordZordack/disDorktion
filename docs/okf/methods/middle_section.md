---
type: Method
title: "Middle section qualification candidate"
description: "Fixed 4x FIR tanh and ADAA folding integration, independent bypass controls, experiment identity and render policy."
timestamp: 2026-10-13T00:00:00Z
tags: ["audio-dsp", "oversampling", "integration", "latency"]
resource: "https://github.com/LordZordack/disDorktion/issues/11"
---

# Middle section qualification candidate

S2 integrates the [Q1 boundary](./middle_oversampling.md) into the shared
[audition harness](./audition_measurement.md). Its fixed quality identity is
`4x-fir-adaa-v1`: maximum-quality JUCE FIR upsampling, [tanh](./tanh_saturation.md),
first-order ADAA [triangle folding](./wave_folding.md), FIR downsampling and the
65-tap fractional-delay correction. Q1's numerical evidence supports evaluating
this candidate; [listening and extreme residual-alias acceptance](../notes/middle_oversampling_validation.md)
remain open. See [S2 qualification evidence](../notes/middle_section_validation.md).

The [input section](./input_section.md), including intentional crushing, runs
outside this boundary. Output filters also run outside it. Full-chain routing,
global dry alignment and host latency reporting belong to C1 (#13).

## Controls and bypass

Tanh and folder have independent -24 to +36 dB drives and -60 to +12 dB output
trims. The fold threshold is 0.05 to 1. Defaults are zero drives/trims and
threshold 1. The stage order makes tanh's trim an input-level control for
folding; it is not a final output gain. Controls and bypass use the existing
10 ms internal-rate ramps. No transfer functions or defaults were retuned.

Common bypass combines with both retained stage bypasses. Returning from
common bypass restores each selection. Upsampling, downsampling, ADAA's dry
averaging/compensation and the correction FIR continue running. Both bypassed
therefore remains a filtered/delayed path. The boundary reports **92 host
samples** at every supported rate and bypass combination. Filter pre-ringing
starts before the reported delay; peak and passband phase establish delay,
not the first nonzero output sample.

Preparation allocates while detached and validates the existing host process
domain. Target setters and resets run on the processing thread or while
detached. GUI scalar setters publish independent atomics; processing reads
them once per callback into settings. These loads are not a coherent transaction
across multiple UI gestures. Bulk record restoration requires detached
processing. Isolated module controls have separate state.

## Persistence and rendering

Version-9 `middle-section` records require `middleQuality`, `tanhDriveDb`,
`tanhOutputDb`, `tanhBypass`, `folderDriveDb`, `folderOutputDb`,
`folderThreshold`, `folderBypass` and common `bypass`. Unsupported quality
identities and foreign module settings are rejected. Versions 1-8 retain their
original targets and schemas.

The shared renderer saves exactly `renderDurationSamples` source and wet
frames. It captures untrimmed processing delay and does not append a tail;
version-9 results record `renderPolicy: fixed-duration-uncompensated`,
`tailFlushed: false` and `latencySamples: 92`. This makes source/wet pairs
represent the live stream at matching offsets. Include trailing silence by
setting a generated source duration shorter than the render duration to capture
ring-down. A file source loops, so a longer file render continues the loop.
Q1's separate listening renderer aligns and level-matches comparison files;
its files have different duration semantics from shared experiment renders.

## Qualification limits

Identical chunked routing must match its constituent modules and rate/correction
wrappers bit for bit. Different host partitions must differ by at most 1e-6 in
absolute sample amplitude. Small-signal impulse/phase checks cover all four
stage bypass states at 44.1, 48, 88.2, 96 and 192 kHz and capacities 1, 32, 64,
127, 256 and 1024. Check independent channels, zero blocks, interrupted ramps,
reset, invalid settings and callback heap activity.

Retain Q1's magnitude/phase/blend and representative alias limits; do not
redefine the -60 dBc residual goal after observing stress settings. CPU gates
use Release p95 batch means below 10% of block duration and remain provisional.
Debug verifies behavior and realtime safety. Human sound judgment and host
deadline timing require their own evidence before production acceptance.
