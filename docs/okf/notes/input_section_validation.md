---
type: Note
title: "Input section qualification"
description: "Recorded correctness, realtime, interaction, CPU and listening evidence for issue 10, with unresolved gates kept explicit."
timestamp: 2026-10-13T00:00:00Z
tags: ["input-section", "validation", "performance", "audition"]
resource: "https://github.com/LordZordack/disDorktion/issues/10"
---

# Input section qualification

The [section design](../methods/input_section.md) preserves the constituent
[gain](../methods/gain.md) and [crusher](../methods/bit_crushing.md) algorithms.
Section acceptance is separate from the [gain evidence](./gain_validation.md)
and [crusher evidence](./bit_crusher_validation.md). Those dependency issues
are closed; their historical unresolved listening/host evidence is not silently
converted to a pass by this integration.

## Recorded implementation checks

The initial Debug core build failed because the new InputSection implementation
was absent. The version-8 example was rejected by the previous renderer with
`Unsupported or missing version`. The registry regression caught the newly
added target (6 entries versus the old expectation of 5).

Core Debug checks passed 7 cases / 4,047,572 assertions, covering exact sequential
and partition agreement, all depths and bypass combinations, sample-rate/capacity
domains, invalid preparation and asymmetric control rejection with history
probes, held-sample mute behavior, zero-length state preservation, extreme
supported finite input, reset and instance independence. The core realtime test
passed with zero allocations and deallocations. The scoped core source review
found no actionable issue; these checks do not qualify CPU or listening.

The initial harness/record/offline suite passed 4 cases / 354 assertions,
including hidden post-gain overload, common bypass retention, strict version-8
round trips, malformed/mixed schemas and exact core/harness agreement.

## Final automated suite results

| Configuration | Actual result |
| --- | --- |
| VS2026 Debug, all configured targets | Build passed; 191/191 CTest cases passed in 16.00 seconds |
| VS2026 Release, all configured targets | Build passed; 191/191 CTest cases passed in 16.06 seconds |
| VS2026 DSP-only Release | Build passed; 160/160 CTest cases passed in 12.72 seconds |

The final counts include exact rendered float WAV/core comparisons over all
eight child/common bypass states, monitoring independence across capacities
7/127, live child edits under common override and release, and benchmark timing
coverage/endpoints/alias rejection. Core and harness source reviews found no
actionable defect. The broad review's LOW benchmark-label finding was corrected:
dynamic bypass now cycles all four states every eight callbacks and requires
at least 100 timed observations in each state. All legacy benchmark macro
targets were rebuilt after the input-only changes.

## Interaction measurements and render examples

Release `disdorktion_input_measurements` generated five deterministic synthetic
sources at each of 48/96 kHz, eight seconds each, with exact event schedules for
gain overload, classical/expressive quantization, reduced capture rates, all
bypass states, mute/held tails, jitter and 16 rapid updates spaced at 1 ms.
Capacity 127 with cyclic incoming blocks 32/64/127/256/1024 exercises oversized
dispatch. Forty float32 source/post-gain/wet/sequential-reference WAVs, CSV and
JSON were generated twice; **all 42 files matched exactly by SHA256**.

All ten cases passed the independent stdlib validator, including source formulas,
settled gain, neutral/classical PCM, exact composition, double bypass, aggregate
peaks/RMS/counts, CSV consistency and 320 spectral projection observations.
Maximum composition and full-call interstage meter errors are **0**; declared
latency is **0**. Post-gain peaks range from 3.753981828689575 to
5.943132400512695, with 16397-506282 overloaded channel samples per fixture.
Dry/bypass segments preserve post-gain values above unity, as designed.

The first validation found a 6.8e-12 RMS difference caused by channel-major versus
interleaved summation of long WAVs. Only aggregate RMS tolerance changed from
2e-12 to 2e-10 relative/absolute. Peak comparison remains 2e-12, and sample
equality, overload counts and meter-error gates remain exact. Negative copies
with a changed peak, missing case or corrupted reference sample were rejected
(exit 1). Reusing the real generator destination was rejected (exit 2), with
all evidence hashes unchanged.

