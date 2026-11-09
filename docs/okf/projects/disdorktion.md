---
type: Project
title: "DisDorktion audio distortion plugin"
description: "Project record for the modular Windows VST3 distortion plugin and its standalone audition application."
timestamp: 2026-11-08T00:00:00Z
tags: ["audio", "distortion", "JUCE", "project"]
---

# DisDorktion audio distortion plugin

The intended first release is a mono/stereo Windows VST3 with a standalone
audition application. Carrier modulation/demodulation was discarded on
2026-11-07. The current prototype's production path is Input Gain ->
global dry/wet split -> crusher/sampler -> Waveshaper -> comb -> dispersion ->
tone -> HP -> LP -> global mixing ->
Final Gain. See [full-chain design](../methods/full_chain.md).

Input Gain affects both global branches. Driver is 1x to 4x, default 1x, and acts
after the crusher immediately before tanh/fold inside the nonlinear wet branch.
Fixed calibrated compensation follows nonlinear output before shared trim and
section mixing, following the delayed 10 ms Driver trajectory. Global dry and
Waveshaper dry remain unchanged. Both stages Off, Waveshaper Mix zero or global
mix zero make Driver inaudible. There is no live tracking or AGC. Input Mix covers
the complete crusher/sampler. Waveshaper Mix covers both nonlinear stages and
shared post-stage trim. Both stages Off retain trim on delayed dry; section
mix zero resets/suspends unnecessary wet work and reactivation warms histories
before fading in. Production latency remains fixed at 92 samples.

The [interface](../methods/plugin_interface.md) has Input, Waveshaper, Output and
GLOBAL panels with no visible mute/bypass controls. DAW bypass remains. Hz
movement is logarithmic; nonlinear minima, HP minimum and LP maximum expose
Off. Comb damping is always active during processing, and zero mix clears its
history after fading out. The prototype replaces old automation/state
compatibility with 27 controls and version-5-only saves.
The Driver adjustment keeps those parameter IDs and persistence schemas;
existing saved sounds above Driver 1x intentionally change, while defaults at
1x retain their prior behavior.

## Research and evidence

The [Driver adjustment record](../notes/driver_adjustment_validation.md) records
the approved calibration method and pending verification. The
[issue #38 validation record](../notes/issue38_validation.md) preserves earlier
automated passes as historical evidence; they do not qualify the adjusted Driver.
No revised CPU, host or listening qualification is claimed. Existing
[input](../notes/input_section_validation.md),
[Waveshaper predecessor](../notes/middle_section_validation.md),
[full-chain](../notes/full_chain_validation.md),
[interface](../notes/plugin_interface_validation.md) and
[expanded Output](../notes/expanded_output_validation.md) measurements describe
their historical implementations and conditions. CPU qualification is specific
to each measured implementation and budget.

The standalone [InputSection](../methods/input_section.md) convenience continues
to compose Gain -> BitCrusher. FullChain owns its own pre-input Gain and direct
BitCrusher so gain precedes Driver and both global branches. Isolated modules
and the development harness may retain test bypass controls that are not plugin
parameters. Internal FullChainSettings bypass is likewise harness-only.

Preserve algorithms, endpoints, latency concerns and alternatives in
[methods](../methods/index.md); verified source metadata and relevant findings
in [papers](../papers/index.md); and test conditions, measurements and decisions
in [notes](../notes/index.md). Distinguish implemented behavior from measured
acceptance. Generated fixtures are not human listening observations. Further
anti-aliasing, representative-source, host and reference performance acceptance
must be tied to the revision actually evaluated.
