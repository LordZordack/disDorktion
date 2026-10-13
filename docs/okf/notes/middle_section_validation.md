---
type: Note
title: "Middle section qualification evidence"
description: "S2 candidate integration verification and remaining human listening, alias-policy and host timing gates."
timestamp: 2026-10-13T00:00:00Z
tags: ["audio-dsp", "integration", "validation", "latency"]
resource: "https://github.com/LordZordack/disDorktion/issues/11"
---

# Middle section qualification evidence

The [section design](../methods/middle_section.md) integrates the fixed
4x FIR + ADAA candidate evaluated by [Q1](./middle_oversampling_validation.md).
**Production acceptance remains open:** candidate integration was authorized;
human listening, extreme residual-alias tradeoff and host deadline timing have
not been accepted by this authorization.

## Plan review

Antigravity reviewed the supplied plan and source excerpts, with no repository
tools or tests. The local deliverable is `.tools/issue-11-agy-plan-review.txt`.
Adopted suggestions strengthen four-state small-signal delay checks, atomic
publication/thread affinity, strict version-9 rejection, reference tolerances,
explicit render semantics and Release-only CPU checks. Source inspection
confirmed that ADAA bypass already preserves its 1.5 internal frames through
dry averaging and compensation. No DSP delay fix was warranted.

The proposed automatic `N + latency + tail` render extension was not adopted:
the shared recorder has an exact-duration paired-stream contract. Version-9
metadata makes its uncompensated lead-in and unflushed tail explicit, with
tests for direct-stream equality and exact duration. Aligned listening renders
remain available through the separate Q1 tool.

## Baseline and current checks

Before implementation, the existing VS2026 Release CTest suite passed 191/191
cases in 13.67 seconds. A new select-target test failed before implementation
because `middle-section` was not selectable, then passed after integration.

Final VS2026 configuration and full Debug/Release builds succeeded, including
the audition app, renderer, plugin VST3 and Standalone targets. Final CTest:

| Configuration | Cases passed | Time |
| --- | ---: | ---: |
| Full Debug | 202/202 | 13.72 s |
| Full Release | 202/202 | 19.65 s |
| Independent DSP-only Release | 169/169 | 13.90 s |

New qualification tests cover all four small-signal bypass states across five
rates and six capacities, with impulse peak 92 and phase/group-delay checks.
Changing partitions, zero blocks, interrupted ramps, endpoint settings and
reset remain deterministic within 1e-6. Harness/core comparisons are exact
across the same rate/capacity matrix and retained/common bypass states. The
existing Q1 scalar correction reference independently orders the modules and
rate wrappers and compares exact samples. Realtime allocation/deallocation
checks pass; concurrent scalar publication is a smoke test, not a race-detector
proof. Additional exact oracles verify middle/isolated target state isolation
and edited live controls surviving 96 kHz device reprepare and common bypass
release.

Version-9 schema tests include required fields, unsupported identity, foreign
schemas, invalid booleans/numbers, pre-float-cast range rejection and destination
preservation on failure. Legacy records retain their targets. Render tests
verify exact stream equality, monitor independence, all bypass states and
uncompensated fixed duration. New CLI mild/stress examples render 49024 frames
each; independent RIFF/float/header/frame/finite checks pass for all four WAVs.
Their final 32 frames are silent, confirming that the explicit 1024-frame
trailing-silence interval captures ring-down. Source/binary hashes accompany
the generated local artifacts.

## Fresh quality and CPU evidence

`docs/measurements/middle_adaa_issue11/` and its `_repeat` each contain 900
spectral cases, 15 filter grids, 144 CPU cases and 90 matched listening WAVs.
The independent validator passes both artifact sets. All 93 deterministic
files (spectral/filter summaries and WAVs) match the repeat byte for byte.
The 4x filter gate and representative alias-improvement gate pass: minimum
relative improvement is about 20.204 dB. Filter limits through 0.4 Fs reach
0.03196 dB magnitude deviation, 0.000652 degrees residual phase and 0.015965 dB
half-blend deviation; measured impulse peaks and reported latency are 92.
Stress off-harmonic dBc spans approximately -27.26 to +15.53, and does not
establish the -60 dBc goal. Fundamental suppression and alias/harmonic collisions
remain limitations of the documented metric.

