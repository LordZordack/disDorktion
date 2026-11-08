# Expanded-chain CPU Qualification Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task. Preserve the selected issue checkout and its existing dirty integration.

**Goal:** Resolve the 96 kHz / 32-frame CPU budget failure without altering the sound, latency, state compatibility or qualification threshold.

**Architecture:** Measure the existing callback and its stages before changing DSP. Prefer exact invariant caches; retain all warm processing, smoothing and arithmetic expressions. Treat focused profiles/screens as diagnostics, and keep the full reference qualification authoritative.

**Tech Stack:** C++20, JUCE 8.0.14, VS2026 Release/Debug, CMake/CTest, Python standard-library audit, GitNexus.

## Global constraints

- Work on `feat/issue-20-integrate-experimental-modules`; preserve unrelated working-tree changes.
- Carrier integration is discarded. User listening acceptance is recorded.
- Preserve 92-sample latency, 20-second documented tail allowance, fixed order, parameter IDs/order, presets and state migrations.
- Keep audio processing allocation-free, lock-free and single-owner. Bypassed module histories remain warm.
- Keep the 25% incoming-deadline gate, three pinned AC Release repetitions and 100,000–200,000 reference observations. Never discard failures or subtract overhead to manufacture a pass.
- Run GitNexus impact before symbol edits; warn on HIGH/CRITICAL. UNKNOWN requires source-confirmed callers. Run complete change analysis before any commit; no commit/publication requested here.

## Evidence and hypotheses

- Independently audited reference v2 failed at stereo 96 kHz / 32 frames, warm-bypassed additions: 92.9 us p99, 27.87% versus 83.333 us / 25% allowance; 147,003 observations, AC/Balanced/P-core0x1/Release valid. It stopped after 31 cases.
- An earlier partial attempt measured the same nominal case at 66.9 us; it lacks a complete environment/session report. This suggests variability, not a reason to erase the v2 failure.
- Current stress evidence passed 48 cases across 8–384 kHz with raw-capture audit. Latest audio code has not changed since that evidence.
- Agents found six repeated double divisions per host frame across HP/LP, a settled-coefficient dispersion square root, comb integer wrap modulo, and a 65-tap middle FIR. These are profiling candidates, not measured bottlenecks.
- Plugin control mapping repeatedly validates/copies all controls, but that mapping is outside the failed standalone timed interval. Defer it unless separately measured/requested.
- FullChain setters revalidate and forward unchanged settings; comb damping exp and dispersion tan are recalculated. Their actual contribution is unmeasured.

## Task 1: Focused reproduction and attribution

**Files:** create `tools/ExpandedChainProfile.cpp`; add its target in `CMakeLists.txt`; retain diagnostics under new `docs/measurements/expanded_chain_profile_*` directories.

- [x] Read the saved failed case's complete initial settings; use its exact 0.3*sin(.013*n) stereo fixture, controls, capacity and timed boundary.
- [x] Provide explicitly unqualified full-callback screening at 48/96 kHz, 32 frames, forward/reverse rate order, repeated rounds, optional sustained preconditioning. Use two-second warmup and ten-second distributions, retaining raw samples and all failures.
- [x] Provide separate section attribution through the real Input -> Middle -> Output -> final-gain stages, plus parameter-only measurement. Additional timers are diagnostic, never acceptance evidence.
- [x] Record p50/p95/p99/max, deadline exceedance count, elapsed observation times and thread CPU/cycle totals. Record cycles as cycles, never convert to elapsed time or MHz; Windows explicitly warns against that conversion.
- [x] Record actual affinity, AC/profile and binary/source identities. Run no builds/tests/agent commands while collecting timings.
- [x] Produce deterministic golden full-chain captures before edits, covering settled controls, interrupted ramps, child/common bypass, reset/reprepare/rejection and varied partitions; retain exact bytes/hashes for after-edit comparison.

Primary API references: [GetThreadTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadtimes), [QueryThreadCycleTime](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime).

## Task 2: Smallest measured, sound-preserving optimization

**Candidate files:** `src/dsp/CutoffFilter.h`, `src/dsp/FrequencyDispersion.h`, `src/dsp/ResonantComb.h`, `src/dsp/FullChain.cpp`; corresponding existing module/chain tests plus narrowly scoped new regression tests if needed.

- [ ] Select candidates from measured section/control costs. Start with cutoff normalizer caching if Output cost justifies it: cache by the exact smoothed g and evaluate the original division expression whenever g changes.
- [ ] Cache dispersion normalization by exact actual coefficient only if measured useful. Never stop lattice history updates while bypassed.
- [ ] Consider bounded comb index wrap only after proving the integer range across rates, delay ramps and rejection paths.
- [ ] Consider an unchanged-settings setter path only if control cost is material. Validate invalid snapshots, compare every field explicitly (no memcmp), preserve in-flight ramps and prepare/reset/state semantics.
- [x] If Middle dominates and earlier candidates cannot close the deficit, profile its FIR/ADAA separately. Prefer a lazy prior-primitive cache that preserves exact operations; defer arithmetic reassociation, shorter FIR, approximate tanh or changed oversampling to a separate design.
- [x] Verify exact golden output before accepting a candidate. Reject changes that alter sound or latency; retain failed performance evidence.

