---
type: Method
title: "Full chain integration candidate"
description: "Shared section routing, latency aligned dry reference, global mixing and final gain."
timestamp: 2026-10-17T12:00:00Z
tags: ["audio-dsp", "integration", "latency", "mixing"]
resource: "https://github.com/LordZordack/disDorktion/issues/13"
---

# Full chain integration candidate

`FullChain` composes the [input](./input_section.md),
[middle](./middle_section.md) and [output](./output_section.md) implementations,
then mixes delayed dry with wet and applies the reusable [gain](./gain.md).
The middle quality is fixed at `4x-fir-adaa-v1`; its integer algorithmic latency
is 92 host samples. Preparation derives the delay from the actual section,
with no rate or quality switch during processing. Every effect bypass preserves
this latency, including the folder's compensated bypass path.

## Mixing and bypass

Dry captures input before production input gain or crushing. A per-channel ring
delays it by the reported middle latency. Linear mixing uses
`dry * (1-weight) + wet * weight`. Global mix is [0,1], default 1; common chain
bypass is false by default. Their independent 10 ms linear envelopes produce
`weight = mixEnvelope * (1-bypassEnvelope)`. Simultaneous ramps therefore have
a product trajectory. Dry and wet continue processing at both endpoints.
Reset clears delay/filter history and snaps envelopes to retained settings.

Common chain bypass selects delayed dry before final gain. Independent effect
bypasses retain the middle FIR/ADAA boundary, so bypassing every wet effect is
not identical to common chain bypass. The final gain, mute and gain bypass
retain the production Gain semantics and affect both mixed branches.

Integer alignment removes algorithmic delay, without matching the output IIR
filters' frequency-dependent phase. The dry reference has no added all-pass
filter. Intermediate mix measurements must compare complex sums, including
phase cancellation near filter transitions, rather than adding magnitudes.
No limiter, normalization or extra saturation is introduced.

## Processing ownership and diagnostics

Preparation validates the host domain (8–384 kHz, 1–65536 capacity, mono/stereo)
and constructs replacement state before replacing a valid preparation. Internal
chunks are capped at 1024 frames to bound scratch storage. Settings validate
as a whole before mutation. Processing, controls, reset and peak access require
exclusive ownership. The adapters publish independent scalar atomics and latch
them once per outer callback; scalar publication is not a whole-state snapshot.

Post-input-gain, post-high-pass, pre-mix wet and final-output peak taps describe
the hot wet path and final mixed signal. They still report internal overload
when common bypass or zero global mix selects dry. Values above unity are
diagnostic headroom evidence, not silently limited signals.

## Plugin and harness compatibility

The plugin preserves its first eight parameter IDs and host ordering. New
instances run the full chain; version-3 state persists its controls and mode.
Version-1/2 restores select the original gain-only path with zero latency;
version-3 saves retain that compatibility mode. Restoring mode after prepare
updates JUCE's latency notification. State parsing finishes before publication
under the host callback lock; persistence is outside audio processing.
Switching compatibility modes clears dormant histories so prior audio cannot
reappear. The host tail declaration is conservatively three seconds plus the
algorithmic delay in full-chain mode and zero in legacy gain mode.

The legacy reference trim precedes full-chain capture, so the dry reference is
independent of production input drive but intentionally includes that legacy
trim. The host's bypass callback supplies untrimmed delayed input, bypassing
final gain as well, and keeps processing histories warm. This differs from the
chain bypass control, which preserves final gain.

The audition harness selects `full-chain`, with version-11 experiment records
and all section/global controls. Nested versions 8, 9 and 10 preserve existing
validated section schemas. Older experiment targets remain unchanged. Offline
renders keep the requested frame count, leading latency and no automatic tail
flush. Monitoring gain is outside recorded DSP measurements.

See [qualification evidence](../notes/full_chain_validation.md). Existing
section listening/dependency gates remain open; integration does not imply
their acceptance or first-release qualification.
