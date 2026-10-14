# Performance procedure and proposed budgets

## Output section budget (S3)

Issue #12 allocates **10% of the incoming callback deadline** to native-rate
high-pass -> low-pass DSP, including parameter application, section dispatch
and the inter-stage peak scan. This combines the existing 5% filter shares.
Shared source generation, GUI transport, monitoring and meters remain outside
the isolated timing. The future complete wet-chain ceiling remains 25%.
Controls update once per callback; both filters advance their internal 10 ms
ramps per sample and retain three warm banks even during bypass. Use the
existing two-second warmup, adaptive sampling and three-run pinned AC procedure.
Record diagnostic and reference qualification separately; mono diagnostics
supplement the established stereo reference matrix.

`disdorktion_output_benchmark` has 20 scenarios over 48/96 kHz and capacities
32/64/127/256/1024: wide, narrow, crossed, nine fixed response pairs,
simultaneous automation, dynamic stage bypass, four fixed bypass states,
retained common bypass, and oversized callbacks. Automation updates both
cutoffs and response selections once per callback. Qualification requires at
least 100 timed observations for each of 101 cutoff residues and 36 response /
stage-bypass combinations; dynamic bypass and common bypass require coverage
of all four and eight respective states. The raw sidecar retains actual controls.
Source/executable SHA256 identities accompany every summary row.

```powershell
& './build/windows-vs2026/Release/disdorktion_output_benchmark.exe' --diagnostic --quick --channels 2 --conditions 'Record actual diagnostic conditions; no competing builds/tests' --output output_section_diagnostic.csv
& './build/windows-vs2026/Release/disdorktion_output_benchmark.exe' --diagnostic --quick --channels 1 --conditions 'Record actual mono diagnostic conditions; no competing builds/tests' --output output_section_mono_diagnostic.csv
& './build/windows-vs2026/Release/disdorktion_output_benchmark.exe' --runs 3 --conditions 'Record actual AC/power/thermal conditions; no competing builds/tests' --output output_section_reference.csv
```

All destinations must be new. Quick, mono and filtered runs are unqualified
and deliberately return exit 1 even when their separate `processing_ok` and
`raw_p99_within_budget` flags pass. Reference qualification uses three unfiltered
stereo Release runs, observed performance-core affinity, before/after AC checks,
full adaptive timing windows and coverage. The 600-case reference matrix takes
at least 2 hours 20 minutes and can generate several GB of raw observations.
Do not describe quick CPU results as a reference-hardware acceptance verdict.

Issue-12 stereo and mono quick diagnostics completed all 200 cases each.
Independent raw audits recomputed 12,405,637 stereo and 16,344,051 mono timings;
all diagnostic CPU/stability gates passed, with worst p99 deadline shares
1.67626943% and 0.66% respectively. Capacity-1024 automation cutoff coverage was
below the reference minimum of 100 observations per setting. These results
remain unqualified. See [output qualification](okf/notes/output_section_validation.md).

```powershell
uv run --no-project --python 3.12 scripts/validate_output_cpu.py './docs/measurements/output_section_cpu_issue12_diagnostic.csv' --executable './build/windows-vs2026/Release/disdorktion_output_benchmark.exe' --channels 2 --output NEW_AUDIT_JSON
```

Issue-12 reference qualification completed **600/600 cases** across three
pinned AC stereo Release runs. Independent reference-mode auditing recomputed
**89,408,001 raw timings** and verified environment, provenance, exact schedules
and control coverage. Worst p99 was **0.87% of incoming deadline** at
96 kHz / capacity 32 / high-pass bypass, 2900 ns (strict 10% limit).
Thermal and ambient conditions were unmeasured; normal OS background services
were uncontrolled. See the
[reference CSV](measurements/output_section_cpu_issue12_reference.csv),
[audit](measurements/output_section_cpu_issue12_reference_audit.json) and
[artifact scope](measurements/README-output-section-issue12.md).

