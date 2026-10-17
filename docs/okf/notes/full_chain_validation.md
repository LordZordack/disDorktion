---
type: Note
title: "Full chain qualification"
description: "Repeatable integration checks and remaining listening, performance and host acceptance gates."
timestamp: 2026-10-17T12:00:00Z
tags: ["audio-dsp", "integration", "validation"]
resource: "https://github.com/LordZordack/disDorktion/issues/13"
---

# Full chain qualification

The [design](../methods/full_chain.md) uses unchanged production sections,
delayed pre-drive dry, global mixing and final gain. Antigravity reviewed the
supplied plan context; its recommendations clarified envelope algebra, complex
phase checks, callback control cadence and legacy restore latency. The review
did not inspect repository source. Existing FIR/ADAA delay compensation was
confirmed in source before retaining it.

## Reproduction and evidence scope

Build using the Windows presets in `docs/BUILDING.md`. The deterministic tool
exports 272 cases at 48/96 kHz in mono/stereo, four source families and all 64
effect bypass masks. Each binary capture retains source, dry, wet, three blend
weights, ordered section reference, common bypass, variable partitions,
changed-drive dry and final gain. The independent Python validator checks
source regeneration, file identities, completeness, finite samples, exact
delayed dry, blending, ordered output, partitioning and final gain. Complex
projections describe phase-sensitive coloration, without assuming flat blends.

```powershell
./build/windows-vs2026/Release/disdorktion_full_chain_measurements.exe NEW_DIRECTORY
& 'C:/Users/jonas/AppData/Roaming/uv/python/cpython-3.12-windows-x86_64-none/python.exe' scripts/validate_full_chain_measurements.py NEW_DIRECTORY --executable ./build/windows-vs2026/Release/disdorktion_full_chain_measurements.exe --output NEW_AUDIT_JSON
./build/windows-vs2026/Release/disdorktion_full_chain_benchmark.exe --diagnostic --quick --conditions 'Record actual conditions' --output NEW_CPU_DIRECTORY
```

The CPU executable is explicitly diagnostic and returns 1 even when its
processing and 25% p99 budget flags pass. It covers 120 cases: 48/96 kHz,
mono/stereo, capacities 32/64/127/256/1024, neutral/extreme/automation/bypass/
oversized/tail. Controls, delay, mixing, final gain and peaks are timed.
Raw timings and schedules are retained and independently audited by the same
validator. Timer overhead is reported without subtraction. See
the performance procedure in `docs/PERFORMANCE.md`.

## Recorded software checks

Final VS2026 Debug and Release builds succeeded, including VST3, plugin
standalone and audition targets. Their final suites each passed **239/239**;
DSP-only Release passed **197/197**. The independent validator's seven tests
passed, including corrupted capture, executable identity, incomplete matrix,
nonfinite metadata, path escape, false reference qualification and incorrect
reported wet latency rejection.

Both fresh numerical runs completed and passed independent audits. Manifests
and capture SHA256 hashes matched exactly. Worst errors were:

| Check | Measured error |
| --- | ---: |
| Delayed dry, ordered sections, common bypass | 0 |
| Partitioning and input-drive independence | 0 |
| Linear blend | 2.3841858e-7 |
| Final +6 dB gain | 2.0538210e-7 |
| Regenerated source | 1.1913105e-7 |

Independent signed impulse centroids and DC group delay were
91.9999301–91.9999302 samples, against 92 reported samples and a 0.02-sample
limit, at 48/96 kHz mono/stereo. Peak across deliberate overload captures was
3.99999976. These are integration/headroom measurements, without a limiter.

Gentle, destructive and phase-blend version-11 examples each rendered 145024
frames with 92 samples of retained leading latency. Their output peaks were
0.24412546, 0.08670585 and 0.12506855 respectively, with no observed overload.
See `docs/measurements/full_chain_issue13_summary.json` and the artifact guide
`docs/measurements/README-full-chain-issue13.md` for exact report locations.

An independent final source review found and corrected stale audio replay
on full-chain/legacy mode transitions and the old zero-tail host declaration.
New regressions cover both. The final review reported no remaining actionable
findings. Publication graph analysis used an identical staged-source mirror:
3100 nodes, 10427 edges and 106 sampled flows. It covered all 252 listed changed
symbols across 55 files and 44 affected sampled processes, with no partial or
truncated change-analysis result. CRITICAL shared GUI/record/plugin flow risk
was reviewed against source and passing regressions. Process enumeration has
entry/branch limits; this is not exhaustive execution-flow coverage.

## Remaining acceptance

The complete quick Release CPU diagnostic passed **120/120** cases.
Independent auditing checked **120000 raw timings**, controls, percentiles,
deadline calculations and source/executable hashes. Publication-refresh p99 cost was **6.15%**
of incoming deadline, below 25%. An external launch helper verified highest
efficiency class 1, group 0 process mask 0x1, HIGH priority and AC before/after.
The run used 20 ms warmup and 1000 observations per case. This is diagnostic
evidence with thermal conditions unmeasured and no reference qualification.

The initial unpinned default run was stopped after p99 failures and slow
collection; its partial data are excluded from acceptance. Pinning and priority
changed together, so the improved result does not isolate either factor.
No DSP algorithms or acceptance thresholds were changed in response.

Human matched-level listening, visual GUI inspection, actual DAW compensation
and reference CPU qualification are separate gates. Numerical captures and
software tests cannot supply a sound verdict. Preserve the prior
[input](./input_section_validation.md), [middle](./middle_section_validation.md)
and [output](./output_section_validation.md) limitations, including extreme
residual-alias acceptance. No algorithm retuning is assumed by this integration.

For listening, use the Full chain audition target with monitor initially muted
at -18 dB. Compare global mix 0/.25/.5/.75/1, common bypass, individual stage
bypasses, moderate and high drive, wide/narrow/crossed filters, and transient
or music loops. Record source identity, exact settings, monitor/matched level,
sample rate and observations in version-11 experiments. Intermediate mix may
change phase and perceived loudness; internal overload indicators can remain
active while dry is audible.
