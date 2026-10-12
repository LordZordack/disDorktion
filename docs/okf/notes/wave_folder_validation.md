---
type: Note
title: "Wave folder validation"
description: "Evidence and outstanding acceptance gates for the isolated native-rate wave folder."
timestamp: 2026-10-11T00:00:00Z
tags: ["audio-dsp", "wave-folding", "validation", "aliasing"]
resource: "https://github.com/LordZordack/disDorktion/issues/7"
---

# Wave folder validation

The [method and acceptance limits](../methods/wave_folding.md) were recorded before implementation. The user authorized implementation after an Antigravity plan review on 2026-10-11. The review was based on supplied plan/contract/code context and performed no repository tests.

## Current evidence

The pinned GitNexus branch index matches starting HEAD `ba2c2dc9c4297f627077eabd55e46aec2cef14ca`: 1,678 nodes, 4,771 relationships, and 50 execution flows. The workspace MCP reader retains older metadata, so current CLI checks use the pinned `feat/issue-7-wave-folding` index. Per-symbol pre-edit checks and UNKNOWN text confirmations are retained locally.

## Acceptance status

Automated correctness, spectral, deterministic-render and CPU qualification evidence is recorded below. Human listening, GUI interaction, VS2022 checks and future Q1 production quality remain separate evidence requirements.

Related: [DisDorktion project](../projects/disdorktion.md), [Buchla research context](../papers/virtual_analog_buchla_259_wavefolder.md).

## Automated implementation evidence (2026-10-11)

The shared module, harness target, version-6 record, offline renderer and audition
controls are implemented. Default controls are identity within [-1,1]; input
outside the linear region folds. Threshold changes the wet ceiling. The plugin
chain is unchanged until S2; anti-aliasing remains Q1 work.

| Check | Result |
| --- | --- |
| VS2026 x64 Debug build and CTest | 140/140 passed |
| VS2026 x64 Release build and CTest | 140/140 passed |
| Independent DSP-only Release build and CTest | 111/111 passed |
| Focused DSP test oracle | 2,565,977 assertions in 10 cases passed |
| Legacy example field round trips | 660 assertions passed; numeric representation and typed float rounding preserved |
| Debug/Release spectral generation | 62 fixtures passed in each run |
| Repeated Release fixtures | 133 audio/CSV files byte-identical; timestamp/build JSON provenance excluded |
| Four version-6 examples, rendered twice | All 8 source/wet WAV comparisons matched; exact requested lengths, zero latency, no overload |
| Existing measurement/render destination | Both rejected with exit 2 |
| Source review | No open implementation blocker found; identified coverage gaps addressed |
| OKF validation and whitespace | Passed |
| VS2022 configure | Unavailable: v143 build tools missing (MSB8020) |

Tests cover fold boundaries and wrap slopes, independent cell/parity oracle,
odd symmetry, silence, exact settled bypass, finite float extremes, all control
endpoints, independent/interrupted/repeated ramps, retained failed prepare/reset,
mono/stereo and the reviewed rates/capacities. Allocation instrumentation covers
DSP, harness and device paths; concurrent scalar writes are exercised. These
checks have narrower scope than a proof of all realtime behavior.

The identity stub first failed 7/10 DSP cases at actual numerical assertions,
then the implementation passed. Target selection and version-6 parsing both
failed before integration. The added legacy test initially compared JSON text
(`48000` versus `48000.0`); correcting it to typed field-value comparisons
resolved the test error without changing production persistence.

## Native-rate measurements

The refreshed branch graph contains 1,846 nodes, 5,430 relationships and 59
flows. Complete `all` and `compare main` change reports include all 13 new issue
files using a temporary alternate Git index; the real index was unchanged.
Both scopes report 41 files, 180/180 symbols and 35/35 processes, with
`partial=false` and `truncated=false`. Aggregate risk is **CRITICAL**, covering
DSP, schema and UI restore/control flows; it is not an all-clear. Source review
found no open blocker and the full correctness suites cover these paths.
Dynamic dispatch and name-fallback graph limits remain. Refresh again if source
changes before committing.

Published compact metrics: `docs/measurements/wave_folder_summary.json`.
Full result JSON, all-bin CSV and WAVs remain reproducible local artifacts in
`docs/measurements/wave_folder_fixtures/`; their size excludes them from this PR.
Regenerate them with `build/windows-vs2026/Release/disdorktion_wave_folder_measurements.exe <fresh-output-directory>`.
Tool: `disdorktion_wave_folder_measurements`; MSVC 19.51 / JUCE 8.0.14,
Release x64, build revision `ba2c2dc` plus the uncommitted issue-7 changes.