```powershell
uv run --no-project --python 3.12 scripts/validate_output_cpu.py NEW_REFERENCE_CSV --executable './build/windows-vs2026/Release/disdorktion_output_benchmark.exe' --reference --runs 3 --channels 2 --output NEW_REFERENCE_AUDIT_JSON
uv run --no-project --python 3.12 python tests/test_output_cpu_validator.py -v
```

For mono, select its summary CSV and `--channels 1`. The auditor recomputes
raw statistics and control coverage, verifies the exact event schedule and
source/executable hashes, and records its own unchanged source identity.

## Input section budget (S1)

Before implementation, issue #10 allocated **6% of the incoming block deadline**
to native-rate Gain -> BitCrusher section DSP: 1% gain + 5% crusher. This includes
control application, section dispatch and the post-gain peak scan. Shared
harness source generation, atomic transport, input/output RMS and monitoring
are outside this isolated DSP timing; the future complete wet-chain ceiling
remains 25%. Acceptance evidence is recorded in
[input section qualification](okf/notes/input_section_validation.md).

Qualify Release x64 stereo at 48/96 kHz and capacities 32/64/127/256/1024 with
steady neutral, expressive worst settings, simultaneous automation, all four
child bypass states, gain mute and oversized callbacks. Follow the existing
equal-wall-time adaptive collector: 2-second warmup, 2-second probe, 10-120 second
window, 100000-200000 timings, all 101 automation residues with at least 100
observations each, and explicit depth/bypass sampled coverage. Require three
AC-powered performance-core-pinned runs with recorded environment. Keep raw
distributions and timer overhead. Run after builds/tests finish; quick and
unpinned runs are deliberately unqualified.

`disdorktion_input_benchmark` reuses the timing infrastructure with input-only
workload and coverage checks. Expressive cases use an interior input amplitude
so gain saturation does not make fractional powers artificially cheap, and
capture at host rate minus 1 Hz to exercise jitter near every frame. The dynamic
`bypass` case cycles all four child bypass states every eight callbacks and
requires 100 sampled observations in each state; fixed bypass states remain
separate cases. Power is checked before and after each case outside timing.
The fresh summary CSV has a `.timings.csv` sidecar containing every retained
elapsed time and its control iteration. Neither raw scheduler outliers nor
timer overhead are subtracted from the p99 acceptance statistic.

```powershell
./build/windows-vs2026/Release/disdorktion_input_benchmark.exe --runs 3 --conditions "Record actual power/thermal conditions; no competing builds/tests" --output input_section_cpu_baseline.csv
./build/windows-vs2026/Release/disdorktion_input_benchmark.exe --diagnostic --conditions "Record actual power/thermal conditions; unpinned diagnostic" --output input_section_cpu_diagnostic.csv
```

There are 90 cases per run: two rates, five capacities and nine scenarios.
Preserve source/executable hashes alongside a starting Git revision when the
worktree is uncommitted; a Git revision alone does not identify the measured
local implementation. Thermal and ambient limitations must remain explicit.

Issue-10 baseline: **270/270 cases qualified** across three pinned AC runs.
Independent auditing of 39,815,902 raw observations confirmed the percentiles,
budgets and control coverage. Worst p99 was **3.74052% of incoming deadline**
(6% limit). The separate 90-case unpinned diagnostic had a worst p99 of 1.89%
and remains unqualified. See the [baseline CSV](measurements/input_section_cpu_baseline_issue10.csv),
[baseline audit](measurements/input_section_cpu_baseline_audit_issue10.json)
and [qualification note](okf/notes/input_section_validation.md) for conditions,
provenance, raw-data hashes and manual evidence limits.

## Output filter measurements

`disdorktion_cutoff_filter_measurements` generates HP/LP impulse response and
stepped-sine comparisons, six five-second sweep WAV pairs, and CPU timing data:

```powershell
./build/windows-vs2026/Release/disdorktion_cutoff_filter_measurements.exe cutoff-filter-measurements
```

