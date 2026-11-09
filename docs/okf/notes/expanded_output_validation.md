---
type: Note
title: "Expanded Output integration validation"
description: "Issue 20 integration evidence and remaining qualification for comb, dispersion and tone."
timestamp: 2026-11-08T00:00:00Z
tags: ["output-section", "integration", "validation", "compatibility"]
resource: "https://github.com/LordZordack/disDorktion/issues/20"
---

## Historical evidence scope after issue #38

The results below describe the recorded pre-issue-38 implementation and test
conditions. They do not qualify the revised Driver routing, section mixes,
nonlinear/filter Off endpoints, inactive history handling, defaults or v5-only
plugin state. Standalone module/harness controls may retain their own bypasses.
See the [current contract and focused evidence](./issue38_validation.md) for
what has actually been checked; no new CPU or listening result is inferred.

## Earlier validation record

# Expanded Output integration

The approved [Output section](../methods/output_section.md) order is comb ->
frequency dispersion -> tone -> high-pass -> low-pass, at host rate. On
2026-11-07 the user discarded carrier modulation; it is no longer a planned
integration requirement for this ticket. Existing five factory sounds and
the first 30 host parameter identities/order remain; 12 controls append.

Argon reviewed the supplied plan and constraints without repository tools.
Accepted suggestions cover composite validation, transactional preparation,
old-state default overwrite/history clearing, conservative tail reporting,
phase coloration in dry blends, and expanded peak-envelope tests. Its claim
of tone boost at 1 kHz was corrected: the pivot is unity.

## Evidence and scope

Local VS2026 full Release and Debug builds passed. Release CTest passed all
307 tests. Harness functional tests passed 79 cases (1,676,354 assertions),
and harness realtime tests passed 22 cases
(657 assertions), including new schemas 15/16 and enabled-effect rendering.
Plugin layout snapshots were inspected at 1000x680, 1120x740 and 1200x800.
Further final-suite results are recorded in the local issue ticket.
Debug passed 306 cases in the full run; its remaining response matrix passed
in a targeted rerun (104.66 seconds) after the timeout adjustment. All 307
cases therefore passed across the Debug suite and rerun.

Debug exposed omitted aggregate member initialization with lambda defaults;
direct C++20 designated bypass initializers corrected it while retaining the
old two-filter aggregate form. A legacy gain-ramp regression was also caught
and corrected: restore clears full-chain histories without snapping historical
gain-only ramps. Configuration-specific Catch discovery prevents a Release
preset from running the last-built Debug test list. The long independent
response matrix uses a 300-second timeout with its numerical gates unchanged.

The 16-case Output matrix uses 8/48 kHz, signed 0.95 damped/undamped
feedback, 40 ms delay, feedforward 1, full comb/dispersion mix, 20 Hz dispersion,
tone +/-6 dB, extreme cutoff responses, one second resonant excitation and
20 seconds silence. It passed finite/channel-isolation, peak below 512 and
final-window residual below 1e-6 checks. One case measured peak 51.8434525.
The matrix includes enabled feedback damping at 20 Hz and disabled damping. These are
scenario measurements, not a proof of arbitrary automation/input bounds.

The previous 14-second silence trial narrowly failed the 1e-6 threshold
(1.118e-6 at 8 kHz, positive feedback, dark tone). The gate was preserved and
the test aligned to the conservative 20-second host allowance. Serial comb,
dispersion and filter states explain why isolated impulse evidence alone does
not bound a resonantly driven composite tail.

No interstage limiter or normalization is inserted. Comb can amplify input;
dispersion changes crest factor, and tone can boost shelves. The isolated
dispersion conservative finite-input proof is documented for input <=16;
comb can exceed that. Passing measured stress scenarios does not extend that
proof. Peaks can exceed unity, and final output gain scales them.

Full-chain latency remains 92 samples. Dispersion group delay is frequency
dependent and can cancel against partially mixed dry audio. The static host
tail of 20 seconds plus FIR latency is a qualification allowance for tested
nominal excitation and settled controls, including bypass states. Recursive
filters do not promise exact silence for arbitrary prior levels or continuing
automation.