## Task 3: Correctness and focused before/after measurement

- [x] Run affected isolated module tests, full-chain composition/automation/state tests, realtime allocation checks and the complete configured Release/Debug suites as appropriate to edited DSP.
- [x] Re-run the 48-case stress matrix and independent raw audit after final DSP changes. Keep the nominal Output tail gate unchanged.
- [x] Run focused pinned screens in both orders and after preconditioning; compare baseline/candidate timing and thread-execution evidence. Confirm the failed scenario has repeatable headroom without selecting only passing runs.
- [x] Obtain a fresh independent code review and fix material findings before the final long run.

## Task 4: Final qualification and documentation

- [x] Freeze source/build identity; run the unchanged 180-case three-repetition reference collector with raw retention and independent audit. Do not run concurrent profiling/build/test jobs.
- [x] Record an actual failure as unqualified; do not silently revise the gate. If the remaining cause is environmental or a larger audio design change, document the concrete blocker and evidence.
- [x] Update the ticket, `docs/PERFORMANCE.md`, affected OKF concepts/log/indexes and run OKF validation. Keep host validation/release-specific gates distinct from offline CPU acceptance.
- [x] Refresh GitNexus as needed and obtain complete, untruncated change analysis before any later commit. No automatic PR, installation or publication.

## Deferred work

Plugin APVTS/control-mapping optimization, UI changes, new presets, carrier features, alternate DSP algorithms, budget relaxation and unrelated cleanup are outside this CPU-fix increment.

## Research record

Three read-only agents investigated DSP hotspots, control updates and qualification validity. Their conclusions agree that the current evidence measures a callback overrun, not a proven bottleneck. Implementation starts with attribution, not a speculative audio rewrite.

## Execution evidence

- Focused tools, frozen baseline sources/binaries and 48 deterministic golden cases are complete. Middle accounts for about 38 of 51 us median under the system-managed process policy; controls contribute about 0.4 us.
- Implemented the exact-argument lazy ADAA primitive cache after impact analysis and independent-oracle baseline tests. Candidate passes 75,384 assertions in eight ADAA cases and all 48 exact-byte golden comparisons. Independent source review found no actionable defects. The median saving is only about 0.5 us; it does not resolve the default-policy failure.
- Same frozen baseline executable at 48/96 kHz: default-policy p50 51.2/50.0 us, p99 107.9/97.4 us; explicit process HighQoS p50 16.1/15.7 us, p99 24.3/23.8 us. Candidate explicit-policy repeats give p99 23.5–24.1 us. These are diagnostic screens, not qualification.
- The reference method is being extended with an explicit `--high-qos` option (`windows-explicit-highqos-v1`), own-process policy capture/readback/restoration and independent policy auditing. This is a declared environment revision, not a DSP performance gain or a relaxation of the 25% deadline gate. Archived default-policy failure remains valid. A completely fresh 180-case matrix is required. Host process policy is never modified by the plugin.
- Detailed method rationale: `.tools/issue20-policy-method-review.md`; cache review: `.tools/issue20-adaa-cache-review.md`. Exact comparison: `docs/measurements/expanded_chain_cpu_adaa_cache_golden_audit.json`.

## Completed outcome

Tasks 1–4 are complete for this CPU/stress increment. Measured control cost did
not justify setter changes; Output coefficient and comb wrap candidates were
deferred. The minimal ADAA cache passed exact-output review but is not credited
with resolving the default-policy failure. A fixed frozen-baseline ABBA screen
confirmed the much larger process-policy sensitivity before final collection.

All 309 Release and 309 Debug tests, 84 Python tests, 48 golden cases and fresh
48-case stress raw audit passed. Both cache and policy method received independent
reviews with no remaining actionable findings. The frozen reference completed
all 180 cases and three repetitions; its independent audit reports complete=true,
reference_qualified=true, policy_ok=true under windows-explicit-highqos-v1.
Worst p99/deadline=0.06490746268656716, below 0.25. The original default-policy
failure remains retained; no universal host or frequency claim is made.

Evidence: `docs/measurements/expanded_chain_issue20_cpu_reference_highqos_v1_audit.json`.
Tracked-change analysis is saved in `.tools/issue20-cache-detect-changes.json`:
138 symbols in 45 changed files and 33 flows, critical overall for the broad
pre-existing integration; no partial/truncated response flags. GitNexus's index
still documents unresolved C++ receivers and capped flow discovery, so this is
not a proof of complete call coverage. Untracked tooling was reviewed separately.
No commit, push, merge or branch cleanup was requested for this increment.
