---
type: Method
title: "Audition and measurement harness"
description: "A reproducible source and record workflow for shared DSP audition and exact pre-monitor offline rendering."
timestamp: 2026-10-10T00:00:00Z
tags: ["dsp", "audition", "measurement", "reproducibility"]
---

# Audition and measurement harness

The harness evaluates the shared module implementation using generated signals, resident file loops, or a monophonic keyboard/MIDI sine voice. Initial target `reference-gain` establishes the workflow before production modules are accepted. See the [project](../projects/disdorktion.md), [processing contract](processing_contract.md), [user/developer guide](https://github.com/LordZordack/DisDorktion/blob/main/docs/AUDITION_HARNESS.md), and [validation record](../notes/harness_validation.md). Repository documentation links become available on GitHub when this work is published.

## Method

Record source settings, target parameters/bypass, active process specification, source length, exact render length, original file hash/format/conversion where applicable, monitoring, build identity, and observations in versioned JSON. Keep observations descriptive until listening or numerical measurements actually occur. Compare experiments with matched input/rate and document changes deliberately.

Take input and output peak/RMS before monitoring. Monitoring is a separate smoothed −60–0 dB control plus mute, starting muted. A pre-monitor magnitude above 1 produces overload; no normalization or limiter modifies the experiment. Offline rendering writes exact-length source and processed float WAVs with aggregate measurements, latency, experiment provenance, and current renderer identity. Save the experiment's original identity separately from the renderer identity.

Generated impulse, sine, two-tone, logarithmic sweep, and seeded noise restart deterministically. Live finite playback stops at its source count and returns the control to Play; pressing Play repeats from the beginning. Offline rendering continues with silence after the source count when the requested render is longer. Render duration is independent. File loops wrap exactly with no crossfade and repeat to the requested render count. Both paths use the shared linear conversion, mono duplication, and equal-weight stereo averaging. Linear downsampling has no anti-alias filter, so rate-matched input is required for controlled bandwidth studies. Endpoint discontinuities can affect loop spectra/listening.

## Scope and limitations

The production `gain` target uses version-2 `gainDb`, `mute` and `bypass` fields; reference version-1 records retain linear gain semantics. Module mute and monitor mute are distinct. See the [gain design](gain.md) and [validation record](../notes/gain_validation.md) for transition fixtures and observed results.

Source loading/hashing/conversion and output writing occur outside realtime playback. Resident PCM and bounded MIDI transport support callback ownership; lifecycle changes detach processing. The initial 256 MiB PCM budget bounds decoded, converted, and old resident data. MIDI uses last-note priority and 5 ms envelope ramps with one-block delayed arrival-time mapping, late-event clamping, and overflow all-notes-off. It is an audition input, not a saved deterministic performance; offline comparisons require generated/file sources.

This implementation supplies measurement tools. It does not establish production sound acceptance, anti-aliasing quality, host qualification, or CPU budgets. Refer to the validation record for observed checks and explicitly outstanding listening/device work.
