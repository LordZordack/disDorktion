---
type: Note
title: "Tone Module Qualification"
description: "Tone implementation evidence and explicit remaining listening and reference-performance gates."
timestamp: 2026-11-03T00:00:00Z
tags: ["tone", "validation", "filter"]
---

# Tone qualification

The [tone design](../methods/tone.md) defines limits before implementation.
Antigravity reviewed supplied plan/contract text without repository tools. Its
useful suggestions clarified state realization, identical-target ramp continuity,
startup/reset behavior, phase checks and 192 kHz CPU coverage. Its critical 8 kHz
claim was rejected: the fixed 1 kHz prewarp remains valid throughout 8–384 kHz.
The original 1e-5 complex tolerance is retained rather than loosened based solely
on a generic float epsilon argument. Local review details are in the issue ticket.

## Implementation and verification scope

The isolated module and audition target use version-14 records. Tests compare
impulse spectra and independent direct-form sine/sweep/transient reference
samples, partitioning, reset, controls, neutral, bypass, stereo independence,
invalid settings and realtime allocation behavior. CPU collectors include control
application and isolated DSP; GUI/source generation/meter work is outside timing.
Raw timing auditing recomputes p99 rather than trusting summaries.

Final VS2026 Debug and Release CTest suites passed **292/292** each; DSP-only
Release passed **234/234**. Targeted Release tone DSP tests passed 1,259,328
assertions in three cases; tone harness/qualification tests passed 116,371
assertions in four cases. The realtime allocation test passed four assertions.
Three Python validator tests passed. The Release audition application and
renderer built successfully. Early Release harness crashes also affected legacy
tests; a clean rebuild resolved them, consistent with stale build artifacts.

Independent response auditing passed **35 cases / 3,535 bins**, maximum absolute
complex error **7.502716636853535e-8**, versus the 1e-5 limit. Evidence is under
`docs/measurements/tone_issue19`. The 0.1-second impulse capture meets this error
gate across all tested rates; the response grid includes DC, Nyquist and pivot.

The 105-case quick CPU capture and raw p99 audit passed their diagnostic
processing/budget checks. Worst p99 was **0.99%** of incoming callback deadline,
below the proposed 2% ceiling. The collector deliberately returned **1** because
quick runs are unqualified. Capture used observed AC and performance-core affinity
on Intel Core Ultra 7 155H, MSVC 19.51 Release x64, SDK 10.0.26100.0, Windows
10.0.26300; ambient and thermal state were unmeasured. No competing task builds,
tests or indexing ran during capture. Many quick rows lack reference timing or
control coverage. Evidence is under `docs/measurements/tone_cpu_issue19_quick`.
The compact result/summary files are published; the approximately 119 MB raw
`timings.csv` remains local, along with generated audio. Recollect timings with
the documented tool to rerun the raw audit on another checkout.

The listening-preparation script successfully rendered and RMS-matched a
48 kHz stereo synthetic sine source into dry/dark/neutral/bright comparisons
under `docs/measurements/tone_listening_issue19_sine`, retaining hashes, settings
and matching gains. This verifies the workflow, not musical acceptance.

A final supplied-source/diff Antigravity code review reported no actionable
important or critical findings. This review had no repository tool access and
did not independently execute tests. Prepublication structured GitNexus analysis
covered all 42 staged files and 123 symbols, with 36 affected processes and
neither partial nor truncated results. It reports critical risk across shared
GUI/persistence flows; the passing regression suites do not erase that rating.
FTS was unavailable in the isolated backend; diff/symbol/process analysis
completed. OKF validation and staged whitespace checks passed. The PR remains a
draft until the acceptance gates below are resolved.

## Remaining acceptance gates

Quick CPU evidence is diagnostic only. Three pinned AC Release reference captures
and a coverage/environment audit are required for the proposed 2% CPU ceiling.
The collector covers dark/bright/neutral static, full-range tone automation,
bypass transitions, quiet decay and oversized callbacks. Full three-run capture
contains 315 cases and takes at least approximately 74 minutes before slow-case
extensions; it has not been run in this implementation session.

Human matched-level listening and GUI acceptance remain open. Use guitar, bass,
percussion and a full mix at 48/96 kHz: compare -6/0/+6 dB with bypass, record
source hash, exact settings, output peaks and matching gain, and compare ramps
and quiet passages. Match RMS on the same active interval, apply only attenuation
when needed for headroom, and retain uncompensated measurements separately.
Do not treat generated or rendered files as human listening approval.

The existing [output section](output_section_validation.md) still has dependency
and manual gates. No issue closure, independent module acceptance or X5 chain
qualification is inferred from the implementation. VS2022/CI and host behavior
require their actual environments. Production-chain integration remains X5.
