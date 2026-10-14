---
type: Note
title: "Output Section Qualification"
description: "Implementation and reproducible qualification evidence for high-pass followed by low-pass; manual acceptance remains separate."
timestamp: 2026-10-14T03:46:45Z
tags: ["output-section", "validation", "filter", "realtime"]
---

# Output section qualification

The [design](../methods/output_section.md) composes unchanged high-pass and
low-pass implementations. The shared audition target uses version-10 records
with independent stage settings and retained common bypass. Algorithmic latency
is zero; phase/group delay and tail duration remain frequency-dependent.

Antigravity reviewed the supplied plan without repository tools. Its useful
recommendations added an inverted dynamic-order control, explicit version rules,
step/burst and inter-stage envelope checks, and documented automation cadence.
Its proposed +1 dB Chebyshev I gain was inconsistent with the existing normalized
design, so numeric limits were retained. Legacy records remain their original
targets and versions. The local ticket records the individual review decisions.

## Verification status

Before implementation, the existing VS2026 Release suite passed 202/202.
Full VS2026 Debug and Release builds succeeded, including plugin and audition
targets. Debug and Release suites each passed **221/221**. DSP-only Release
build and suite passed **187/187**. The CPU auditor's nine rejection/boundary
tests passed **9/9**. The final measurement-tool publication
retry also compiled in Debug and DSP-only Release.

Tests cover the inverted dynamic-order negative control, transactional rejected
calls, block partitioning, rates/endpoints, all response pairs and stage bypasses,
common-bypass retention, strict version-10 records and unchanged legacy records,
exact shared rendering, live scalar publication and device reconfiguration.
Allocation tracking found zero allocations/deallocations in the tested module,
harness processing/control/reset and rejected paths. Concurrent producer tests
are stress checks, not a formal race-detector proof.

Both fresh final measurements passed, and the independent validator
`--repeat --self-test` passed. Every artifact hash matched between repeats;
NaN reports, incomplete CSVs and tampered WAVs were rejected. Source and
executable SHA256 identities disambiguate the uncommitted working tree.

| Measurement | Worst result | Limit |
| --- | ---: | ---: |
| Impulse sample error | 2.96141832e-8 | <= 6e-6 |
| Absolute complex response error | 6.11798069e-8 | <= 6e-5 |
| Ordered module sample error | 0 | <= 1e-6 |
| Stepped-sine magnitude error | 6.25921435e-7 dB | <= 0.1 dB |
| Deep-stopband absolute tone error | 3.40308962e-11 | <= 6e-5 |
| Automated output peak | 2.46601415 | < 8 |
| Post-high-pass peak | 2.19452453 | <= 4 |
| Tail peak after two seconds | 0 in recorded cases | < 1e-6 |
| Algorithmic latency | 0 samples | 0 samples |

Measured group delay uses neighboring impulse-DFT projections; an independently
computed BLT reference derivative is exported separately. Delay is masked near
transmission zeros and below -60 dB; maximum defined measured/reference delay
difference was 0.07474 samples. This is diagnostic, with no new group-delay
acceptance limit; the complex-response gate remains the agreed phase-sensitive
criterion.

Final evidence directories are `docs/measurements/output_section_issue12_retry1`
and `docs/measurements/output_section_issue12_repeat`. The first attempted
`output_section_issue12` run completed numerical CSVs but failed Windows native
no-replace publication of fixture 45. It is preserved as incomplete and is
excluded from acceptance. The tool now retries that exact publication error at
most three times, 100 ms apart, only while the destination remains absent.
The complete final runs both exited zero.

Measurement executable SHA256:
`37847d0933fae9f408715f82f7917fe3ce5b8572f65d6db3deed6bdadeae8aef`.

## Reference CPU qualification