## Remaining release gates

The user confirmed listening completed on 2026-11-07. No source list or detailed
listening observations were supplied, so this records user acceptance without
inventing a formal listening matrix. External host validation, reference-condition
performance qualification and versioned release packaging remain open. Old HP/LP CPU
measurements do not qualify the expanded warm-processing chain. No release
acceptance or completion of every issue #20 dependency is claimed.

Related: [full chain](../methods/full_chain.md),
[comb](../methods/resonant_comb.md), [dispersion](../methods/frequency_dispersion.md),
[tone](../methods/tone.md), [previous Output evidence](output_section_validation.md).

## Continuation review (2026-11-06)

Gemini 4 Argon High reviewed supplied planning/validation context only. Interim
three-module verification can proceed; final issue/release acceptance remains
open. Source review confirms hot-session old-state history-reset tests and an
independent dynamic full-chain order comparison at 48 kHz already exist.
Higher-rate combined resonant/automation stress remains an additional gate.

The recorded 51.8434525 peak is measured after the complete Output section,
not at dispersion input. It does not establish that intermediate signal level
or extend the isolated analytical proof. No limiter or universal stability
claim follows from these observations.

Existing Output and full-chain CPU collectors leave the added modules bypassed;
the full-chain settings provenance also omits their controls. Expanded active
CPU qualification therefore requires collector coverage/provenance work before
measurement: warm bypass, enabled static, automation and resonant decay, under
the established 48/96 kHz reference procedure and independent raw audit.
Expanded factory presets remain a release gate. Future host controls must append
without moving current indices; signal-flow layout belongs in the UI. Carrier
integration was subsequently discarded by the user on 2026-11-07.

Fresh continuation verification: VS2026 Release and Debug builds passed. Full CTest confirmation passed 307/307 in Release (100.54 s) and Debug (264.31 s). An initial restricted Release harness timeout passed in isolation and the complete rerun. The Debug response matrix passed within its unchanged 300 s limit. Collector/stress extensions remain pending a successful GitNexus refresh; both refresh attempts stalled. No new DSP implementation or release acceptance is claimed.

## Stress and CPU qualification update (2026-11-07)

The user discarded carrier integration and confirmed listening completed.
The revised full-chain stress matrix passed all 48 cases at 8/44.1/48/96/192/384
kHz, signed maximum comb feedback, static and automated/warm-bypass excitation,
and both distorted and high-gain upstream-bypass paths. Variable blocks include
oversized dispatch. The independent auditor reread interleaved float captures
and confirmed finite output, exact silent-channel isolation, 92-sample reported
latency, measured peaks and measured residuals. Maximum peak was 155.1373749;
maximum final100ms residual after20s was 1.6890225e-9. This is scenario evidence,
not an arbitrary-input stability proof. The existing nominal Output resonance/
decay CTest also passed with its unchanged gate.

Evidence: `docs/measurements/expanded_chain_issue20_stress_v2/` and
`docs/measurements/expanded_chain_issue20_stress_v2_audit.json`. The 13 auditor
regressions passed, including forged percentile/peak, bypass, leakage,
truncation, latency, partial-prefix and power-scheme failures.

The initial pinned CPU attempt was stopped after the120s sampling ceiling
retained only34,027–89,728 observations at1024 capacity, below100,000. Its
available p99 values were below25%, but that does not qualify the run. The
collector was revised before retry to allow10–1200s while retaining150k
wall-time slots,100k–200k observations and the25% incoming deadline gate.
Actual1s excitation/30s decay cycles now repeat throughout the sampled window;
all four phases require coverage. Session metadata is saved before collection,
and CSVs plus case metadata flush after each case. Fail-fast preserves explicitly
unqualified partial results on a failed reference gate. The revised60-case
unpinned smoke run and its independent audit completed; it is not acceptance
or reference CPU evidence. Reference measurement remains pending.

