---
type: Note
title: Tanh saturation validation
description: Numerical, realtime, spectral, performance and listening evidence for the native-rate tanh module.
timestamp: 2026-10-10T00:00:00Z
tags: ["audio-dsp", "saturation", "validation"]
resource: https://github.com/LordZordack/DisDorktion/issues/6
---

# Tanh saturation validation

## Scope and acceptance

The [method design](../methods/tanh_saturation.md) was approved before implementation.
It specifies unnormalized tanh, explicit drive/output trim, double intermediates,
10 ms frame-based envelopes, exact settled bypass and zero module latency.
This is the isolated native-rate baseline for Q1. Production middle-section
anti-aliasing and plugin-chain integration require their own acceptance evidence
and renewed affected validation after quality changes.

The pinned JUCE 8.0.14 smoother source was inspected for floor-based duration,
advance timing, final target snap, and repeated targets. Drive/output amplitude
conversions are cached for unchanged dB settings. Core and harness callbacks
use scalar arithmetic, prepared dispatch, and lock-free scalar loads; source
inspection found no allocation, blocking, file/logging access or UI calls in
their processing paths. Persistence, loading and measurement output run outside
callback ownership.

## Verification record

After the coverage guard landed, full VS2026 Debug passed 119/119 tests in
6.56 s; full VS2026 Release passed 119/119 in 3.22 s; independent headless
Release passed 93/93 in 2.12 s. Version-5 records require the tanh target and
controls. Legacy v1-v4
parsing, rendering and defaults remain covered by regression tests.

Tests use an independent expm1 transfer oracle, independent frame interpolation,
strict rejection and state preservation, the supported rate/capacity matrix,
partition invariance, independent channel/instance behavior, exact silence,
finite extreme float inputs, reset/reprepare and zero latency. Realtime checks
track replaceable C++ allocation/deallocation on the current thread and inspect
callback code; they do not instrument arbitrary C allocators, custom allocators,
other threads, drivers or scheduler timing.

Version-5 records require the tanh target and its controls. Legacy v1–v4 parse,
render and default semantics are regression-tested. Device restoration has a
regression proving that recorded drive/trim apply before the first callback.

## Spectral and render evidence

The Release measurement tool passed all 18 spectral fixtures at 48 and 96 kHz.
Maximum transfer-reference sample error was 2.98022956313204e-8; maximum
significant-bin reference error was 0 dB at reported precision; worst measured
even-harmonic level was -248.8765 dBc. Fixtures include coherent single and
high-frequency sines, two-tone tests, and two-second logarithmic sweeps at
0 dB and 36 dB drive. The report includes finite-order folded odd-harmonic
estimates and collision rules. These are native-rate observations, not a
production antialiasing pass.

Repeating the measurement produced 46 files with identical SHA-256 hashes.
Four version-5 examples rendered successfully: tanh-sine.json (48,000 frames),
tanh-sweep.json (96,000), tanh-two-tone.json (48,000), and tanh-extreme.json
(48,000). All reported zero latency and no overloads.
Rerendering all four examples into fresh directories produced eight source/render
WAVs with identical SHA-256 hashes. Reusing measurement and render destinations
both exited with code 2 and left existing hashes unchanged. WAV headers were
confirmed as IEEE float format 3, 32-bit.

Generated constant listening fixtures use RMS-matched output trims. Mild uses
0 dB drive and 0.122767382 dB output trim, with actual wet RMS delta
-7.91e-9 dB. Extreme uses 36 dB drive and -16.10727516 dB output trim, with
actual wet RMS delta 2.34697e-7 dB. These are generated signals, not user
listening observations.

The initial Debug collection was canceled after the original CSV string
concatenation implementation ran for several minutes; its output is incomplete
and is not acceptance evidence. The CSV writer was changed to streaming output,
and final Release collection completed and passed.

## Performance evidence

The initial diagnostic attempt was interrupted after its benchmark fixture
generated an out-of-range float endpoint; the calculation was corrected to round
through double precision before converting to float. This fixture defect is
distinct from the first baseline sampling-cap failure below.

The first three-run baseline attempt is preserved but incomplete at 62
rows. In run 2, the 48 kHz/127-frame automation case exceeded the 200,000-sample
storage cap as callback throughput recovered from probe-time slowdown. Its
warmup/probe projections were 4,215,554/2,732,234 callbacks with stride 18. The
case recorded a 3,500 ns p99 against a 132,292 ns deadline budget, but this
truncated distribution does not qualify.

Revision 2 changed only the tanh collector. The full pinned Release baseline
exited 0 and qualified all 120 rows across three runs (40 matrix cases per
run). Per-case timing counts ranged from 148,469 to 149,711. Automation covered
all 101 control residues in every case, with per-residue counts from 987 to
2,065. The worst p99 was 5,000 ns for 96 kHz/127-frame bypass, or 0.377952756%
of its deadline, below the unchanged 5% budget. See the [revision-2 CSV](https://github.com/LordZordack/DisDorktion/blob/main/docs/measurements/tanh_saturation_cpu_baseline_revision2.csv).

The reference machine was an Intel Core Ultra 7 155H with 22 logical processors
and 33,752,997,888 bytes of RAM, Windows 10.0.26300, Windows SDK 10.0.26100,
MSVC 19.51, Release x64. It reported AC at 100% battery and the Balanced power
profile (scheme GUID prefix 381b); the benchmark pinned to group 0, mask 0x1,
efficiency class 1. CPU qualification passed under those recorded conditions.
A thermal sensor was unavailable and ambient temperature was unconfirmed, so
those conditions remain a documented limitation.

The full revision-2 unpinned diagnostic completed all 40 matrix rows and exited
1 as expected: every row was marked “unpinned diagnostic; reference
qualification unavailable,” with no additional errors or budget exceedances.
Timing counts ranged from 146,994 to 149,815. Every automation row covered all
101 residues, with 994 to 2,062 samples per residue. Worst p99 was 1,600 ns
(0.48% of deadline) for 96 kHz/32-frame automation. This diagnostic is
intentionally unqualified and separate from the passing pinned baseline.

## Listening and manual checks

User listening observations remain pending. No subjective observations are
inferred from generated fixtures or automated renders. Listening to licensed
drums, bass, guitar, vocal and synth material at matched monitoring levels
remains open, with source provenance, settings, rate and observations to be
recorded.

GUI layout inspection and interactive save/load/playback checks, VS2022 CI,
and production Q1 acceptance remain pending. Automated callback and record
tests do not establish these manual or external gates.

## Graph and publication checks

The final change analysis covered 186 symbols across 44 files and 28
flows without partial or truncated results; GitNexus reported CRITICAL graph
risk. Source review found no code blockers and the automated suites passed;
human listening and GUI/manual checks remain open.
The final OKF manager run completed index, log and link validation with exit 0.
The graph contains 1,678 nodes, 4,771 edges and 50 flows. Patch whitespace
checks passed. GUI/manual checks remain open; no commit or publication occurred.
