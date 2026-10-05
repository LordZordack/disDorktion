---
type: Project
title: "DisDorktion audio distortion plugin"
description: "Project record for the modular Windows VST3 distortion plugin and its standalone audition application."
timestamp: 2026-10-04T00:00:00Z
tags: ["audio", "distortion", "JUCE", "project"]
---

# DisDorktion audio distortion plugin

## Project direction

The agreed first release is a mono and stereo Windows VST3 plugin with a standalone audition application. Its serial wet path is input gain, bit crusher, tanh saturation, wave folder, high-pass filter, and low-pass filter. A global dry/wet control follows the output filters, then final output gain. The dry signal is captured before input gain; final output gain affects both paths. See the [overarching project plan](https://github.com/LordZordack/DisDorktion/blob/main/docs/PROJECT_PLAN.md) for architecture, acceptance gates, tests, and release strategy.

Carrier modulation and demodulation, resonant comb filtering, frequency warping, and a dedicated tone module are experimental extensions. Research may precede the first release; integration follows qualification.

## Research records

- **Processing foundation** (`../methods/processing_contract.md`): implemented real-time module interface, callback rules, and reference gain behavior. See the [DSP contract](https://github.com/LordZordack/DisDorktion/blob/main/docs/DSP_CONTRACT.md), [performance procedure](https://github.com/LordZordack/DisDorktion/blob/main/docs/PERFORMANCE.md), and [foundation validation note](../notes/foundation_validation.md).
- **Methods** (`../methods/`): record each DSP design, its equations or algorithm, parameter ranges and endpoints, expected sound, aliasing and latency concerns, and alternatives. Link supporting papers and experiments.
- **Papers** (`../papers/`): preserve verified bibliographic details and source links, summarize the relevant finding, and identify the methods it informs. Do not fill gaps in source metadata by guessing.
- **Notes** (`../notes/`): record experiments and listening evaluations with test signal, sample rate, settings, observations or measurements, and the decision they support. Link the method and paper records where relevant.
- **Hardware** (`../hardware/`): document source-backed hardware details only when hardware is being studied or emulated.

## Evidence status

Describe unimplemented designs as **proposed behavior**. The reference gain foundation's unit correctness is verified in the [foundation validation note](../notes/foundation_validation.md). Production distortion modules remain unqualified. CPU performance has not been qualified because the recorded benchmark work was performed on battery power. Use **measured behavior** only for results supported by a documented test or listening evaluation; include the test conditions and link its note. Record whether evidence accepts, rejects, or revises the proposal.

## Open research questions

- Choose and qualify the shared anti-aliasing boundary for the nonlinear middle section.
- Establish parameter ranges, defaults, transitions, and identity behavior for each module before implementation.
- Determine whether the experimental modules meet their acceptance gates and merit integration.
