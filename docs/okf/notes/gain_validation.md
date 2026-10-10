---
type: Note
title: "Reusable gain validation"
description: "Observed correctness, integration, transition fixtures and battery diagnostics for the reusable input/output gain module."
timestamp: 2026-10-10T00:00:00Z
tags: ["gain", "dsp", "validation", "performance", "audition"]
resource: "https://github.com/LordZordack/disDorktion/issues/4"
---

# Reusable gain validation

The [gain design](../methods/gain.md) was approved before implementation. Related: [processing contract](../methods/processing_contract.md), [audition method](../methods/audition_measurement.md), and [project](../projects/disdorktion.md). Correctness is verified by the checks below. Listening, manual host qualification, and AC CPU qualification remain pending.

## Build and correctness evidence

| Configuration | Build | CTest |
| --- | --- | --- |
| `windows-vs2026-debug` | All targets, including VST3, Standalone, audition, render, benchmarks and transition generator | 71/71 passed |
| `windows-vs2026-release` | All targets | 71/71 passed |
| `windows-vs2026-dsp-only-release` | Independent build without plugin/device adapter | 49/49 passed |

Commands: `cmake --build --preset windows-vs2026-debug --parallel 4`, the corresponding Release preset, and `cmake --preset windows-vs2026-dsp-only` followed by its Release build with `--parallel 2`. Each build was followed by `ctest --preset PRESET --output-on-failure --no-tests=error --output-junit REPORT`. Local reports: `build/windows-vs2026/test-results-issue4-debug.xml`, `build/windows-vs2026/test-results-issue4-release.xml`, and `build/windows-vs2026-dsp-only/test-results-issue4-release.xml`. Full builds used the existing configured pinned dependency archives. MSBuild emitted a nonfatal `CMAKE_GENERATOR_PLATFORM` warning in a helper target; builds and tests returned zero.

Test-first checks reproduced missing production APIs, the old two-parameter plugin layout, and rejection failures for out-of-range source values that rounded inside float bounds. The final implementation checks source numeric bounds before narrowing.

Seven focused production DSP tests passed 2,485,298 assertions in Debug. Independent double analytic oracles cover conversion, every ramp frame, mono/stereo at 44.1/48/88.2/96/192 kHz with blocks 1/32/64/127/256/1024, 8/384 kHz boundary ramps, empty/oversized/variable blocks, rejected controls/layouts, reset/reprepare, interrupted simultaneous gain/bypass ramps, gain changes while muted/bypassed, all mute/bypass combinations, and independently updated/reset input/output instances. Settled unity/bypass are sample-identical; mute is exact zero. Equal event positions preserve sample identity across partitions.

Adapter tests cover all eight parameter IDs/order/ranges, version-1 import and version-2 persistence, legacy gains including zero and quiet positive values, independent production controls, malformed states, source bounds, underflow rejection, raw controls/parameter values/attachments, reset and allocation-free processing. Host normalized-float transport retains its existing precision limits; this does not promise exact round trips for the tiniest positive subnormal legacy gains. Independent control atomics are not a whole-state transaction.

Harness tests compare production samples with direct DSP, preserve version-1 reference records, exercise strict version-2 dB/mute records, offline pre-monitor samples, device restoration, and separate module/monitor mute. Review found that the UI slider's 0.1 step could quantize restored fractional controls; continuous sliders now retain both production and quiet reference values. A manual UI restore/save exercise remains pending.

Realtime tests track current-thread replaceable C++ allocations/deallocations across prepared core, plugin, harness and device paths; these checks passed with zero counts. They do not instrument arbitrary C allocation, custom allocators or other threads. Source inspection found cached lock-free atomic loads, scalar arithmetic and prepared smoothing/dispatch, with no allocation, blocking, file/log access or UI calls in these processing paths. Persistence and target restoration stay outside callback ownership.

## Offline render and transition evidence

