---
type: Method
title: "Plugin interface, presets and host state"
description: "Current prototype controls, logarithmic frequency movement and version-5-only persistence."
timestamp: 2026-11-08T00:00:00Z
tags: ["audio-plugin", "automation", "state", "presets", "metering"]
resource: "https://github.com/LordZordack/disDorktion/issues/14"
---

# Plugin interface, presets and host state

The editor has Input, Waveshaper, Output and GLOBAL panels. Input Gain, Driver,
global mix and Final Gain live in GLOBAL. There are no visible mute/bypass or
legacy compatibility controls; DAW host bypass remains. Waveshaper controls run
Tanh Drive -> Fold Drive -> Threshold -> Waveshaper Trim -> Waveshaper Mix.

The [full chain](./full_chain.md) applies Input Gain before both global branches
and Final Gain after global mixing. GLOBAL Driver remains 1x to 4x, default 1x,
but acts after the crusher immediately before tanh/fold inside the nonlinear
wet branch. Fixed calibrated compensation follows nonlinear output before
shared trim and section mixing, following the delayed 10 ms Driver trajectory.
It uses no live level tracking or AGC. Global and Waveshaper dry are unchanged;
Driver is inaudible with both stages Off, section mix zero or global mix zero.
Input Mix covers the crusher/sampler. Waveshaper Mix
covers both nonlinear stages and their shared trim with an aligned dry blend.
Both stages Off retain trim on delayed dry. Production latency remains 92
samples at all mix and Off endpoints.

Capture rate and filter cutoff use logarithmic knob movement and editable
numeric values. Tanh/Fold minima, HP minimum and LP maximum display Off. Comb
damping is always enabled. Section zero-mix states suspend/reset unnecessary
wet work; reactivation warms histories before the wet contribution fades in.

The prototype intentionally replaces prior automation/state compatibility:
27 parameters, version-5-only state, no gain-only mode or Legacy Trim.
Complete ranges, defaults, state rejection rules and host bypass behavior are
in the repository guide `docs/PLUGIN_INTERFACE.md`. Factory presets reuse
production settings and shared trim; identity is derived from control values.
The Driver adjustment preserves parameter IDs and persistence schemas. Existing
saved sounds with Driver above 1x intentionally change; default 1x behavior
remains unchanged.

Raw input/final output meters retain peaks and overloads between UI polls.
Controls publish independent scalar atomics, not a whole-state bank. Serialized
host lifecycle ownership governs state restore/reset. The editor starts at
1120x740, with limits 1000x680 to 1400x1000.

See [Driver adjustment status](../notes/driver_adjustment_validation.md),
[historical issue #38 evidence](../notes/issue38_validation.md) and the explicitly
historical [interface validation record](../notes/plugin_interface_validation.md).
No revised CPU, listening or host qualification is inferred from older results.
