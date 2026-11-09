---
type: Method
title: "Full chain integration candidate"
description: "Production gain staging, compensated Driver, section mixes and fixed-latency routing."
timestamp: 2026-11-08T00:00:00Z
tags: ["audio-dsp", "integration", "latency", "mixing"]
resource: "https://github.com/LordZordack/disDorktion/issues/13"
---

# Full chain prototype routing

The production path is Input Gain -> global dry/wet split ->
[crusher/sampler](./bit_crushing.md) -> [Waveshaper](./middle_section.md) ->
[Output](./output_section.md) -> global mixing -> Final Gain.
The prepared production latency remains **92 host
samples**, using fixed 4x FIR/ADAA nonlinear processing. Both global branches
include Input Gain. The dry branch is delayed by the section latency.

Driver ranges 1x to 4x, default 1x, with 10 ms smoothing shared across channels.
It acts after the crusher, immediately before tanh/fold inside the nonlinear
wet branch. Fixed calibrated compensation follows the nonlinear output before
shared trim and section mixing, using the delayed Driver trajectory. Global dry
and Waveshaper section dry receive neither Driver nor its compensation. Driver
has no audible effect with both nonlinear stages Off, Waveshaper Mix zero or
global mix zero. Manual trim remains active with both stages Off.

The lookup uses Driver knots 1, 1.5, 2, 3 and 4. Each correction is the median
RMS correction across four active factory configurations, sine/two-tone/seeded
noise, -24/-18/-12 dB levels and 48/96 kHz; correction is clamped to +/-6 dB.
Interpolation is linear in dB against log Driver. This replaces unconditional
inverse gain; there is no live level tracking or AGC and no constant perceived
loudness claim. Calibration and verification status are recorded in the
[Driver adjustment note](../notes/driver_adjustment_validation.md).
`postInputGain` measures after Input Gain, before the global split.

`FullChain` owns a separate pre-input `Gain` and direct `BitCrusher`; it does
not use the convenience `InputSection` for production routing. The standalone
[input section](./input_section.md) still composes Gain -> BitCrusher for
isolated harness use. Input Mix covers only the crusher/sampler.

Global and section mixes use aligned dry references. At settled zero section
mix, unnecessary wet work is reset/suspended; histories warm before wet fades
back in. Both nonlinear stages Off select trimmed delayed section dry; the
untrimmed delayed reference remains available to Waveshaper Mix. Effective
latency stays fixed during Off and mix transitions. See the
[historical issue #38 validation record](../notes/issue38_validation.md).

The plugin exposes no mute/bypass controls or legacy compatibility mode.
`FullChainSettings::bypass` remains only for the development harness; it selects
the delayed Input Gain branch before Final Gain and is not a plugin parameter.
DAW bypass instead selects delayed raw input. Current parameters and v5-only
state are documented in the [interface method](./plugin_interface.md).

Preparation allocates scratch audio and delay/gain history. Runtime processing,
controls and reset require exclusive ownership and avoid allocation. Scalar
host controls are latched per callback and do not form an atomic settings bank.
No limiter is inserted; output phase shaping can cause frequency-dependent
cancellation in partial global blends. Full-chain tail reporting is 20 seconds
plus latency. Historical [full-chain qualification](../notes/full_chain_validation.md)
predates the revised routing and does not qualify it.

Full-chain experiment records now use version 17 for Driver, section mix and
shared trim. Older version-11/16 records load defaults for these new controls;
this harness migration is separate from the plugin's v5-only state policy.
The Driver adjustment changes neither parameter IDs nor these persistence
schemas. Saved sounds above Driver 1x intentionally change; Driver 1x defaults
retain their prior behavior.
