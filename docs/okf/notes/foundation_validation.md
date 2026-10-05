---
type: Note
title: "JUCE foundation validation"
description: "Observed builds, correctness checks and performance measurements for the reference gain foundation."
timestamp: 2026-10-04T00:00:00Z
tags: ["dsp", "validation", "gain", "performance"]
---

# JUCE foundation validation

See the [processing method](../methods/processing_contract.md), [project](../projects/disdorktion.md), [build instructions](https://github.com/LordZordack/DisDorktion/blob/main/docs/BUILDING.md) and [performance procedure](https://github.com/LordZordack/DisDorktion/blob/main/docs/PERFORMANCE.md). Repository documentation links become available on GitHub when this work is published.

## Environment

Windows 11 10.0.26200; Intel Core Ultra 7 155H, 16 cores / 22 logical processors, about 32 GiB memory. Visual Studio Community 18.8.2; MSVC 19.51.36252.0 (tools 14.51.36231); SDK 10.0.26100.0; CMake 4.3.1-msvc1. Local dependencies were GitHub source archives addressed by the pinned JUCE and Catch2 commit SHA values in `cmake/Dependencies.cmake`.

## Correctness and build observations

The first sandbox configure failed to detect MSVC and left empty compiler flags in its cache. Reconfiguring with `--fresh` and approved compiler access restored `/EHsc`, Debug `/Od` and Release `/O2`. No result from the poisoned cache is qualification evidence.

Seven DSP test cases failed against deliberately incomplete stubs. Follow-up adapter regression tests reproduced surplus-channel silence and stale control targets after state restoration/reset; fixes process the active channel view and snapshot host controls during reset.

| Local configuration | Build | CTest result |
| --- | --- | --- |
| `windows-vs2026-debug` | All targets, including VST3 and Standalone | 16/16 passed, 0 failures |
| `windows-vs2026-release` | All targets, including VST3 and Standalone | 16/16 passed, 0 failures |
| `windows-vs2026-dsp-only-release` | DSP, tests and benchmark; no adapter/plugin | 10/10 passed, 0 failures |

Commands were the build/test presets in `docs/BUILDING.md`, with `--parallel 4` for builds and `--output-on-failure --no-tests=error` for CTest. Full build configuration used local pinned source archives through the documented overrides. Debug and Release VST3 binaries and Standalone executables were checked at their documented artifact paths. Test reports and raw command logs are retained locally under `.tools/issue-2/`; CI retains reports and artifacts on execution.

Allocation tracking replaces global C++ scalar/array, aligned, sized and nothrow operators only in the realtime test executable. Its deliberate allocation self-test and nested-scope test verify instrumentation; assertions run after tracking stops. This covers replaceable C++ operators on the current thread, not arbitrary C allocation, custom allocators or other threads. Source inspection of the prepared dispatcher/gain/adapter processing path found scalar arithmetic, non-owning views, smoothing and cached lock-free atomic reads, with no allocation, locks, logging, file access or UI calls. State parsing/serialization and editor construction are outside that path.

## External review disposition

Antigravity performed supplied-context reviews of the build/allocation tests and of the DSP/adapter. Its raw output is retained locally under `.tools/issue-2/`. Accepted: explicit CI artifact checks, test timeouts, nested allocation counts, surplus-channel handling and restored controls on reset. Tested improvements are above.

Rejected or corrected against source/build evidence: CMake 4.2 is released; pinned JUCE exports `JUCE_MODULES_DIR`; public header paths are explicitly exported despite private module source linkage; successful builds contradict the asserted header/link failures. Bounded dispatch is the documented oversized-block policy. DSP tests omitted from the supplied review context already cover ramps, zero blocks, partition equivalence and boundary preparation. JUCE's APVTS converts and snaps `AudioParameterBool` to legal raw values; fractional normalized host automation passes the regression test. Lifecycle serialization is required for the entire DSP state, so changing only the ready flag to an atomic would not permit concurrent preparation. State restoration must not mutate the module from a potentially concurrent nonprocessing thread; reset and callback transport provide the safe synchronization points.

## Performance observations

Budgets remain those established before module implementation: raw p99 at most 1% of the incoming block deadline for reference gain, with 25% reserved for a future wet chain. The user selected battery diagnostics and deferred AC-powered qualification.

The first unpinned Release/x64 run exercised all 40 combinations of 48/96 kHz, prepared blocks 32/64/127/256/1024 and steady gain / automation / bypass / oversized dispatch. Each case warmed for 2 seconds and retained a ten-second measurement window. Thirty-five distributions had 100,000–200,000 samples; five exhausted capacity and are explicitly marked truncated. Original rows are preserved in `docs/measurements/reference_gain_battery_diagnostic.csv` ([published CSV](https://github.com/LordZordack/DisDorktion/blob/main/docs/measurements/reference_gain_battery_diagnostic.csv)).

Calibration initially aimed near the 200,000 upper bound and lacked margin for observed throughput drift. Before retries, the method was revised in `docs/PERFORMANCE.md` and the benchmark: add a two-second untimed probe and choose a fixed integer stride closest to 150,000 predicted samples. Compile-time stride oracles passed in Debug, Release and DSP-only builds. A revised quick smoke returned 1 with explicit unqualified status; invalid and conflicting selectors returned 2.

Only the five truncated cases were rerun, each using `--diagnostic --case-id RATE/BLOCK/SCENARIO` and a separate output file, then combined in `docs/measurements/reference_gain_battery_retries.csv` ([published retry CSV](https://github.com/LordZordack/DisDorktion/blob/main/docs/measurements/reference_gain_battery_retries.csv)). They recorded 139,704–153,091 samples per full ten-second window. Together with the 35 complete original cases, all 40 combinations now have complete distributions. Failed original rows were not erased or silently treated as valid.

| Scenario | Complete cases | Observed raw p99 range | Largest fraction of block deadline |
| --- | --- | --- | --- |
| Steady gain | 10 | 200–2,900 ns | 0.0600% |
| Gain automation | 10 | 200–4,500 ns | 0.0600% |
| Bypass transitions | 10 | 200–3,100 ns | 0.0750% |
| Oversized dispatch | 10 | 400–9,000 ns | 0.0499% |

Percentages above are calculated from raw p99 and the incoming block deadline, not from the CSV's rounded fraction field. No timer overhead was subtracted. Per-case median, p95, p99, maximum, timer-only median, counts, power/topology and conditions are in the CSVs. These observed timings are below the proposed numeric reference threshold, but **do not qualify it**: this was one unpinned battery run plus targeted retries, rather than three AC-powered P-core runs. The future wet-chain budget remains a proposal with no implementation or measurement.

The native observations matched Intel Core Ultra 7 155H, 22 logical processors, 33,752,997,888 bytes physical RAM, Windows 10.0.26200, SDK 10.0.26100.0 and MSVC 1951 / full 195136252 Release x64. Windows reported hybrid efficiency classes and affinity mask `0x3fffff`; diagnostics were unpinned. Power scheme GUID was `{381b4222-f694-41f0-9685-ff5bb260df2e}`. Battery readings at process startup were 71% for the first run and 63% for retries. Builds had completed; background system activity was uncontrolled and thermal sensor measurement was unavailable. No scheduling or thermal outliers were removed. Qualification remains pending on AC with the prescribed affinity, power/thermal conditions and three-run matrix.

## Remaining qualification

Hosted VS2022 CI execution and manual Standalone listening / release VST3 host qualification have not run in this local implementation session. F2 adds audition sources; M1 provides the production gain/listening qualification.

GitNexus CLI refreshed the local source index successfully. Before staging, `detect_changes(scope="all")` covered only tracked documentation. During PR preparation, the 35 intended files were staged and the check was rerun: 225 changed symbols, four affected benchmark flows, medium risk, and no partial/truncated indication. The benchmark argument parsing, timing/percentile and scenario-control flows were reviewed alongside the source and earlier diagnostic evidence. Newly indexed DSP/adapter relationships and correctness were reviewed separately; an empty static caller set does not prove host callbacks are unused. The MCP freshness metadata lagged the CLI, so unresolved caller results were corroborated by source searches. Fresh PR verification repeated the full Debug/Release and independent DSP-only configure/build/test presets with the same passing counts above.
