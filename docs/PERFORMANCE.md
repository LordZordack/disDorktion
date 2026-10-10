# Performance procedure and proposed budgets

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

## Reference machine

Record this observed reference machine with each result: Intel Core Ultra 7 155H, 16 cores / 22 logical processors, approximately 32 GiB RAM, Windows 11 Home 10.0.26200. Also record compiler, SDK, power mode, AC power status, and benchmark conditions.

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