The destination must be new. Each module's diagnostic p99 budget is strictly
below 5% of the callback deadline. Both filters process three warm response
banks, even in bypass. Timing covers 44.1/48/96/192 kHz, 32/64/128/256/512/1024
frames, mono/stereo, settled, endpoint cutoff/mode automation, and full bypass:
288 cases total, 256 warmup callbacks and 1000 stored timings per case.
`cpu_raw.csv` retains every timing; `cpu_summary.csv` reports median/p99/max and
deadline fraction. `result.json` separates spectral success, diagnostic CPU
budget success and reference-hardware qualification. The command fails if
either numeric check fails or cases are incomplete. Each module reports zero
latency samples; frequency-dependent phase delay is not reported as added latency.

These short, unpinned runs establish diagnostics only. Reference-hardware
qualification still requires the longer warmup/sampling, recorded power and
thermal conditions, affinity and repeated runs in this document. Run timings
without concurrent builds/tests. Evidence and limitations are recorded in
[high-pass validation](okf/notes/high_pass_validation.md) and
[low-pass validation](okf/notes/low_pass_validation.md).

## Production gain measurements

`disdorktion_gain_benchmark.exe` compiles the shared timing harness against the
production module. `--stages 1` measures one instance (1% budget), `--stages 2`
the input/output pair (2%), and `--stages 3` the legacy trim plus pair (3%).
These are DSP path measurements; they exclude host parameter loads, meters,
and the rest of `PluginProcessor`. The complete wet-chain proposed ceiling
remains 25% of deadline. The original reference benchmark retains its four
scenarios and CSV format.

Production scenarios add `mute` to steady/automation/bypass/oversized. Steady
and oversized use +24 dB; automation spans -60 to +24 dB; mute/bypass switch at
every eighth callback. Both production instances receive the same worst-range
settings in pair measurements; the compatibility trim stays at 4x. CSV adds
target, stage count, zero latency, and input profile. `--tiny-input` exercises
subnormal +/-1e-40 inputs with scoped denormal suppression, always unqualified.
Debug builds, short windows, filtered cases, and diagnostics cannot qualify.

```powershell
./build/windows-vs2026/Release/disdorktion_gain_benchmark.exe --stages 1 --runs 3 --conditions "AC power; recorded power/thermal conditions; no competing builds" --output gain_single_baseline.csv
./build/windows-vs2026/Release/disdorktion_gain_benchmark.exe --stages 2 --runs 3 --conditions "AC power; recorded power/thermal conditions; no competing builds" --output gain_pair_baseline.csv
./build/windows-vs2026/Release/disdorktion_gain_benchmark.exe --stages 3 --runs 3 --conditions "AC power; recorded power/thermal conditions; no competing builds" --output gain_compatibility_baseline.csv
./build/windows-vs2026/Release/disdorktion_gain_benchmark.exe --stages 1 --diagnostic --tiny-input --case-id 48000/32/steady --conditions "Recorded power conditions; subnormal input diagnostic" --output gain_subnormal_diagnostic.csv
```

Use the warmup/calibration/full-window/reference procedure below for production
measurements. Run no benchmarks during builds/tests. Preserve failures and raw
distributions. See the [production gain validation note](okf/notes/gain_validation.md) for actual coverage;
commands and proposed limits do not constitute qualification.

These are proposed engineering limits for the gain foundation, not measured results or acceptance evidence for the future complete wet chain. Document the budgets before implementing the reference module. Allocate performance shares for additional modules and integration before implementing them.

## Bit crusher measurements

`disdorktion_bit_crusher_measurements.exe` writes deterministic source/wet WAVs
and `result.json` into a fresh output directory. It exercises 48 and 96 kHz
spectral checks. The destination must not already exist or contain files.

```powershell
./build/windows-vs2026/Release/disdorktion_bit_crusher_measurements.exe bit-crusher-measurements
```

`disdorktion_bit_crusher_benchmark.exe` uses the same timing criteria and
5% deadline budget, with a crusher-specific adaptive window to keep
low-throughput cases inside the sampling-count bounds.
Its steady-64/full, automation cycling all 11 depths at 100 Hz/full,
bypass-8-at-12345.5-Hz, and oversized-64/full cases use a 5% deadline budget.
For qualification, run three
AC-powered, CPU-pinned runs with recorded conditions:

```powershell
./build/windows-vs2026/Release/disdorktion_bit_crusher_benchmark.exe --runs 3 --conditions "AC power; recorded power/thermal conditions; no competing builds" --output bit_crusher_baseline.csv
```

For each case, retain the 2-second warmup and 2-second probe. Project the
10-second timing count, then choose a window of
`max(10, min(120, 1,500,000 / projectedCountAt10Seconds))` seconds. Scale the
projected count for that window, then select a fixed stride targeting 150,000
observations. The stored count must remain within 100,000–200,000; the 120
second cap still applies. Keep all raw values and do not discard timing
outliers. This adaptive window applies only to the bit crusher; the gain
collector retains its existing 10-second window.

Use `--diagnostic` for an unqualified unpinned diagnostic, `--quick` only for
a smoke check, and `--case-id 48000/32/steady` to filter a retry. The qualified
[baseline](measurements/bit_crusher_baseline.csv) passed all 120 rows (40 cases
per run, three runs) for the legacy crusher algorithm; it does not qualify the
expressive controls. The [unpinned diagnostic](measurements/bit_crusher_unpinned_diagnostic.csv)
is intentionally unqualified. Preserve both CSVs as legacy evidence. New
mix/crush/shape/jitter correctness tests and renders pass. The first
expressive baseline attempt is preserved but unqualified because its 48 kHz /
1024-frame oversized case collected only 47,099 timings, below the 100,000
minimum. Its p99 was 278,800 ns versus a 3,201,041.667 ns budget, so this was a
sampling-coverage shortfall, not a timing-budget failure. The reviewed adaptive
collector qualified all 120 rows across three runs. Worst p99 was 1.390866% of
the block deadline, below the 5% limit. See the [expressive baseline](measurements/bit_crusher_expressive_adaptive_baseline.csv)
and the separate, intentionally unqualified [unpinned diagnostic](measurements/bit_crusher_expressive_unpinned_diagnostic.csv). See the [bit crusher validation note](okf/notes/bit_crusher_validation.md).

## Tanh saturation measurements

The proposed module p99 budget is 5% of the incoming block deadline for
stereo at 48/96 kHz and blocks 32/64/127/256/1024. The native-rate module reports
zero latency. Steady/oversized use maximum drive and trim; automation traverses
the drive/trim ranges; bypass switches every eighth callback. For tanh retry
runs, use the adaptive 10–120 second window and 150,000 equal-wall-time slots.
Record at most one callback per slot and skip missed slots without catch-up.
For automation, cover all 101 control residues with at least 100 samples each;
report the minimum and maximum residue counts in the CSV to expose gaps or
periodic sampling alias. Keep the 100,000 overall minimum sample count and 5%
p99 budget. This coverage revision does not change gain or bit-crusher collector
behavior. Run after builds/tests finish:

```powershell
./build/windows-vs2026/Release/disdorktion_tanh_saturation_measurements.exe tanh-measurements
./build/windows-vs2026/Release/disdorktion_tanh_saturation_benchmark.exe --runs 3 --conditions "AC power; balanced profile; thermal sensor unavailable; no competing builds" --output tanh_saturation_cpu_baseline_revision2.csv
./build/windows-vs2026/Release/disdorktion_tanh_saturation_benchmark.exe --diagnostic --conditions "AC power; balanced profile; thermal sensor unavailable; unpinned diagnostic; no competing builds" --output tanh_saturation_cpu_diagnostic_revision2.csv
```

The measurement tool requires a fresh destination and writes generated
source/wet float WAVs, JSON and CSV spectra. Native-rate folded components
are observations for Q1; they do not establish anti-aliasing quality. Preserve
failures and raw CSVs. The [validation note](okf/notes/tanh_saturation_validation.md)
records actual test, spectral, CPU, listening and qualification status.

