# Performance procedure and proposed budgets

These are proposed engineering limits for the gain foundation, not measured results or acceptance evidence for the future complete wet chain. Document the budgets before implementing the reference module. Allocate performance shares for additional modules and integration before implementing them.

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

One full battery diagnostic and five targeted sampling retries are recorded in the [validation note](okf/notes/foundation_validation.md), with the [original CSV](measurements/reference_gain_battery_diagnostic.csv) and [retry CSV](measurements/reference_gain_battery_retries.csv). All 40 cases have complete distributions after replacing the five truncated distributions with their documented retries. AC-powered reference qualification remains pending; these diagnostics do not establish a performance pass.
