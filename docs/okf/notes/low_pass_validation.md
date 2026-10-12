---
type: Note
title: "Low-Pass Module Validation"
description: "Numerical, realtime and diagnostic CPU evidence for selectable output filtering; human listening and reference CPU qualification remain pending."
timestamp: 2026-10-12T12:58:10Z
tags: ["filter", "validation", "realtime", "low-pass"]
---

# Low-Pass validation

The independently selectable module implements [its reviewed design](../methods/low_pass_filter.md) with Butterworth, Chebyshev I (1 dB ripple) and Chebyshev II (40 dB stopband). Ideal filtering was explicitly excluded. Antigravity reviewed the plan before implementation; its supported feedback added digital BLT oracles, long impulses, unified mode fades, target snapping and mode/schema coverage. A bounded implementation review identified a missing CPU budget check; the tool now enforces that diagnostic limit separately from spectral limits.

## Measured evidence

VS2026 Release build succeeded, including VST3/Standalone and the audition application. The complete Release suite passed **154/154**; focused Debug filter/DSP/harness/realtime/device checks passed **14/14**. Debug checks use assertions. All new filter tests are tagged `[cutoff-filter]`. Source and test commands are reproducible with the repository's existing CMake presets.

| Measurement | Result | Established limit |
| --- | ---: | ---: |
| Maximum impulse magnitude error | 5.38524447e-08 | < 3e-5 |
| Maximum stepped-sine response error | 2.60531387e-07 dB | < 0.05 dB |
| Maximum half-power magnitude error | 3.67190971e-07 dB | < 0.02 dB |
| Minimum Type II transmission-zero depth | 149.125046 dB | ≥ 80 dB |
| Maximum Type II endpoint stopband-floor error | 3.05581087e-05 dB | < 0.05 dB |
| Worst diagnostic p99 / callback deadline | 0.570000% | < 5% |
| Reported added latency | 0 samples | 0 samples |

The impulse suite independently generates complex analog prototype poles/zeros and direct-form BLT biquads, checking both sample responses (max error < 3e-6) and complex frequency responses (< 3e-5). Separate polynomial magnitude expectations use tangent frequency mapping. Impulses contain 131072 frames; rates include 8/44.1/48/96/192/384 kHz, cutoffs 20/1000/20000 Hz (clamped where required), and all three modes. Measurements also include Type I extrema/endpoint gain and Type II zeros/floor. The stepped sine sweep covers 125–16000 Hz at 48 kHz and 1 kHz cutoff, with one second settling plus one second least-squares sinusoidal projection at each frequency.

Contract/stability tests cover mono/stereo at 44.1/48/96/192 kHz and 32/64/128/256/512/1024 frames; single-frame, empty, mismatched and oversized blocks; rejected settings/preparations; retained cutoff after rate changes; exact silence/reset; channel/instance state independence; warm full bypass; interrupted mode fades; moving-control block partition; 10 ms geometric ramp against an independent JUCE TPT instance; and finite rapid endpoint automation below peak 8 for nominal unit-peak sine input. Impulse tails at 20 Hz/384 kHz fall below 1e-6 after two seconds. A concurrent producer/callback stress check exercises atomic live controls; it is not a formal race-detector proof. Allocation tracking reports zero allocations/deallocations during module, harness and device callback control changes/reset/processing.

## Artifacts and reproduction

The PR includes compact spectral/CPU summaries, source hashes and render metadata. Large raw timing CSVs and generated WAVs remain local; regenerate them with the measurement command below. The README documents building and launching the Release audition app, which includes these filter controls.

- Measurement result (`docs/measurements/cutoff_filter_verified/result.json`)
- Impulse response bins (`docs/measurements/cutoff_filter_verified/spectra.csv`)
- CPU summaries (`docs/measurements/cutoff_filter_verified/cpu_summary.csv`) and all raw timings (`docs/measurements/cutoff_filter_verified/cpu_raw.csv`)
- Source identity manifest (`docs/measurements/cutoff_filter_verified/source_manifest.json`)
- Butterworth sweep render metadata (`docs/measurements/cutoff_filter_verified/low-pass-butterworth/result.json`), Chebyshev I (`docs/measurements/cutoff_filter_verified/low-pass-chebyshev-1/result.json`), Chebyshev II (`docs/measurements/cutoff_filter_verified/low-pass-chebyshev-2/result.json`)
- Standalone sweep experiment (`examples/experiments/low-pass-sweep.json`)

```powershell
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-release --parallel 4
ctest --preset windows-vs2026-release --output-on-failure
cmake --build --preset windows-vs2026-debug --target disdorktion_dsp_tests disdorktion_realtime_tests disdorktion_harness_tests disdorktion_harness_realtime_tests disdorktion_audition_tests --parallel 4
ctest --preset windows-vs2026-debug -R "cutoff|Cutoff|High pass|Low pass|response modes|Interrupted response|Slow cutoff|Filter targets|Filter harness|Filter device|Filter CPU|Filter live" --output-on-failure
./build/windows-vs2026/Release/disdorktion_cutoff_filter_measurements.exe NEW_DIRECTORY
```

The build identity names base revision c1326a3; this evidence was generated from the uncommitted issue-8 implementation. The source manifest disambiguates that working tree. The initial diagnostic run in `cutoff_filter_validation` preceded explicit CPU enforcement; `cutoff_filter_verified` is the reviewed evidence set. Numeric spectra and source/wet WAVs match across these two runs byte-for-byte. Timing distributions naturally differ.

## Listening and remaining gates

Each response has a five-second 20 Hz–20 kHz logarithmic sweep, 48 kHz stereo, block 256, peak 0.25, cutoff 1 kHz, bypass off. Exact source, cutoff, mode and monitoring metadata accompany each render. Listen at monitor −18 dB, compare dry/bypass and wet response, then repeat with transient and representative music material using live source files. **Human listening observations are pending**; generated fixtures do not constitute listening results. A true ideal response is not offered.

CPU diagnostics ran on Windows 11, GenuineIntel, 22 logical CPUs, Release, with no simultaneous builds/tests. Each module has 144 cases: four rates, six capacities, mono/stereo, and settled/endpoint-mode-automation/full-bypass scenarios. Each case warms up 256 callbacks and retains 1000 timings; all three banks run in every scenario. The corrected tool passed the <5% p99 budget. Affinity, power and thermal conditions were not qualified, so this is **not reference-hardware release qualification**. Follow the performance procedure (`docs/PERFORMANCE.md`) for longer repeated qualification runs. IIR phase/group delay remains frequency-dependent despite zero added latency.

GUI compilation and callback restoration checks passed; visual GUI inspection, VS2022/CI coverage, output-section/plugin integration, human listening and reference-hardware CPU qualification remain open. See the [project record](../projects/disdorktion.md).