The 48/96 kHz coherent sine matrix uses 65,536 frames, bins 1,365 and 9,833,
input peak 0.25, drive -24/0/12/36 dB, threshold 1/0.5/0.05 and trim 0 dB.
It also records low/high two-tone products, two-second logarithmic sweeps,
silence, and stereo tone/transient control transitions. Every source is generated.

Maximum saved-sample oracle error was 2.98e-8 (limit 2e-7 times max(1,g*T)).
Maximum significant-bin deviation was 0.00540 dB (limit 0.1 dB for reference
bins above -80 dBr). Worst even-index bin in applicable sine fixtures was
-165.87 dBr (limit -100 dBr). dBr uses the largest non-DC reference bin;
weak/null fundamentals produce null conventional THD with an explicit validity
flag. The nonfundamental RMS ratio includes aliases and residual components.
Harmonic and intermodulation locations identify possible collisions; measured
bin magnitudes cannot isolate coincident products.

Sampled-reference agreement establishes implementation fidelity. Sharp triangle
corners produce harmonics and foldback; no alias-suppression pass is claimed.
The tool preserves source/settings/rates for comparison by Q1.

## Listening material and remaining gates

Generated mild comparison: +6 dB drive, threshold 0.5, trim -5.78555 dB.
Generated aggressive comparison: +18 dB drive, threshold 0.25, trim +0.62884 dB.
Both saved wet/source pairs match RMS within 0.000001 dB (limit 0.5 dB).
Published measurement metadata (`docs/measurements/wave_folder_summary.json`)
records the exact source equations and settings. The fixture directory contains
`matched-mild-{source,wet}.wav`, `matched-aggressive-{source,wet}.wav` and
stereo transition WAVs. They are candidates, not human evaluations.

Human listening must assess useful settings and limitations on generated tones,
transients, drums/bass/vocals/broadband material with known licensing. GUI
interaction and VS2022 CI remain pending. CPU qualification is recorded below.
Thermal sensing and ambient conditions are not inferred.
Q1 anti-aliasing and S2 chain integration require renewed affected validation.

## CPU qualification

Three-run baseline CSV (`docs/measurements/wave_folder_cpu_baseline.csv`):
**120/120 rows qualified**, benchmark exit 0. All rates, capacities and four
scenarios in the reviewed matrix completed on AC with performance-core thread
affinity. Worst p99 fraction was **1.01867%** of the full incoming deadline
(limit 5%): run 1, 96 kHz, prepared capacity 256, incoming 769-frame oversized
block, p99 81.6 microseconds. Maximum latency is still zero samples.

Each row contains 136,235–149,966 recorded timings over 10–13.825 seconds;
every automation case covers all 101 settings, at least 532 samples per
setting. Warmup/probe, equal-wall-time collector revision 2, timing overhead,
percentiles and unfiltered maxima are retained in the CSV. No build or fixture
generation competed with the qualification runs.

Observed machine: Intel Core Ultra 7 155H, 22 logical processors,
33,752,997,888 bytes RAM, Windows 10.0.26300, SDK 10.0.26100.0,
MSVC 19.51 full 195136252 Release x64. AC power, battery 99%, Balanced scheme
381b4222-f694-41f0-9685-ff5bb260df2e. Benchmark thread selected group 0/mask 0x1,
efficiency class 1; prior mask 0x3fffff is restored by the harness. Thermal
sensor readings were unavailable and ambient temperature unconfirmed.
These results qualify the isolated module under the recorded conditions,
not the future complete wet chain or arbitrary devices.

The unpinned diagnostic CSV (`docs/measurements/wave_folder_cpu_diagnostic.csv`)
completed all 40 rows with the expected exit 1 and no budget exceedances.
Worst p99 was 1.7925% of the incoming deadline. Every row is intentionally
unqualified, with the recorded reason `unpinned diagnostic; reference
qualification unavailable`.

Quick single-case tiny/wide/extreme-input diagnostics returned 1, conflicting
input flags returned 2, and an existing benchmark output was rejected with
exit 2. These smoke checks do not qualify those input profiles.
Human listening and GUI/VS2022 checks remain open regardless of CPU results.