Debug `disdorktion_render.exe` rendered `examples/experiments/gain-sine.json` and `gain-mute.json`, returning zero. Both are 48 kHz stereo, 48,000 frames, block capacity 256, generated 440 Hz sine amplitude 0.25. Input peak 0.25 and RMS 0.176776694939678. At +6 dB output peak was 0.498815566301346 and RMS 0.352715869574818; explicit mute produced exact zero peak/RMS. Both reported zero latency and no overload. Monitor mute was true and did not change rendered samples. Result records are preserved in the [measurement directory](https://github.com/LordZordack/DisDorktion/tree/main/docs/measurements).

`disdorktion_gain_transitions.exe --output-dir docs/measurements/gain_transition_fixtures` generated two original synthetic 32-bit float stereo WAVs, each four seconds at 48 kHz with capacity 127 and exact event-boundary splits. No external recording or third-party license is involved. The [settings/provenance record](https://github.com/LordZordack/DisDorktion/blob/main/docs/measurements/gain_transition_fixtures/settings.json) includes formulas, event frames and measured peaks. Each source has amplitude 0.025; output peak is 0.396223306655884, finite and below unity, with zero latency. A second render matched sample-for-sample. Reusing the destination was rejected with exit 2 without altering the directory. The 3 MB of generated WAVs remain local; the generator and settings are published so readers can reproduce them into a fresh directory.

| Time (seconds) | dB | Mute | Bypass |
| --- | --- | --- | --- |
| 0.0 | 0 | false | false |
| 0.5 | +24 | false | false |
| 1.0 | -60 | false | false |
| 1.5 | -60 | true | false |
| 2.0 | +24 | false | false |
| 2.5 | +24 | false | true |
| 3.0 | +24 | false | false |
| 3.5 | -12 | false | false |

Listen to the generated `gain-sine.wav` and `gain-transients.wav`, recording playback device and observations at the event times. The user intends to provide observations; none have been received yet. Numerical envelopes and finite fixtures do not establish subjective click acceptance or recorded musical-material acceptance.

## CPU and environment

The user explicitly selected battery diagnostics and left AC qualification pending. Builds/tests completed before benchmarking. The diagnostic set selects all five scenarios (steady +24 dB, full-range automation, bypass, oversized and mute) at 48/96 kHz, prepared capacity 32, for one production instance, two production instances, and the legacy trim plus pair. Oversized callbacks contain 97 frames. This is 30 selected cases, not the full block-size matrix. One additional single-instance 48 kHz/32 steady case uses subnormal +/-1e-40 input.

Each ordinary invocation uses `--stages N --diagnostic --case-id RATE/32/SCENARIO --conditions "Battery diagnostic requested by user; builds/tests complete; unpinned; background activity uncontrolled; thermal sensors unavailable" --output FILE`. The tiny-input invocation adds `--tiny-input`. Timing includes a scoped denormal guard, including destruction; dynamic scenarios also include parameter updates. It excludes host parameter reads, meters and other adapter work. Warmup and untimed calibration are two seconds each, followed by a ten-second window. Raw timings include timer overhead; no overhead or outliers are subtracted.

The [ordinary diagnostic CSV](https://github.com/LordZordack/DisDorktion/blob/main/docs/measurements/gain_battery_diagnostic.csv) preserves all 30 rows; 28 have complete 100,000–200,000-sample distributions and 2 exhausted capacity. Failed rows are retained, excluded from the summary below, and not retried in this battery session. All invocations returned the expected exit 1 (unqualified), including the subnormal diagnostic. This is one unpinned battery run of selected cases, not an AC performance pass.

| DSP stages | Complete cases / selected | Raw p99 range | Largest block deadline fraction |
| --- | --- | --- | --- |
| 1 | 8 / 10 | 200–400 ns | 0.0900% |
| 2 | 10 / 10 | 300–800 ns | 0.1200% |
| 3 | 10 / 10 | 400–1100 ns | 0.1500% |

Failed rows: stage 1 48000/32/steady (200000 samples); stage 1 48000/32/automation (200000 samples).

The [subnormal diagnostic CSV](https://github.com/LordZordack/DisDorktion/blob/main/docs/measurements/gain_subnormal_diagnostic.csv) reports 149491 samples, raw p99 200 ns, and reason `tiny-input diagnostic; reference qualification unavailable`. It remains unqualified independently of the numeric timing.

Complete ordinary distributions contain 141,898–166,879 samples. Recorded startup battery readings span 36–41%. These p99 values include timer overhead; maxima and timer-only medians remain in the raw rows.

Sampling-capacity failures cannot support a performance conclusion. Windows observations at the first case: Intel Core Ultra 7 155H, 22 logical processors, 33,752,997,888 bytes RAM, Windows 10.0.26300, SDK 10.0.26100.0, MSVC 1951/full 195136252 Release x64; Balanced power scheme `{381b4222-f694-41f0-9685-ff5bb260df2e}`, battery 41%, unpinned affinity. Each CSV row records actual process-start conditions and hybrid topology. Background activity is uncontrolled; thermal sensors are unavailable.

The proposed 1%/2%/3% stage budgets require the full 48/96 kHz and 32/64/127/256/1024 matrix, all five scenarios, three AC-powered P-core-pinned runs. Battery diagnostics cannot qualify these budgets.

## Review and outstanding gates

Antigravity's supplied-context design review was assessed against pinned JUCE source. Accepted architecture/state clarification, separate compatibility ownership, double envelopes, explicit concurrency guarantees and denormal diagnostics. Rejected a blanket switch to bare parameter setters because pinned APVTS uses notifications to synchronize listeners/raw controls. Its original verdict was revise-before-implementation; the approved revisions are recorded in the local ticket. Independent core and integration source reviews found no remaining actionable defect after the slider correction; their verdict does not replace host, listening or CPU measurements.

Remaining: human listening observations, representative licensed musical material, manual audition restoration and VST3 host reload/automation, hosted CI evidence, and the prescribed AC CPU qualification. No audible-click or qualified CPU pass is claimed. The issue's listening/performance acceptance gates remain open.

Antigravity also reviewed the implemented core and plugin adapter from supplied source and recorded checks, returning **APPROVED**, with no concrete bug identified. This is a source review, not independent test execution, harness/benchmark coverage, or proof about every possible allocation path. Its informational concurrency, lifecycle and verification limitations are already recorded above. Deferred optional dirty-control caching: the module already avoids repeated conversion and no adapter measurement establishes a need for added publication state. Rejected signed-zero normalization: C++ compares +0.0 and -0.0 equal, so the existing equality guard already avoids that conversion; the proposed mutation also conflicts with a const settings reference. State restoration still requires the documented lifecycle synchronization; being on a message thread alone does not provide it. Raw reviews/dispositions are retained in the local issue ticket.

At implementation handoff, the GitNexus index refresh succeeded: 1,234 nodes, 3,304 edges and 34 flows. `detect_changes(scope="all")` reported 193 changed symbols in 42 tracked/intent-to-add files affecting 25 flows, with **critical** change risk; all 193 symbols were returned, with no partial/truncated flag. New code was exposed through intent-to-add entries without staging its contents. This rating is recorded without treating it as a clean all-clear. Reviewed contexts and source for UI restore/snapshot, experiment load/validation, harness target selection/reset, plugin prepare/process/reset control application, and benchmark argument/timing flows against the independent reviews and passing regression checks. Manual UI/host and CPU gates remain open. New documentation/measurement files are validated separately by the OKF checker and result inspection.

## Pull request preparation

Fresh full configure/build verification repeated Debug and Release 71/71 and independent DSP-only Release 49/49, all with zero failures. Reports are `build/windows-vs2026/test-results-issue4-pr-debug.xml`, `build/windows-vs2026/test-results-issue4-pr-release.xml`, and `build/windows-vs2026-dsp-only/test-results-issue4-pr-release.xml`. The corrected Release transition command `--output-dir .tools/issue4-validation/pr-transition-fixtures` returned zero with the same finite peaks and deterministic rerender check. README, build/use guides, affected OKF concepts, indexes and log were reviewed; repository guidance and entry points remain applicable. No formatter/linter is configured in the checkout. Hosted VS2022 and external qualification remain pending; local VS2026 checks do not establish those results.
