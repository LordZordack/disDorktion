---
type: Note
title: "Frequency dispersion validation"
description: "Issue 18 verification ledger, with automated and human acceptance kept explicit."
timestamp: 2026-11-03T00:00:00Z
tags: ["dispersion", "validation", "measurements"]
resource: "https://github.com/LordZordack/disDorktion/issues/18"
---

# Frequency dispersion validation

Compact publication evidence is in
`docs/measurements/frequency_dispersion_issue18_summary.json` and
`docs/measurements/frequency_dispersion_cpu_issue18_failure.json`.
Large raw captures remain local. The committed collectors and validators can
regenerate independent evidence in a fresh output directory.

[Design and predeclared gates](../methods/frequency_dispersion.md).
Implementation and automated functional/spectral verification are complete.
Human listening is pending; generated fixtures cannot establish that acceptance
gate. Production-chain integration remains X5.

## Build and regression evidence

Windows VS2026 CMake configuration and full Release build succeeded, including
the audition application and existing plugin formats. Updated DSP/realtime and
harness/realtime Debug test targets built successfully. Both
`ctest --preset windows-vs2026-debug --output-on-failure --no-tests=error --parallel 4`
and the equivalent `windows-vs2026-release` command passed 284/284 tests.
The original pre-change Debug baseline passed 270/270 tests.
Junit records are in build/windows-vs2026/issue18-{debug,release}-tests.xml.

Tests include independent direct-form static response, analytic frame ramp oracle,
automation energy passivity, retained transactional state, effective frequency,
exact dry endpoints, warm bypass histories, independent channels, all declared
rate/capacity combinations, allocation instrumentation, strict old/new record
schemas and exact isolated offline rendering. Source-only reviews found no
remaining blocker after adding the smoothing oracle. No executed TDD red phase
is claimed.

## Independent spectral and time-domain evidence

Release command:
`build/windows-vs2026/Release/disdorktion_dispersion_measurements.exe docs/measurements/frequency_dispersion_issue18_verified`.
Independent check:
`python scripts/validate_dispersion_measurements.py docs/measurements/frequency_dispersion_issue18_verified`.
The local Python 3.12 runtime was used. `python -m unittest discover -s tests -p
test_dispersion_validator.py` passed five validator regression tests.

42 static cases cover six sample rates, frequency endpoints/default, half/full
mix and legal Fs/4 pure-delay cases. Raw impulses, 129 complex bins, first-moment
tail bounds and source/executable hashes are in
`docs/measurements/frequency_dispersion_issue18_verified/result.json`.
Maximum errors: complex response 5.91e-8 absolute; fully wet magnitude
4.97e-7 dB; group delay 0.000171 samples. Maximum captured impulse amplitude
after one second is 2.69e-18. Maximum exported conservative static tail bound
among tested endpoint/default settings is 0.78 seconds. All passed predeclared
limits, including separately checked omitted l1 and first-moment tails.

Six automated sweep cases match an independent normalized lattice plus scalar
ramp oracle within 1.49e-8 absolute error. Pure-delay fixtures verify the
32-sample special case; settled two-tone samples match 997/1499 Hz with analytic
phase and magnitude, confirming that static dispersion does not remap frequencies.
This is no claim of alias-free automation or flat dry/wet magnitude.
An earlier frequency_dispersion_issue18 capture lacked the later automation
coverage and is superseded by the verified directory above.

## CPU and listening status

The initial pinned AC Release capture in frequency_dispersion_cpu_issue18 was
stopped after a budget failure: run 0, 192 kHz, 256 frames, automation p99
85.8 us = 6.435% of deadline (limit <5%). In 138986 observations, p50 was
34.2 us, p95 55.8 us, p999 893 us, maximum 28.4813 ms. First-quarter p99 was
236 us versus 37.4 us in the last quarter. Medians were similar across control
residues, so no parameter-specific hotspot was demonstrated. The underlying
system/scheduling/performance-state cause is unproven. A quick diagnostic repeat
passed this case at 21.5 us (1.6125%), but is explicitly unqualified due to its
short windows. The aborted raw capture and diagnostic remain retained.

A complete three-run repeat was attempted, using
`disdorktion_dispersion_benchmark --output docs/measurements/frequency_dispersion_cpu_issue18_verified --conditions ...`,
with background indexing/build/test jobs stopped. It was also stopped on failed
budget gates in different cases: run 0, 96 kHz/256 frames static p99 198.6 us
(7.4475%) and bypass p99 176 us (6.6%). Processing, sampling coverage and stable
AC power gates passed for these rows. Performance acceptance is FAILED, not
qualified. Both aborted raw captures are retained; no three-run success is claimed.
The passing quick diagnostic does not override either failed capture. No DSP
change was justified by a reproducible control-specific hotspot, and the 5% gate
was not weakened. Further performance qualification requires diagnosing or
controlling this machine's substantial timing variability.
The required complete matrix is 4 rates × 3 blocks × 4 scenarios × 3 runs, p99 <5% deadline,
with 100k–200k wall-time-sampled observations per case. Raw data must pass
`scripts/validate_dispersion_cpu.py` before performance qualification is claimed.
First-failure analysis is saved in
`docs/measurements/frequency_dispersion_cpu_issue18_failure.json`.

The example `examples/experiments/dispersion-impulse.json` rendered successfully
with the Release CLI to `docs/measurements/frequency_dispersion_example_issue18`.
GitNexus change analysis identified the shared UI/loading/persistence flows and
reported critical aggregate risk; these received source review and regression
coverage. Fresh index attempts stalled, including an isolated index attempt,
and were canceled. New-symbol graph coverage therefore remains unavailable;
the initial implementation remained uncommitted. GUI visual interaction is unverified.

36 wet WAV examples cover impulse, kick, snare, bright percussion, bass and
two-tone at 20/500/20000 Hz and 50/100% mix. Sources are generated deterministically;
metadata includes settings, peaks and RMS. Automated sweeps are also captured.
The user reports that the isolated effect does not change the sound greatly.
On 2026-11-03 they chose to experiment further in the integrated plugin later and
requested PR publication. More aggressive signal-driven phase distortion,
saturation and feedback are possible future experiments, not implemented features.
This is limited subjective feedback, not complete listening qualification across
the declared matrix. CPU qualification remains failed. The PR is therefore a draft;
neither the listening deferral nor publication waives remaining acceptance gates.
