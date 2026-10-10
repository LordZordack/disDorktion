---
type: Note
title: Bit crusher validation
description: Acceptance criteria and evidence status for the bit crusher implementation.
timestamp: 2026-10-10T00:00:00Z
tags: ["audio-dsp", "validation", "bit-crushing"]
resource: https://github.com/LordZordack/DisDorktion/issues/5
---

# Bit crusher validation

## Status

The legacy version-3 design was approved on 2026-10-10. Its source review found no remaining defects. Full
Debug, full Release, and independent DSP-only Release builds and test suites
passed under VS2026. The corresponding Visual Studio 2022 CI configuration has
not been run. Those legacy test, render, and CPU results predate the expressive
extension and do not qualify its new controls or formulas; extension-specific
final results are recorded separately below.

## Legacy version-3 design checks

- Confirm the depth menu covers 1–8, 16, 32, and 64 bits and defaults to 64.
- Confirm signed PCM rounding uses nearest with ties away from zero.
- Confirm saturation endpoints are [-1, 1 − 2^(1−B)], including the 1-bit range [-1, 0].
- Confirm the float32 sample interface and int64 quantization code path.
- Confirm capture rate is constrained to 100–384000 Hz and capped at the host rate, with a 384000 Hz default.
- Confirm capture-rate and bypass smoothers use 10 ms, with bypass initially off.
- Confirm depth changes requantize the held raw sample on the next frame without resetting phase.
- Confirm channels retain independent held samples while sharing a capture clock.
- Confirm immediate capture on the first frame, phase persistence across blocks, and zero latency.

## Legacy version-3 acceptance limits

- Compare against exact reference quantized float values; exhaustively check levels 1–8 and high-depth boundaries.
- Exercise mono and stereo at 44.1, 48, 88.2, 96, and 192 kHz with capacities 1, 32, 64, 127, 256, and 1024, plus 8 kHz and 384 kHz.
- Over 10 minutes at a noninteger capture/host ratio, phase error must remain within one host frame.
- Smoothed ramp output must be within 2e-6 * max(1, abs(expected)).
- Check integer-rate holds, split-block behavior, reset, and bypass exactly.
- Require zero realtime allocations.
- On three qualified, AC-powered, CPU-pinned runs, p99 processing time must stay below 5% of the block deadline at 48 and 96 kHz with block sizes 32, 64, 127, 256, and 1024.
- For a 6 kHz input with an 8 kHz capture rate, the alias peak must be at 2 kHz within one FFT bin and 0.1 dB of the reference.
- Listening evaluation was performed by the user on the verified sine and transient WAVs; the feedback was “Sounds as intended.” Listening across musical source categories remains pending.

See the [bit crushing method](../methods/bit_crushing.md).

## Evidence record

| Check | Evidence |
|---|---|
| Legacy full Debug | 86/86 tests passed; exit 0; 4.11 s (VS2026) |
| Legacy full Release | 86/86 tests passed; exit 0; 2.33 s (VS2026) |
| Legacy independent DSP-only Release | 63/63 tests passed; exit 0; 1.64 s (VS2026) |
| Legacy builds | All requested VS2026 builds succeeded; VS2022 CI not run |
| Legacy source review | No remaining defects found |
| Legacy render examples | Release sine, alias, and low-bit examples rendered; records: `docs/measurements/bit_crusher_renders/bit-crusher-sine/result.json`, `docs/measurements/bit_crusher_renders/bit-crusher-alias/result.json`, `docs/measurements/bit_crusher_renders/bit-crusher-low-bits/result.json` |
| Legacy spectral verification | Debug and Release alias results recorded below |
| Legacy user listening | Verified sine and transient WAVs: “Sounds as intended”; musical source categories pending |
| Legacy GUI restore/save manual check | Pending |
| Legacy CPU qualification | Passed: 120/120 qualified rows across 40 cases × 3 runs; details below |
| Unpinned diagnostic | Intentionally unqualified filtered diagnostic; details below |
| Legacy audition app startup smoke | App remained alive for 3 seconds and closed with exit 0; no GUI control inspection |
| Legacy graph analysis | Unresolved at that stage: 31 files, 71 symbols, 22 flows, aggregate CRITICAL risk |
| Expressive DSP Debug | 17 cases, 515,868 assertions passed |
| Expressive realtime Debug | 2 cases, 9 assertions passed; zero-allocation checks passed |
| Expressive full Debug | 102/102 tests passed; exit 0; 2.46 s (VS2026) |
| Expressive full Release | 102/102 tests passed; exit 0; 2.38 s (VS2026) |
| Expressive headless Release | 77/77 tests passed; exit 0; 1.74 s (VS2026) |
| Expressive builds | All full VS2026 builds exited 0, including Debug/Release UI seed-fix builds |
| Expressive render | V4 example render succeeded; record: `docs/measurements/bit_crusher_expressive_render/result.json` |
| Expressive fixtures | `docs/measurements/bit_crusher_expressive_fixtures/result.json` reports passed=true; legacy spectral reference error 0, 48/96 kHz peaks 1999.5117/2000.9766 Hz, latency 0; 8-event schedule source recorded |
| Expressive user listening | Pending; legacy listening feedback above does not apply to the new WAVs |
| Expressive manual GUI inspection | Pending; hidden startup smoke stayed alive for 3 seconds and closed with exit 0, with no GUI inspection |
| Expressive CPU initial attempt | INTERRUPTED/NOT QUALIFIED: 26 CSV rows, one unqualified; details below |
| Adaptive collector | Reviewed READY |
| Expressive CPU adaptive baseline | PASSED: 120/120 qualified rows across 40 cases × 3 runs; details below |
| Expressive unpinned diagnostic | Intentionally unqualified filtered diagnostic; details below |
| Expressive source review | READY; no remaining defects |
| Expressive graph analysis | Publication rerun: MCP returned all 104 detected symbols and 23 processes across 58 staged files, aggregate CRITICAL risk, with no partial/truncated flag. Index coverage remains unresolved for new files; refresh attempts stalled. This does not establish that unindexed code is unaffected. |

