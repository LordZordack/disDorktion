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
- **Bit crusher** (`../methods/bit_crushing.md`): the legacy depth/capture-rate processor passed Debug, Release, DSP-only Release, render checks, and its three-run CPU baseline. The expressive mix/crush/shape/jitter extension passed VS2026 Debug, Release, headless Release, render checks, and its three-run CPU baseline; new-WAV listening, GUI inspection, VS2022 CI, and graph gate remain open. See the [validation note](../notes/bit_crusher_validation.md).
- **Tanh saturation** ([design](../methods/tanh_saturation.md)): the native-rate module and version-5 experiment target passed full VS2026 Debug (119/119), Release (119/119), and independent headless Release (93/93) suites. The Release measurement tool passed 18 fixtures; repeated output matched across 46 files, and four experiment examples rendered without overload or reported latency. The revision-2 CPU baseline qualified all 120 rows across three pinned runs under recorded conditions. The full unpinned diagnostic completed all 40 rows with the expected unqualified status. Thermal and ambient conditions were unavailable or unconfirmed. See the [validation note](../notes/tanh_saturation_validation.md). Human listening, GUI/VS2022 checks, Q1 antialiasing, and final documentation graph/OKF refresh remain open.
- **Methods** (`../methods/`): record each DSP design, its equations or algorithm, parameter ranges and endpoints, expected sound, aliasing and latency concerns, and alternatives. Link supporting papers and experiments.
- **Papers** (`../papers/`): preserve verified bibliographic details and source links, summarize the relevant finding, and identify the methods it informs. Do not fill gaps in source metadata by guessing.
- **Notes** (`../notes/`): record experiments and listening evaluations with test signal, sample rate, settings, observations or measurements, and the decision they support. Link the method and paper records where relevant.
- **Hardware** (`../hardware/`): document source-backed hardware details only when hardware is being studied or emulated.

## Evidence status

The [tanh saturation method](../methods/tanh_saturation.md) passed its automated correctness, spectral, deterministic-render, example-render and revision-2 CPU qualification checks under recorded conditions; see the [validation note](../notes/tanh_saturation_validation.md). The full unpinned diagnostic completed with its expected unqualified status. Thermal/ambient conditions were unavailable or unconfirmed. Generated fixtures are not user listening observations. Human listening, GUI/VS2022 checks, Q1 antialiasing and production integration remain open.

The [reusable gain design](../methods/gain.md) is implemented in the shared harness and independent plugin input/output stages. Debug and Release checks verify correctness; [gain validation](../notes/gain_validation.md) records fixtures and battery diagnostics. Human listening and manual host checks remain pending. The bit crusher's verified legacy sine and transient renders were user-auditioned as “Sounds as intended”; musical-category listening remains pending. Its three-run AC CPU baseline passed for the legacy version. Expressive CPU qualification passed; expressive WAV listening remains open.

Describe unimplemented designs as **proposed behavior**. The reference gain foundation's unit correctness is verified in the [foundation validation note](../notes/foundation_validation.md). Other production distortion modules remain unqualified. CPU performance is qualified for the bit crusher's legacy algorithm only; see its [validation note](../notes/bit_crusher_validation.md). Use **measured behavior** only for results supported by a documented test or listening evaluation; include the test conditions and link its note. Record whether evidence accepts, rejects, or revises the proposal.

## Open research questions

- Choose and qualify the shared anti-aliasing boundary for the nonlinear middle section.
- Establish parameter ranges, defaults, transitions, and identity behavior for each module before implementation.
- Determine whether the experimental modules meet their acceptance gates and merit integration.