The first three-run baseline attempt is preserved but incomplete at 62
rows. In run 2, the 48 kHz/127-frame automation case exhausted its 200,000-sample
storage cap as callback throughput recovered from probe-time slowdown. The
warmup/probe projections were 4,215,554/2,732,234 callbacks with stride 18. Its
3,500 ns p99 is below the 132,292 ns deadline budget, but the truncated
distribution does not qualify. The revised collector uses one sample per
equal-wall-time slot and does not catch up after missed slots; the 100,000
minimum and 5% budget remain unchanged. See the preserved
[partial baseline CSV](measurements/tanh_saturation_cpu_baseline.csv) and the
[method revision](okf/methods/tanh_saturation.md#tanh-collector-revision-2-and-qualification).
Revision 2 qualified all 120 rows across three pinned runs; automation covered
all 101 control residues in every case with at least 987 samples per residue.
Worst p99 was 0.377952756% of the block deadline. See the [qualified baseline
CSV](measurements/tanh_saturation_cpu_baseline_revision2.csv). AC power and
Balanced profile were reported; thermal sensing and ambient temperature remain
unavailable or unconfirmed. The separate full unpinned revision-2 diagnostic
completed all 40 rows and exited 1 as expected because every row was marked
unpinned/reference qualification unavailable; it had no additional errors or
budget exceedances. Its worst p99 was 0.48% of deadline, and it remains
intentionally unqualified. See the [diagnostic CSV](measurements/tanh_saturation_cpu_diagnostic_revision2.csv).
An earlier diagnostic's out-of-range float endpoint was a separate
benchmark-fixture calculation error; the corrected conversion and interrupted
diagnostic are not qualification evidence.

## Reference machine

The revision-2 tanh baseline recorded an Intel Core Ultra 7 155H, 16 cores / 22 logical processors, 33,752,997,888 bytes RAM, Windows 10.0.26300, Windows SDK 10.0.26100, MSVC 19.51 Release x64. It reported AC power (battery 100%), Balanced profile (scheme GUID prefix 381b), and benchmark affinity group 0 / mask 0x1 / efficiency class 1. Thermal sensing was unavailable and ambient temperature unconfirmed. Record compiler, SDK, power mode, AC power status, affinity, and environmental limitations with each result.

## Budgets

In Release/x64, test stereo at 48 and 96 kHz with block sizes 32, 64, 127, 256, and 1024. Reference gain p99 processing time must be at most 1% of `blockSamples / sampleRate`. Reserve at most 25% of that deadline for the future complete wet chain. These are proposed limits, not measured claims or full-chain acceptance.

## Offline benchmark procedure

Preallocate inputs and results. Warm up for 2 seconds, then collect at least 10 seconds of timings per case. Exercise steady gain, continuous parameter changes, bypass transitions, and oversized dispatch as separate cases. Keep timer calls, fixtures, percentile sorting, and result output outside module processing. Consume output so processing cannot be eliminated. Report median, p95, p99, maximum, timer-only overhead, deadline fraction, configuration, and observed hardware.

Repeat three runs on AC power with a recorded power profile and no competing builds. All three p99 results must meet the reference budget. Hosted CI runs correctness tests; CPU budget qualification runs on the reference machine. Record failures without silently relaxing budgets.

For comparable primary measurements on the hybrid CPU, use observed Windows processor topology to select a performance-core logical processor. Pin only the benchmark thread to it and record the selected processor, affinity, power profile, and topology evidence. Restore the original affinity afterward. Also report an unpinned diagnostic run, keeping its results separate from the pinned baseline. If topology or affinity cannot be established, record the limitation and leave reference CPU qualification unresolved. Record thermal and power conditions; do not discard scheduler or thermal outliers from the recorded distribution.

## Bounded timing collection

After at least 2 seconds of warmup, run a further untimed 2-second probe using the same callback/fixture work to calibrate a fixed sampling stride for the 10-second window. Choose the integer stride with predicted count closest to 150,000 within the permitted 100,000–200,000 range. Preallocate capacity for 200,000 timings. Record both warmup and probe projections. If actual sampling falls outside the bounds, report the case as unqualified and explicitly revise the method before retrying. Keep input regeneration and sampling decisions outside the timed interval. Automation cases include `setParameters` and processing inside that interval.

Compare the raw timing distribution against the budget. Report timer-only overhead separately; do not subtract it to manufacture a pass. For oversized cases, calculate the deadline using the entire incoming block length.

This calibration revision follows the first battery diagnostic: targeting the 200,000 upper bound left insufficient margin for observed throughput drift. A full ten-second window was retained when storage filled, but its truncated sample distribution is not valid qualification evidence. Preserve those rows, and rerun failed cases using the revised probe and stride. Neither battery nor unpinned diagnostic observations qualify the AC-powered reference baseline.

## Commands

Use the Release executable after correctness builds/tests have completed; no competing builds during measurement:

```powershell
./build/windows-vs2026/Release/disdorktion_reference_gain_benchmark.exe `
  --runs 3 --conditions "AC power; recorded power/thermal conditions; no competing builds" `
  --output reference_gain_baseline.csv
./build/windows-vs2026/Release/disdorktion_reference_gain_benchmark.exe `
  --diagnostic --conditions "Recorded power/thermal conditions; no competing builds" `
  --output reference_gain_diagnostic.csv
```

`--diagnostic` runs once without pinning and always reports unqualified. `--quick` shortens the windows for a smoke check and is never qualification evidence. `--case-id 48000/32/automation` selects one case for a recorded retry; filtered or fewer-than-three-run results cannot qualify the full matrix. Exit 0 means all baseline cases qualified; exit 1 means recorded results are unqualified or exceeded limits; exit 2 is invalid input or output failure. Keep each command's CSV even when exit 1 is expected for diagnostics.

## Observed battery diagnostics

Production gain diagnostics are recorded in [gain validation](okf/notes/gain_validation.md),
the [ordinary CSV](measurements/gain_battery_diagnostic.csv), and the
[subnormal CSV](measurements/gain_subnormal_diagnostic.csv). The selected set
uses 32-frame capacity at 48/96 kHz, all five scenarios and all three stage
counts: 28 complete ordinary distributions and two preserved capacity failures.
It is unpinned battery evidence for a subset, with AC qualification pending.

One full battery diagnostic and five targeted sampling retries are recorded in the [validation note](okf/notes/foundation_validation.md), with the [original CSV](measurements/reference_gain_battery_diagnostic.csv) and [retry CSV](measurements/reference_gain_battery_retries.csv). All 40 cases have complete distributions after replacing the five truncated distributions with their documented retries. AC-powered reference qualification remains pending; these diagnostics do not establish a performance pass.

## Wave-folder baseline

`disdorktion_wave_folder_benchmark` uses the revision-2 equal-wall-time
collector, 150,000 preallocated timing slots and an adaptive 10–120 second
window to obtain 100,000–200,000 observations. Every one of the 101 automation
residues needs at least 100 observations. Qualify three pinned AC runs for
stereo 48/96 kHz, capacities 32/64/127/256/1024, and steady/automation/bypass/
oversized scenarios. Its p99 limit is 5% of the **incoming** block deadline.
Keep builds and fixture generation out of the qualification runs.

```powershell
& '.\build\windows-vs2026\Release\disdorktion_wave_folder_benchmark.exe' `
  --runs 3 --conditions 'Record actual power and thermal conditions; no competing builds' `
  --output '.\docs\measurements\wave_folder_cpu_baseline.csv'
& '.\build\windows-vs2026\Release\disdorktion_wave_folder_benchmark.exe' `
  --diagnostic --output '.\docs\measurements\wave_folder_cpu_diagnostic.csv'
```

`--quick`, `--tiny-input`, `--wide-input` and `--extreme-input` are diagnostics;
the three input options are mutually exclusive. Diagnostic runs intentionally
return 1 and cannot qualify the reference budget. A fresh output path is
required. See [wave-folder evidence](okf/notes/wave_folder_validation.md) for
actual conditions, failures and remaining acceptance gates.

Measured wave-folder baseline: all 120 rows qualified across three pinned AC
runs; worst p99 was 1.01867% of incoming deadline (5% limit). See
[the baseline CSV](measurements/wave_folder_cpu_baseline.csv) and
[recorded conditions](okf/notes/wave_folder_validation.md). Thermal sensing
and ambient conditions were unavailable/unconfirmed. The isolated result does
not qualify the complete wet chain. The separate unpinned diagnostic and human
listening remain distinct gates.