### Spectral measurements

Debug results are in `docs/measurements/bit_crusher_verified`; Release results
are in `docs/measurements/bit_crusher_release`. For the 48 kHz case, the peak
was 1999.51171875 Hz; for 96 kHz, 2000.9765625 Hz. Both configurations report
time error 0, spectral error 0, and latency 0.

An earlier `bit_crusher_fixtures` run reported a 5.421e-20 mismatch. The
independent reference had omitted 64-bit quantization near zero; the reference
was corrected. Preserve that initial failure as provenance; it was not a
crusher defect.

### CPU qualification

The baseline CSV at `docs/measurements/bit_crusher_baseline.csv` contains all
120 qualified rows: 40 cases per run across three runs, covering 48/96 kHz,
block sizes 32/64/127/256/1024, and the four documented scenarios. All rows
qualified against the 5% block-deadline budget. The worst budget fraction was
0.030 (3% of the budget, or 0.15% of the block deadline); maximum p99 across
all cases was 40,000 ns. At 96 kHz / 32 frames / automation, p99 was 500 ns
against a 16,666.667 ns budget.

Conditions: Intel Core Ultra 7 155H; MSVC 19.51 Release/x64; Windows SDK
10.0.26100.0; Windows 10.0.26300; AC power at 74%; Balanced power plan
(`381b4222-f694-41f0-9685-ff5bb260df2e`); benchmark pinned to performance class
1, mask `0x1`; no competing builds or tests. Thermal conditions were not
measured.

The unpinned diagnostic CSV at `docs/measurements/bit_crusher_unpinned_diagnostic.csv`
contains one filtered 48 kHz / 32-frame automation row and is intentionally
unqualified. It exited 1 as expected; p99 was 800 ns against a 33,333.333 ns
budget, with 147,479 samples at stride 156 over a ten-second window. It ran on
AC power at 94% and is diagnostic only; it is not part of the qualified
baseline.

The references establish fundamentals only; they do not constitute measurements or listening evidence for this implementation.