The full three-run Release stereo matrix qualified **600/600 cases** and the
benchmark exited zero. Independent reference-mode auditing also exited zero
after checking **89,408,001 raw observations**. Worst p99 was **0.87% of the
incoming callback deadline**: run 1, 96 kHz, capacity 32, high-pass bypass,
2900 ns. The strict limit remains 10%, including controls, section dispatch
and the inter-stage peak scan.

The Intel Core Ultra 7 155H reference machine reported Windows 10.0.26300,
MSVC 19.51 Release x64 and SDK 10.0.26100.0. Every case observed AC before
and after timing, the Balanced power GUID, and performance-core affinity at
group 0 / mask 0x1 / highest observed efficiency class 1. No competing task
builds, tests, measurement, indexing or audit jobs ran during capture. Normal
OS background services were uncontrolled; thermal and ambient conditions were
unmeasured. These limitations remain part of the result.

Each case retained 130841-149805 timings over 10-13.735 seconds, after the full
warmup and calibration. The raw audit reconstructed all 101 HP/LP cutoff
positions and 36 response/bypass combinations, with minima of 1347 and 3266
observations respectively. Dynamic stage bypass covered all four states with
at least 32640 observations; retained/common bypass covered all eight with
at least 14620. Every minimum exceeds the required 100. Percentiles, exact
control schedules, processing flags, power/affinity metadata and unchanged
source/executable/auditor identities were verified without removing outliers.

Published `docs/measurements/output_section_cpu_issue12_reference.csv`
and `docs/measurements/output_section_cpu_issue12_reference_audit.json`
retain provenance. The 7,632,967,153-byte raw sidecar remains local; its hash is
in the audit. See `docs/measurements/README-output-section-issue12.md` for artifact scope and reproduction.
Benchmark executable SHA256:
`db75aafafc69d93d2a6dcd61b831894bfe24f3b2bbfefce31b7ef633044dceca`.

## CPU diagnostics

The complete quick unpinned Release matrix ran separately for stereo and mono
on an Intel Core Ultra 7 155H, Windows, with AC recorded before/after each case,
and no competing builds/tests/measurement/indexing jobs. Thermal conditions
were unmeasured. All **400** case processing and strict raw p99 budget flags
passed. Stereo worst p99 was **1.67626943%** of the incoming deadline at
48 kHz / capacity 64 / oversized, 67400 ns. Mono worst was **0.66%** at
96 kHz / capacity 32 / Chebyshev I -> Chebyshev II, 2200 ns. Limit: strictly
below 10%, including control application, section dispatch and HP peak scan.

Independent `scripts/validate_output_cpu.py` audits exited zero for both
matrices, checking **12,405,637** stereo and **16,344,051** mono raw observations.
They recomputed every percentile, count and coverage summary, verified exact
automation and bypass schedules, preserved common-bypass stage selections,
and checked source/executable/raw hashes. An auditor source check also ensures
its implementation does not change during a run. A direct negative control
accepted four valid events and rejected three forged response/bypass events.

Quick automation at capacity 1024 had cutoff-residue minima 60/61 for stereo
48/96 kHz and 98/87 for mono. All 101 cutoff positions, 36 response/bypass
combinations, four dynamic stage-bypass states and eight retained/common states
were observed. Reference coverage requires at least 100 observations per
setting, along with the full warmup/window, pinned reference environment and
three runs. These quick results are **unqualified**, and each benchmark CLI
intentionally returned exit 1; independent diagnostic CPU/stability gates passed.
The separate full reference result above qualifies the combined 10% section
budget; these quick observations remain diagnostic evidence.

Compact summaries: `docs/measurements/output_section_cpu_issue12_diagnostic.csv`
and `output_section_cpu_issue12_mono_diagnostic.csv`. Final raw audits:
`output_section_cpu_issue12_diagnostic_audit_verified.json` and
`output_section_cpu_issue12_mono_diagnostic_audit_verified.json`. Earlier audits
are preserved; use the verified reports with exact schedule checks. Large
`.timings.csv` sidecars remain local and their hashes are in the verified reports.