**The provisional local CPU gate fails in both fresh runs.** On Intel Core
Ultra 7 155H, MSVC 19.51.36252, Release build `0.1.0-acea5f6-Release`, worst 4x
p95 batch means are 12.4945% and 22.3268% of block duration, above the unchanged
10% limit. The first worst case is 96 kHz/mono/32/cascade; the repeat worst is
96 kHz/stereo/512/cascade. These are uncontrolled local batch timings, not
callback deadline tails. Q1's historical optimized runs passed at 6.30%/6.01%;
that history does not override these failures. No DSP algorithm was changed,
and no cause is inferred from these timing differences. Performance acceptance
requires further profiling/measurement; this change is a functional integration
candidate, not a qualified production section.

## Review and graph audit

Independent static review found no blocking source defect and confirmed the
meaningful restart/isolation oracles added in response to review. It ran no
tests. Manual GUI interaction and physical layout were not verified.

The installed CLI refresh stalled before useful progress. GitNexus's shared
analysis orchestrator successfully refreshed an isolated checkout containing
the complete intended change set, preserving the real checkout's staging.
All-changes and compare-main audits each list 30 files, 97/97 changed symbols
and 27/27 affected processes; aggregate risk is **CRITICAL** because shared GUI,
load/save, render and preparation flows are affected. Listing counts match;
this MCP version supplies no explicit partial/truncated flags. The analyzer
warns that 10 candidate entry points, 418 callee branches and one budget-limited
walk were omitted from process extraction. Thus graph coverage is not an
exhaustive proof of absence. Keyword/FTS indexing was explicitly skipped;
structured graph analysis completed. The main workspace index still needs a
fresh publication audit before a later commit.

## Remaining acceptance gates

The implementation may merge as a qualification candidate while issue #11
remains open. Merging does not approve the failed CPU gate, residual-alias
tradeoff, or sound quality. A compact [measurement summary](https://github.com/LordZordack/DisDorktion/blob/main/docs/measurements/middle_section_issue11_summary.json)
is committed for future reference; raw WAVs, timing/spectral artifacts and
source snapshots named above remain local generated evidence.

- Matched-level listening across mild/extreme drives, bypasses and transitions,
  with source identity/license and actual human observations recorded.
- Explicit acceptance or revision of the extreme residual-alias policy. Q1's
  representative relative gate passing does not establish the -60 dBc goal.
- Production callback deadline timing and later C1 host latency/dry mixing.
- CPU qualification: both fresh local runs exceed the unchanged 10% budget.
- VS2022 hosted CI and manual GUI interaction evidence when available.

Generated recordings and local measurement outputs are reproducible evidence;
they do not supply a human verdict. [Native tanh](./tanh_saturation_validation.md)
and [folding](./wave_folder_validation.md) transfer functions remain unchanged.

## Recommended performance follow-up

1. Establish three comparable Release runs on AC power, with the benchmark
   thread pinned to an identified performance core. Record power/thermal
   conditions and processor topology; keep competing builds/indexing stopped.
   Measure individual callback percentiles alongside batch means, following
   [the performance procedure](https://github.com/LordZordack/DisDorktion/blob/main/docs/PERFORMANCE.md).
2. Re-profile the actual expensive stages. Q1's earlier diagnostic attribution
   was about 40% oversampling, 30% ADAA and 21% correction FIR; these historical
   proportions need confirmation under the new controlled baseline.
3. If the budget still fails, investigate vectorization of the correction FIR
   and repeated arithmetic in ADAA. Retain quality/latency limits and re-run
   numerical equivalence, bypass/transition, spectra and phase/blend checks.
4. Evaluate shorter correction/oversampling filters or lower processing rates
   only as explicit quality tradeoffs with renewed aliasing and listening
   evidence. A filter redesign may require matching integer delay to preserve
   the 92-sample contract.
5. Qualify the complete chain and host callback behavior before release.
   Continue independent output-section work (#12); carry these unresolved
   gates into full-chain work (#13). The 10% allocation is an engineering
   budget, not measured whole-system CPU usage or proof of missed deadlines.