The smoother timing convention is grounded in the pinned [JUCE 8.0.14 `SmoothedValue` source](https://github.com/juce-framework/JUCE/blob/2cdfca8feb300fb424002ba2c2751569e5bacb64/modules/juce_audio_basics/utilities/juce_SmoothedValue.h): frame count is floored, advancement occurs on each next-value call, and the target is reached on the last ramp frame.

## Expressive extension validation

The approved extension adds mix, continuous crush, shape, deterministic
capture jitter, and version-4 experiment records. Its full VS2026 Debug,
Release, and independent headless Release suites passed; builds and render
checks succeeded. Source review is ready with no remaining defects. Extension
CPU qualification passed; extension listening, manual GUI inspection, VS2022
CI, and the GitNexus graph gate remain open. The
preceding legacy test counts, spectra, listening, and CPU CSV describe the
version-3 depth/capture-rate/bypass implementation only and do not qualify the
extension.

The Antigravity supplied-context review, verdict REVISE, is recorded at
`.tickets/issue-5-extension-agy-review.txt`. Its final math/schema revisions
are incorporated in the method. One regression fixed the neutral bypass
expression: at B=64 and 48 kHz with 100 Hz capture, a held +0.3 sample is
followed by dry -0.3 during a 480-frame bypass ramp. Recomputing the dry
weight as 1-(1-b) can change rounding near half-ramp; the implementation now
uses the original bypass expression when wetMix is exactly 1. The exact
regression test, `Neutral expressive settings preserve legacy bypass expression exactly`,
passed among the initial targeted Debug cases and the final full Debug suite.

The full Debug, Release, and headless suites passed, and both the version-4
example and expressive fixture report successful renders. New rendered WAVs
still need listening evaluation; no legacy listening feedback is carried over.
The first expressive baseline attempt, `docs/measurements/bit_crusher_expressive_baseline.csv`,
was stopped after 26 CSV rows: one is unqualified. The file is preserved as
interrupted and not qualified. Its 48 kHz / 1024-frame oversized case collected 47,099 timings,
below the 100,000 minimum; p99 was 278,800 ns against a 3,201,041.667 ns
budget. This was a sampling-count shortfall, not a timing-budget miss. The
adaptive three-run baseline at
`docs/measurements/bit_crusher_expressive_adaptive_baseline.csv` contains
120/120 qualified rows: 40 cases per run across three runs. The worst deadline
fraction was 1.3908661417322834% (run 3, 96 kHz, 127-frame automation: p99
18,400 ns against a 66,145.833 ns five-percent budget). Sample counts ranged
from 125,589 to 189,317; the longest collection window was 32.407 seconds.
Conditions: Intel Core Ultra 7 155H; MSVC 19.51 Release/x64; Windows 10.0.26300,
SDK 10.0.26100.0; AC power at 100%; Balanced plan
(`381b4222-f694-41f0-9685-ff5bb260df2e`); pinned performance class 1, mask
`0x1`; no competing builds or tests. Thermal conditions were not measured.

The unpinned diagnostic at
`docs/measurements/bit_crusher_expressive_unpinned_diagnostic.csv` is
intentionally unqualified: one filtered 48 kHz / 32-frame automation case,
exit 1, p99 8,000 ns against a 33,333.333 ns budget, and 148,189 timings over
10 seconds. It is diagnostic only and is not part of the qualified baseline.

The reviewed adaptive collector preserves the 2-second warmup and
2-second probe, projects count at 10 seconds, then chooses
max(10,min(120,1,500,000/projectedCountAt10Seconds)) seconds. It scales the
projection to the chosen window and selects a fixed stride targeting 150,000
observations within the 100,000–200,000 bound. All collected timings are included in percentile computation;
no outlier is discarded. This change is crusher-only; the gain collector
remains at 10 seconds. Manual GUI restore/save inspection, VS2022 CI, and a
complete refreshed GitNexus coverage also remain pending.

## Publication verification

Both VS2026 configure presets succeeded. Fresh publication builds passed for
full Debug, full Release, and independent DSP-only Release. CTest passed
102/102 in Debug (5.91 s), 102/102 in Release (2.65 s), and 77/77 in headless
Release (1.82 s). OKF validation and the staged whitespace check passed.

The final MCP `detect_changes(scope=all)` rerun returned every observed symbol
and process, unlike the capped CLI display. Its aggregate CRITICAL risk spans
audition controls and worker lifecycle, device preparation, experiment
serialization, live engine processing, offline rendering, and benchmarks.
These paths have corresponding automated coverage and source review. The
cached index does not resolve the new crusher symbols; zero graph matches are
not evidence of no callers. Source references and integration tests cover the
new DSP, target, mapping, timing collector, and measurement utility. Normal,
branch-specific, and isolated-snapshot refresh attempts stalled, leaving fresh
graph coverage as a review limitation. No clean refreshed-graph claim is made.

Small CSV and JSON evidence reports are committed. Generated WAVs and the
Antigravity transcript under `.tickets/` remain local; reproduce the WAVs with
the documented measurement and render commands. New expressive listening,
musical-source listening, manual GUI restoration/operation, VS2022 CI, and
refreshed graph coverage remain unchecked acceptance items.