All three wide/narrow/crossed example records rendered successfully with
240000 frames, zero added latency and no overload. Wide output peak was
0.25003198, narrow 0.24982403 and crossed 0.00249849 for quarter-scale sweep
input. These levels are measured; they do not establish a human sound verdict.

## Reproduction

```powershell
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-debug --parallel 4
ctest --preset windows-vs2026-debug --output-on-failure --no-tests=error
cmake --build --preset windows-vs2026-release --parallel 4
ctest --preset windows-vs2026-release --output-on-failure --no-tests=error
& './build/windows-vs2026/Release/disdorktion_output_measurements.exe' NEW_DIRECTORY
uv run --no-project --python 3.12 scripts/validate_output_measurements.py NEW_DIRECTORY --repeat REPEAT_DIRECTORY
uv run --no-project --python 3.12 scripts/validate_output_measurements.py './docs/measurements/output_section_issue12_retry1' --repeat './docs/measurements/output_section_issue12_repeat' --self-test
& './build/windows-vs2026/Release/disdorktion_output_benchmark.exe' --runs 3 --channels 2 --conditions 'Record actual AC, power, thermal and competing-work conditions' --output NEW_REFERENCE_CSV
uv run --no-project --python 3.12 scripts/validate_output_cpu.py NEW_REFERENCE_CSV --executable './build/windows-vs2026/Release/disdorktion_output_benchmark.exe' --reference --runs 3 --channels 2 --output NEW_AUDIT_JSON
uv run --no-project --python 3.12 python tests/test_output_cpu_validator.py -v
```

The tool exports 324 independent impulse/complex cases, 1296 ordered-module
bypass cases, 288 stepped-tone cases, 24 automation/stimulus cases, 36 tail
cases and 72 reproducible source/wet listening fixtures. Numeric success,
artifact completeness, source/executable identity and human listening are
distinct evidence. Keep large generated WAVs and raw timing distributions local;
retain compact summaries and provenance with the implementation.

## Listening and remaining acceptance

Fixture sources include a 20 Hz-20 kHz log sweep and quarter-scale impulse at
48 kHz, stereo, three seconds, block 256. Use monitor -18 dB and matched comparisons
with dry/bypass and representative transient/music files. Record source identity,
exact cutoffs/response modes, independent/common bypasses, useful tonal ranges
and limitations for wide, narrow, equal and crossed settings.

The user reported "I believe it works" after implementation. This is functional
feedback without a recorded source, settings or matched-level listening protocol;
it does not supply the structured listening or visual GUI inspection above.
No such verdict is inferred from generated files or builds. The independent
[high-pass](high_pass_validation.md) and
[low-pass](low_pass_validation.md) notes still leave human listening and
their individual 5% reference-machine CPU budgets open. The combined 10%
section qualification does not independently qualify either 5% share. VS2022/CI
and actual host timing
require those environments. Full-chain plugin integration is a later milestone;
this note must not describe the output section as fully accepted while its
manual/dependency gates remain open.

The independent final source review found no remaining important or critical
defects after correcting benchmark CMake linkage, exporting measured group
delay and aligning fixture documentation. Existing graph change analysis
reported critical shared GUI/benchmark flow risk. The publication index was
successfully rebuilt in isolated task storage using explicit Node heap settings:
3336 nodes, 9949 edges and 105 sampled flows. Structured change analysis covered
all 334 listed changed symbols in the 46 source/documentation files staged at
that point and 45 affected sampled processes, with no partial/truncated/error
flags. Critical shared GUI, harness and benchmark paths were reviewed against
the passing tests. CMake, Markdown, JSON fixtures and Catch2 macro bodies also
received source review where the graph supplies no changed symbols. Process
enumeration is bounded by GitNexus's entry/branch limits; this is not exhaustive
flow coverage. Complete change analysis is repeated on the final stage before
publication.