## Reference CPU result (2026-11-07)

The revised pinned AC Release attempt stopped on its first failed timing gate,
as specified by --fail-fast. The independent raw audit passed and confirmed an
unqualified partial result:31 of180 planned cases collected. All30 cases at
48kHz passed timing,100k–200k sample coverage and stable-power checks. The first
96kHz case (stereo,32-frame capacity/incoming block, new effects warm-bypassed)
failed the unchanged25% full-chain budget: p99=92,900ns against a333,333.333ns
callback deadline, or27.87%; the25% allowance is83,333.333ns. It retained147,003
observations; coverage and power checks passed. No outliers were removed and
no timer overhead was subtracted. This is a real measured budget failure,
not the previous insufficient-sampling problem. Remaining149 cases were not
run after that failure; three complete passing repetitions are not claimed.

Conditions: Intel Core Ultra7 155H, MSVC195136252 Release, JUCE8.0.14,
Windows SDK10.0.26100.0, observed AC power, Balanced scheme
381b4222-f694-41f0-9685-ff5bb260df2e, performance class1/group0/mask0x1.
Thermal sensor unavailable and ambient unconfirmed. The session, each completed
case, raw timings, summaries, source/executable hashes and independent audit
are retained. Installed/production DSP was not changed by this qualification
work. Performance optimization or an explicit reviewed budget decision is
still required before CPU acceptance; the budget was not relaxed.

Evidence: docs/measurements/expanded_chain_issue20_cpu_reference_v2/ and
its independent audit docs/measurements/expanded_chain_issue20_cpu_reference_v2_audit.json.
Carrier is discarded and user listening is recorded as completed. Expanded
stress passed48/48 with independent raw capture verification. Issue20 remains
in progress: reference CPU qualification has failed, and release-specific
host/preset/packaging gates are not completed by these measurements.

### CPU resolution research (2026-11-07)

Three research agents completed attribution, control-cost and measurement-method investigations. The exact-argument ADAA primitive cache passes eight ADAA tests (75,384 assertions), 48 exact-byte full-chain/Output golden comparisons and independent source review. Its default-policy median benefit is only about 0.5 us; it does not close the failed budget. Same frozen executable screens show much larger process-policy sensitivity: default-policy p99 107.9/97.4 us at 48/96 kHz versus explicit HighQoS 24.3/23.8 us. Candidate HighQoS repeated p99 is 23.5–24.1 us. These are diagnostics, not qualification.

The new explicit standalone method windows-explicit-highqos-v1 captures/restores its own process policy and requires independent readback auditing, with every numerical gate unchanged. It is an offline active-audio surrogate; it makes no universal DAW policy claim. The original failed reference remains retained. Full regression/stress checks and an entirely fresh 180-case reference matrix are pending. See `docs/PERFORMANCE.md` for rationale and [Microsoft QoS](https://learn.microsoft.com/en-us/windows/win32/procthread/quality-of-service) for the policy distinction.

### Completed CPU resolution qualification (2026-11-07)

The pending checks above are now complete: 309 Release and 309 Debug tests,
84 Python tests, 48 exact-byte golden comparisons and a fresh 48-case raw stress
audit passed. The complete fresh 180-case, three-run CPU reference independently
qualified under `windows-explicit-highqos-v1`, including policy readback,
actual affinity, exact restoration, coverage, power, source/build identity and
92-sample latency. Worst p99 was 6.490746% of the incoming deadline (25% limit),
at 96 kHz / 67-frame oversized input; 148,958 observations, 45.3 us p99.

Retained evidence is in
`docs/measurements/expanded_chain_issue20_cpu_reference_highqos_v1/` and
`docs/measurements/expanded_chain_issue20_cpu_reference_highqos_v1_audit.json`.
Stress maximum residual after 20 seconds was 1.689022477435742e-9 and peak was
155.1373748779297. No numerical gate was relaxed. The original system-managed
policy failure remains valid; actual host validation is still separate.
