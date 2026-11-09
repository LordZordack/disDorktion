---
type: Note
title: "Issue 38 prototype control and routing validation"
description: "Historical automated validation of the original issue-38 routing, before the nonlinear-wet-only Driver adjustment."
timestamp: 2026-11-08T00:00:00Z
tags: ["audio-dsp", "validation", "driver", "latency", "inactive-processing"]
resource: "https://github.com/LordZordack/disDorktion/issues/38"
---

# Issue 38 prototype control and routing validation

## Historical scope

This record preserves the original issue-38 implementation and its reported
results. That revision placed Driver before the global split and inverse
compensation after global mixing. The subsequent approved Driver adjustment
moves Driver into the nonlinear wet branch and uses a fixed calibrated lookup;
its calibration and verification status are tracked separately in
[Driver adjustment validation](./driver_adjustment_validation.md). The passes
below do not qualify that adjusted routing, lookup or automation behavior.

## Original issue-38 implemented contract

The original [FullChain](../methods/full_chain.md) applied Input Gain -> Driver before the
global split, then delayed reciprocal Driver compensation after global mixing
and before Final Gain. Driver was 1x to 4x, default 1x, with frame-associated
compensation; no AGC or constant perceived-loudness claim is made. The production
chain owns its pre-input Gain and direct BitCrusher. The standalone
[InputSection](../methods/input_section.md) convenience retains Gain -> BitCrusher.

[Waveshaper](../methods/middle_section.md) has shared post-stage trim and section
mix. Both stages Off select trimmed delayed dry, with trim active; mix blends
against untrimmed delayed dry. Zero-mix states reset/suspend unnecessary wet
processing, keep the dry delay advancing, and warm wet histories before fading
back in. Production latency remains 92 samples. Comb history clears after its
mix fade settles at zero, and damping is always enabled during active processing.

The [interface](../methods/plugin_interface.md) has no visible mute/bypass
controls, retains DAW bypass, uses logarithmic Hz mappings and explicit
Tanh/Fold/HP/LP Off endpoints. Internal `FullChainSettings::bypass` remains a
development-harness setting, with no plugin parameter. The prototype replaces
old state/automation compatibility with 27 controls and version-5-only state.

## Historical automated verification

The implementation owner reported these passing focused runs:

| Check | Cases | Assertions | Result |
| --- | --- | --- | --- |
| DSP tags `[driver],[section-mix],[inactive],[middle]` | 22 | 2,394,065 | Pass |
| Realtime suite | 18 | 86 | Pass |
| Plugin controls, logarithmic UI, state and presets | 25 | 290,751 | Pass |
| Full-chain tests | 7 | 177,522 | Pass |

Final verification by the implementation owner:

- Complete `windows-vs2026-release` build passed, including VST3, standalone,
  audition app, tests and measurement tools.
- `ctest --preset windows-vs2026-release --parallel 4` passed all **309 tests**.
  Coverage includes version-17 live audio restoration, standalone Output panel
  descriptor completeness, canonical experiment state and realtime allocation.
- Python discovery (`test_*.py`) passed all **84 tests**.
- The final full-chain generator produced **272 cases**; schema-2 independent
  validation passed against the final executable/source provenance. Ordered,
  bypass and block-partition errors were zero; maximum dry error was below 1e-6.
  Both-stages-Off impulse fixtures measured 92 samples at 48/96 kHz in mono and
  stereo. These fixtures exercise the delayed-dry branch; active nonlinear
  timing is covered by the section/full-chain tests.
- Driver automation on neutral dry, wet and mid-mix paths passed 38,226
  assertions. Independent source review found no remaining actionable defects
  after three audition/serialization integration fixes.

These are automated functional and allocation-check results. They do not
establish measured reference CPU performance or human listening acceptance.

## Remaining qualification

Host/editor inspection, representative-source listening and renewed reference
CPU measurement were unperformed for the recorded revision. Check inactive processing
through the included deterministic suspension tests; reference CPU qualification
remains separate. Historical measurement/listening records below and in linked
notes apply only to their recorded revisions, including prior routing, defaults,
bypass controls and persistence schemas; they do not qualify issue #38.

Related historical evidence: [full chain](./full_chain_validation.md),
[interface](./plugin_interface_validation.md),
[Waveshaper predecessor](./middle_section_validation.md),
[Output](./expanded_output_validation.md).
