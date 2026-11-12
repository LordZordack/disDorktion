---
type: Method
title: "Plugin interface, presets and host state"
description: "Current prototype controls, factory/user preset workflow and strict version-5 persistence with optional host identity metadata."
timestamp: 2026-11-12T00:00:00Z
tags: ["audio-plugin", "automation", "state", "presets", "metering"]
resource: "https://github.com/LordZordack/disDorktion/issues/43"
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
production settings and shared trim.
The Driver adjustment preserves parameter IDs and persistence schemas. Existing
saved sounds with Driver above 1x intentionally change; default 1x behavior
remains unchanged.

## Preset workflow and defaults

One compact preset button opens Save preset, Save preset as, Load preset,
Delete preset and Reset to defaults, with adjacent Factory presets and User
presets submenus. There is no separate Initialize button. The five factory
settings remain available immediately and after parameter edits. New plugin
instances, Reset to defaults and the Initialize factory preset use 0 dB Tanh
and Fold Drive. Their -24 dB Off endpoints remain available; the shared DSP and
audition-harness defaults retain Off.

The processor owns preset identity independently of parameter equality, so
edits and editor reopening retain the current factory or user target. Modified
status compares controls to the retained snapshot and clears when they return
to saved values. Saving a factory setting creates a same-name user file without
changing the factory definition; collisions direct Save preset as. Saving a
current user preset overwrites its file. Delete is enabled only for a current
user preset and resets defaults after successful deletion.

Files normally live under JUCE's per-user application-data directory in
`DisDorktion/Presets` and use `.disdorktionpreset`. Save as can choose another
location, adds the preset extension when absent and confirms overwriting the
final target. Load accepts valid external files regardless of extension. The default-folder menu is refreshed
when opened and lists only its direct preset files. An external file remains
the current user save target without being added to that listing.

Files use XML version-5 scalar snapshots and share the strict, side-effect-free
host-state codec: unsupported, incomplete, malformed, nonfinite or out-of-range
parameter input rejects the whole candidate before publication. Saves replace
the target through a temporary file. Cancelled or failed operations retain sound
and identity. Message-thread UI/filesystem work and generation checks protect
asynchronous operations from stale targets after a preset change or host restore.

Optional host-state identity metadata retains a valid save target. Valid
version-5 states without it still restore their saved values; absent or invalid
metadata, including a missing or invalid user file, detaches any prior target. Save then
opens Save as and Delete is disabled. No identity lock, string or filesystem
work is added to the audio callback.

Raw input/final output meters retain peaks and overloads between UI polls.
Controls publish independent scalar atomics, not a whole-state bank. Serialized
host lifecycle ownership governs state restore/reset. The editor starts at
1120x740, with limits 1000x680 to 1400x1000.

See [preset management validation](../notes/preset_management_validation.md),
[Driver adjustment status](../notes/driver_adjustment_validation.md),
[historical issue #38 evidence](../notes/issue38_validation.md) and the explicitly
historical [interface validation record](../notes/plugin_interface_validation.md).
No revised CPU, listening or host qualification is inferred from older results.