The [result JSON](https://github.com/LordZordack/DisDorktion/blob/main/docs/measurements/input_section_issue10/result.json),
[metrics CSV](https://github.com/LordZordack/DisDorktion/blob/main/docs/measurements/input_section_issue10/metrics.csv)
and [source snapshot](https://github.com/LordZordack/DisDorktion/blob/main/docs/measurements/input_section_issue10/source_snapshot.json)
record settings, formulas, JUCE v8.0.14, starting HEAD `8efb31d`, dirty source
hashes and executable hashes. Raw WAVs, repeat artifacts and negative copies
remain local and can be reproduced into a fresh destination.

Both committed version-8 examples rendered successfully through the Release
offline renderer. Overload example: post-gain peak 3.962233066558838, final peak
1.0, overloaded=true. Expressive example: post-gain peak 0.498815566301346,
final peak 0.53733092546463, overloaded=false. Both report zero latency and
48000 frames; monitor mute does not alter the rendered output.

## Graph and source review

GitNexus refreshed to 2509 nodes, 7549 relationships and 94 reported flows.
Implementation-time `detect_changes(scope="all")` returned **all 192 changed symbols** across 39
tracked/intent-to-add files and 42 affected flows, with **CRITICAL** change risk;
the result had neither partial nor truncated flags. The source reviews and
regression matrix cover the changed DSP, harness/records, restore/snapshot,
measurement and benchmark paths. CRITICAL is reported rather than described as
an all-clear. The analyzer separately reported extraction limits: two entry
candidates omitted, 383 callees skipped at branching limits and one walk cut
by budget. Consequently absence from reported flows is not evidence of absence
of runtime effect. No graph change claim substitutes for human GUI/host checks.

## CPU qualification

The Release three-run baseline completed **270/270 qualified cases** (exit 0).
An independent raw-data audit passed all **39,815,902 observations**, recomputing
median/p95/p99/maximum, incoming-block budgets, timing counts and sampled control
coverage. Each case retained 117359-149865 timings. Automation covered all 101
residues and 44 depth/bypass combinations, with minima of 959 and 2835 samples
respectively; dynamic bypass covered all four states with at least 32525 samples
per state. No timer overhead or scheduler outlier was removed.

Worst p99 was **75200 ns / 3.74052% of incoming deadline**, below the recorded
6% budget: run 2, 96 kHz, prepared capacity 64, oversized incoming block 193.
All rows report zero added latency. This qualifies the isolated section,
including controls, dispatch and the interstage scan; shared harness source,
transport, RMS and monitoring costs and the future full chain remain separate.

Observed conditions: Intel Core Ultra 7 155H, 33752997888 bytes RAM, Windows
10.0.26300, MSVC 19.51 Release x64, SDK 10.0.26100.0, JUCE v8.0.14, build
`0.1.0-8efb31d-Release`. Every baseline case observed AC before and after timing;
the power scheme was Balanced (`381b4222-f694-41f0-9685-ff5bb260df2e`). Benchmark
thread affinity was group 0 / mask 0x1 / efficiency class 1, with previous mask
0x3fffff. No competing builds/tests ran. Thermal sensing was unavailable and
ambient temperature unconfirmed; the result does not establish thermal stability.

The separate unpinned diagnostic completed **90/90 cases**, with **13,349,159
observations**, and exited 1 as expected. Its independent audit passed counts,
raw percentiles, coverage and budgets; worst p99 was **6300 ns / 1.89% of
deadline** (96 kHz / 32 / automation). Every row remains intentionally
unqualified because it was unpinned. The first launch was not executed because
automatic approval review timed out; the permitted retry launched successfully.

Summary CSVs, audit JSONs and dirty-source/executable provenance are retained at
`docs/measurements/input_section_cpu_{baseline,diagnostic}_issue10.csv`,
`docs/measurements/input_section_cpu_{baseline,diagnostic}_audit_issue10.json`
and `docs/measurements/input_section_cpu_provenance_issue10.json`. Every raw
timing/control iteration remains in each summary's local `.timings.csv` sidecar.
The audits record SHA256 for both summaries and raw data; baseline raw SHA256 is
`efd0116d6713a7b41400aca2de7a8e7d40204af6089fc0acea7f65eab29588fc`, diagnostic
raw SHA256 is `2e414daf98aa27a5cd564838ef7d0d1ea19d13794fdcbb5ba4fae79d5870758e`.
The independent audit script is retained locally at `.tools/issue-10-cpu-audit.py`.
Smoke data remain deliberately unqualified.

## Listening and remaining manual evidence

On 2026-10-13, the user was asked to audition the generated
48 kHz sine and transient wet files covering gain, crushing, capture-rate
reduction, rapid controls and mute tails. Their observation was: **"I believe
they behave as intended"**. This records user acceptance of those synthetic
fixtures. Representative musical material and actual GUI/device inspection
remain unperformed; no broader listening or manual GUI/host pass is claimed.

## PR preparation verification

Fresh publication checks on 2026-10-13 passed `cmake --preset windows-vs2026`,
Debug and Release builds, and CTest: **191/191** in 50.70 seconds (Debug) and
**191/191** in 7.87 seconds (Release). Independent DSP-only configuration/build
passed, with **160/160** tests in 7.20 seconds. README launch/control entry points
and the repository's GitNexus instructions were reviewed; OKF concepts/indexes
cover the new architecture, settings, persistence, realtime and budget behavior.

The fresh validator command initially failed before validation because a prior
whitespace rewrite had converted its UTF-8 BOM into six mojibake text bytes.
Removing exactly those bytes, without changing the body, restored execution.
Both original and repeated ten-case measurement directories passed again;
the retained corrupted-reference copy still failed exact composition (exit 1).
Validator SHA256 was refreshed in both provenance snapshots. All 22 recorded
source/executable hashes match after the fresh builds; measured DSP/tool binaries
are unchanged, so the recorded CPU distributions remain applicable.

Compact measurement JSON/CSV, CPU summaries/audits and source snapshots accompany
the source changes. Raw WAVs, full timing sidecars, local `.tickets/` notes and
review/build logs remain local. VS2022 CI and manual GUI/device inspection are
not represented as locally passed checks.
