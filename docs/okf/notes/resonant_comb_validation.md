---
type: Note
title: "Resonant comb validation"
description: "Issue 17 verification record, measurement provenance, and explicit remaining listening and CPU gates."
timestamp: 2026-11-08T00:00:00Z
tags: ["comb", "validation", "measurement"]
resource: "https://github.com/LordZordack/DisDorktion/issues/17"
---

## Historical evidence scope after issue #38

The results below describe the recorded pre-issue-38 implementation and test
conditions. They do not qualify the revised Driver routing, section mixes,
nonlinear/filter Off endpoints, inactive history handling, defaults or v5-only
plugin state. Standalone module/harness controls may retain their own bypasses.
See the [current contract and focused evidence](./issue38_validation.md) for
what has actually been checked; no new CPU or listening result is inferred.

## Earlier validation record

# Resonant comb validation

The [design](../methods/resonant_comb.md) defines acceptance limits before
implementation. Related: [project](../projects/disdorktion.md) and
[audition method](../methods/audition_measurement.md).

Antigravity reviewed the supplied plan and approved with amendments; the
design records their adoption and qualifications. The raw review is retained
locally in `.tickets/issue-17-antigravity-review.md` (ticket notes are not published).
Human listening and manual GUI
appearance acceptance remain open; generated audio does not establish either.

## Verification recorded on 2026-11-01

- Full `cmake --build --preset windows-vs2026-release` completed successfully,
  including VST3, standalone and audition targets.
- `ctest --preset windows-vs2026-release --output-on-failure --no-tests=error -j 2`:
  270/270 passed (15.35 s).
- Debug audition app and all six test executables built using
  `cmake --build --preset windows-vs2026-debug --target DisDorktionAudition disdorktion_dsp_tests disdorktion_realtime_tests disdorktion_harness_tests disdorktion_harness_realtime_tests disdorktion_adapter_tests disdorktion_audition_tests`.
  Debug CTest: 270/270 passed (54.10 s).
- After the final offline metadata assertion and renderer rebuild, Release
  harness CTest passed 86/86 (3.98 s); the Debug offline case passed
  1,016 assertions. Final renders contain the asserted policy metadata.
- Focused Release DSP: 8 cases, 1,316,407 assertions passed, including warm
  bypass/history round-trip. Allocation test: one case, five assertions passed.
- Focused harness: four cases, 51,830 assertions passed; realtime harness:
  two cases, nine assertions passed, including concurrent scalar publication.
- Python validator regressions cover corruption, source identity, path escape,
  nonfinite data, matrix completeness, false reference claims, fractional
  transfer, residual truncation and large-delay decay-pole overflow. Final
  count is 13/13 passed. The large-delay overflow regression exercises
  384 kHz / 40 ms / feedback 0.95 with damping off and 20 Hz damping.

## Independent numerical artifacts

Capture: `docs/measurements/resonant_comb_issue17_final/`; audit:
`docs/measurements/resonant_comb_issue17_final_audit.json`.
Run with `disdorktion_comb_measurements.exe NEW_DIRECTORY`, then
`scripts/validate_comb_measurements.py NEW_DIRECTORY --executable EXE --output NEW_AUDIT_JSON`.
All 42 static impulse/complex-response cases and six automation cases passed.
Worst impulse recurrence error was 2.978284e-8; worst automation error was
5.946505e-8. Maximum computed omitted-tail l1 bound was below 1e-9.
Irregular-block repeat captures were bit-identical. Maximum reported static
unit-impulse -120 dB tail among measured cases was 12.925920 s. Latency was zero.
Artifacts include raw float captures, settings/events and source/executable
SHA-256 identities. Numerical acceptance does not imply human sound acceptance.
See `docs/measurements/README-resonant-comb-issue17.md` for artifact publication scope:
compact manifests, summaries and audits are published; raw captures remain local.

The test matrix covers 8/44.1/48/96/192/384 kHz, mono/stereo,
1/32/64/127/256/1024 capacities, zero/oversized blocks, settings endpoints,
finite extremes, deterministic reset, invalid reprepare and block invariance.
Linear interpolation and deliberate pitch glides remain documented behavior.

## CPU evidence

The initial unpinned quick Release diagnostic completed 80/80 scenarios and
passed independent raw auditing. Worst p99 was 0.423307% of the incoming
deadline (96 kHz, capacity 127, neutral), below the proposed 5% ceiling.
This is not reference acceptance: windows are short and affinity is unpinned.
The quick capture predates a validator-only log-domain overflow correction;
final evidence must be recaptured against the final source identity.

Reference CPU qualification completed all 240 cases across three pinned AC
Release runs. Independent raw auditing passed and confirmed
`reference_qualified: true`, complete matrix, sample/control/settled-bypass
coverage, power observations, raw statistics and source/executable identity.
The audit recomputed 34,826,506 raw timings. Worst p99 was 700 ns, 0.21% of the
incoming deadline, at 96 kHz / capacity 32 / automation in the third run
(zero-based run 2, case 204), below the strict 5% ceiling. Raw maximum in that
case was 406,500 ns; scheduler outliers are retained and not subtracted.

Evidence: `docs/measurements/resonant_comb_cpu_issue17_reference/` and
`docs/measurements/resonant_comb_cpu_issue17_reference_audit.json`. Command:
`disdorktion_comb_benchmark.exe --runs 3 --conditions "AC-powered Intel Core Ultra 7 155H; observed performance-core affinity; Windows Balanced scheme; no competing builds/tests; OS background services uncontrolled; thermal sensor unavailable and ambient unmeasured" --output NEW_DIRECTORY`.
Audit with the shared `validate_comb_measurements.py` command from BUILDING.

Observed Intel Core Ultra 7 155H, 32 GB RAM, Windows 10.0.26300, SDK
10.0.26100.0, selected group 0/mask 0x1/efficiency class 1, AC power and unchanged
Balanced scheme. Quiet-decay traces cover the first 2 s at callback resolution
and sparse observations through 120 s of audio, with hashes/order/finite-value
and duration checks. CPU timing is isolated module control+processing cost;
source generation, GUI and monitoring are outside the timed interval.
Thermal and ambient conditions were unmeasured; normal OS background services
were uncontrolled. Qualification applies to this observed machine/build and
does not establish manual listening or production-chain acceptance.

## Listening fixtures and scope

`comb-impulse.json`, `comb-metallic.json`, and `comb-notches.json` under
`examples/experiments/` reproduce the listening settings. Each renders stereo
at 48 kHz, capacity 256, for 20 s with a 1 s generated source and explicit
trailing silence. The metallic example uses 7.375 ms delay, feedback 0.9,
feedforward 0.5, 4500 Hz damping and mix 0.6. The notch example uses 5 ms,
zero feedback, feedforward -1 and full wet with damping off.
All remain audition candidates without human sound approval. Production-chain
and plugin parameter integration is deferred to X5.

Final listening renders are in `docs/measurements/` directories
`resonant_comb_listening_issue17_{metallic,impulse,notches}_final`.
Each renderer command exited zero and wrote 960,000 frames, zero latency,
`renderPolicy: fixed-duration-uncompensated` and `tailFlushed: false`.

## Review and graph scope

Independent task reviews covered DSP/contract, harness/records/UI transport and
measurement/audit logic. Fixed review findings include settled bypass CPU
coverage and quiet-trace auditing. Final tracked-diff GitNexus analysis reported
41 changed symbols across 21 tracked files and 24 affected indexed flows,
with CRITICAL aggregate risk from shared UI/save/load/prepare paths. The result
was neither partial nor truncated. The analyzer's flow discovery itself is
budget-limited; missing flows are not proof of absence. New untracked files
were reviewed and tested directly; this is not a publication/commit gate for
files that have not yet been staged. No commit was created during that initial review.

## Publication verification

For PR preparation, configured `windows-vs2026`, built the full Release preset
and the Debug audition/test targets, and reran both CTest presets: 270/270
passed in each configuration. The 13 Python validator tests and OKF validation
passed. The numerical raw-data audit was repeated successfully for all 48 cases.
Staged graph analysis covered all 44 publication paths and 226 changed symbols,
48 affected indexed flows, with CRITICAL aggregate risk and no partial/truncated
result. Local ticket notes, raw captures, build artifacts and unrelated work
are excluded from publication. The repeated CPU raw-data audit passed all 240
cases with `reference_qualified: true` and zero budget failures.
Manual listening/GUI acceptance remains pending.
