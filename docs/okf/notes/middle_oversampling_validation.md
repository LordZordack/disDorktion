---
type: Note
title: "Middle section anti-aliasing validation"
description: "Q1 measured FIR and ADAA comparisons, equivalent CPU optimization, and pending listening and production acceptance."
timestamp: 2026-10-12T00:00:00Z
tags: ["audio-dsp", "oversampling", "validation", "latency"]
resource: "https://github.com/LordZordack/disDorktion/issues/9"
---

# Middle section anti-aliasing validation

The [design and predeclared targets](../methods/middle_oversampling.md) describe
the Q1 boundary. Evidence supplements the [native tanh](./tanh_saturation_validation.md)
and [native folding](./wave_folder_validation.md) validation.

Measurement directories named below are generated local evidence and are not
included in the Git change set. Reproduce them with the commands in
`docs/AUDITION_HARNESS.md`; the design, summarized results, source tools and
tests are published for review. Listening and extreme-alias approval remain open.

## Review and scope

The user requested token-efficient planning, Antigravity plan review, adoption
of relevant feedback and implementation. Antigravity reviewed supplied plan,
dispatcher and native module context without repository tools or tests. Its
completed review and disposition are retained in the local ticket and
`.tools/issue-9-agy-plan-review.txt`. Adopted safeguards cover internal-domain
validation, host block splitting, coherent collision checks, phase/blend tests,
latency rounding and 10 ms smoothing. Several severe findings were hypothetical
implementation hazards already addressed in the plan, not native module bugs.

Source changes supply a reusable evaluation module and measurement executable.
Production routing, UI, persisted full-chain parameters and host dry mixing
remain the scheduled S2/C1 work. No human listening acceptance is inferred.

## Initial integer-correction baseline

Baseline report (`docs/measurements/middle_oversampling_issue9/result.json`)
records 900 spectra, 15 filter responses, 144 local CPU cases and 18 matched
listening sets (90 WAVs), with the original JUCE integer/Thiran correction.
The accompanying source snapshot records the evaluated working tree.

At 48 kHz, 4x reports 61 samples; at 0.4 Fs the residual phase is 60.444 degrees
and the 50-percent blend is -1.2695 dB. This **rejects that correction** under
the predeclared phase/blend targets. 8x and 16x also exceed the phase target.
These measurements motivated the extra FIR fractional-delay correction.

## Corrected FIR candidate

Candidate report (`docs/measurements/middle_oversampling_fir_issue9/result.json`)
uses the documented 65-tap correction. At 4x, latency is 92 samples. Measured
filter-only response at 0.4 Fs/48 kHz is -0.001355 dB, residual phase about
0.000396 degrees, and half blend -0.000678 dB. Magnitude/phase responses for all
factors and mixes are retained in `filter_response.csv`; these do not describe
the nonlinear transfer or the later output-filter/full-chain blend.

At 48 kHz, input bin 3371/16384 (9875.98 Hz), input amplitude 0.25, 12 dB drive:

| Path / threshold | Native off-harmonic dBc | 4x dBc | 8x dBc | 16x dBc |
| --- | ---: | ---: | ---: | ---: |
| Tanh | -23.54 | -104.24 | -100.40 | -98.88 |
| Folder / 0.25 | +5.98 | -23.51 | -32.09 | -41.95 |
| Cascade / 1 | -0.01 | -26.91 | -39.73 | -50.56 |
| Cascade / 0.25 | +11.21 | +4.36 | -18.41 | -22.10 |

The cascade's 12 dB applies to **both** drives. The aggressive 0.25 cascade
improves only about 6.85 dB at 4x, failing the predeclared 15 dB improvement
target. Positive dBc values can occur when folding suppresses the fundamental;
compare the saved absolute spectra/fundamental level as well. Increasing the
factor does not establish a -60 dBc floor for these sharp folding/cascade cases.

**Decision:** the phase correction is supported by automated evidence, but an
unconditional fixed-4x quality approval across the measured controls is rejected.
4x remains a useful low-cost candidate for tanh and milder settings. 8x is a
listening comparison with better aggressive-cascade suppression and higher CPU
cost. Choosing intentional residual coloration or revising M3/M4 antialiasing
requires an explicit decision before S2 acceptance. No factor is silently
approved by relaxing the numerical limits after measurement.

## Cleaner triangle-folder evaluation

The user described the initial clips as strong distortion that might be intended,
without a rate preference or playback setup. They then chose **"Keep approval
open; evaluate cleaner folding."** This records an observation and scope choice,
not residual-alias acceptance.

The separate first-order ADAA option is defined in the
[method](../methods/middle_oversampling.md#separate-first-order-adaa-candidate).
ADAA report (`docs/measurements/middle_oversampling_adaa_issue9/result.json`) and
validation (`docs/measurements/middle_oversampling_adaa_issue9/validation.json`)
contain another 900 spectra, 15 filter responses, 144 CPU cases and 90 matched WAVs.

At 48 kHz/4x, 12 dB drive and the same high-tone fixture:

| Path / threshold | Plain triangle dBc | ADAA dBc | Fundamental change |
| --- | ---: | ---: | ---: |
| Folder / 0.25 | -23.51 | -41.49 | +0.55 dB |
| Cascade / 1 | -26.91 | -48.26 | -0.20 dB |
| Cascade / 0.25 | +4.36 | -18.68 | +1.05 dB |

4x ADAA passes the representative relative-improvement gate for all tested rates
(minimum about 20.20 dB over its own native-rate ADAA comparison). The table also
compares against the original 4x triangle. This does not establish the -60 dBc
residual goal. At 36 dB/threshold 0.25, the folder fundamental falls from -30.75
to -59.41 dBFS, while off-harmonic aggregate power falls by about 24.77 dB.
Fundamental suppression makes dBc alone misleading; inspect absolute spectra
and changed timbre too.

4x ADAA reports 92 host samples. Through 0.4 Fs, worst magnitude/phase/half-blend
deviations are about **0.032 dB / 0.00065 degrees / 0.016 dB**, meeting the
filter targets. Native/2x ADAA fail the magnitude target. Nominal linear delay
includes 1.5 internal samples in all bypass states; driven nonlinear behavior
has no single LTI group-delay description.

CPU remains a failed gate: worst 4x p95 batch mean is **23.4%** of block duration,
versus **19.1%** for the corrected plain triangle, exceeding the provisional
10% budget under uncontrolled local conditions. This does not prove a deadline
miss, but it does not qualify the budget.

**Decision:** ADAA is a tested, measurably cleaner evaluation option for the
representative folder/cascade cases. Q1 approval stays open for residual aliases,
changed character, CPU tradeoff/optimization and human listening. The default
evaluation remains the plain triangle; no accepted M4 replacement or production
chain choice is inferred. An independent source reviewer found no concrete
implementation defect and confirmed the mathematical design; no reviewer tests
were run.

## Reproduction and interpretation

Use a fresh output path with `disdorktion_middle_measurements`; instructions are
in the harness guide (`docs/AUDITION_HARNESS.md`). `scripts/validate_middle_measurements.py`
checks coverage, gzip/ordered spectrum bins, saved WAV lengths, finite samples,
headroom, RMS matching and decision gates. It can compare a repeated run while
excluding CPU/provenance JSON. Full spectra use peak-amplitude normalization
and are compressed in `spectra.csv.gz`. Each single-tone record includes its
rate, coherent bins, drive, threshold, factor and warmup. Source snapshots and
build identities accompany the evidence; the starting Git revision alone does
not identify an uncommitted working-tree evaluation.

Frequency/phase/group-delay results use impulse responses with both nonlinear
modules bypassed. Single-tone capture waits at least 50 ms plus 1024 frames.
Two-tone spectra are unclassified; 16x is not an alias-free oracle. CPU timing
uses 128 warmup blocks and 21 batches of 128, preallocated input copying included;
nearest-rank p95 is a batch mean, not a callback tail percentile. Power, affinity,
thermal and background activity are uncontrolled; local figures are diagnostic.

## Automated verification

| Check | Result |
| --- | --- |
| Full VS2026 x64 Debug build / CTest | 170/170 passed, 57.83 s |
| Full VS2026 x64 Release build / CTest | 170/170 passed, 11.45 s |
| Native M3 regression generator | 18 fixtures passed |
| Native M4 regression generator | 62 fixtures passed |
| Corrected triangle artifact validation | 900 cases / 7,373,700 bins / 90 WAVs passed |
| ADAA artifact validation | 900 cases / 7,373,700 bins / 90 WAVs passed |
| Repeated ADAA spectra/filter/audio | 93 files byte-identical; CPU/provenance JSON excluded |
| Validator malformed-evidence regressions | Missing file, three truncated CSVs and two stale gate flags rejected |
| Realtime instrumentation | Both boundary modes allocate/deallocate zero in processing, updates and reset |
| Independent source review | No concrete DSP defect; reported validator gap fixed |
| OKF bundle validation | Passed after respecting its bundle-only local-link policy |
| GitNexus all / compare-main audit | 32 files, 161/161 symbols, 13/13 processes; partial=false, truncated=false; aggregate risk HIGH |

Numerical tests cover the independent unwrapped-corner integration oracle,
large-cycle/tiny-step cases, smoothing, bypass compensation, reset/chunking,
stereo isolation, finite wet extremes, actual wrapper capacities 1/65536 at
384 kHz/16x, mode rejection and filter phase. Identity stubs failed at numerical
assertions before implementation; the original integer correction failed the
phase regression. These checks do not supply a human sound verdict, individual
callback tail timing, VS2022 or cross-platform qualification.

The complete graph audit includes new source/doc files via a temporary Git index;
the real index remains unchanged. CLI prose shortened the display, so raw reports
were obtained from the same installed backend and saved in
`.tools/issue-9-graph-all.json` / `issue-9-graph-compare.json`. The raw run warned
that FTS was unavailable; graph/diff analysis still completed without partial
or truncation flags. HIGH aggregate risk remains recorded, not waived by tests.
Earlier unresolved C++ member-call edges were checked against scoped source.

## Listening protocol and remaining gates

There are 18 synthetic matched sets: tanh/folder/cascade, 12/36 dB drive,
tones/transients/sweeps, each at native/2x/4x/8x/16x. All files are stereo 48 kHz,
3 seconds, integer-delay aligned and RMS matched within 0.001 dB, using a common
peak scale. RMS matching is not perceptual loudness matching; residual phase
and nonlinear timbre differences remain. Keep playback gain fixed and alternate
factors within a set. Record alias audibility, transient ringing, filtering,
preferred factor and whether residuals are acceptable for the intended character.
Include listener/date/playback chain and repeat with representative real program
material before final musical qualification. No observations have been supplied.

Pending: human comparison and chosen residual-alias tradeoff; any required
waveshaper revision with repeated affected M3/M4 tests, spectra, controls and
CPU checks; S2 interactions/automation; C1 dry capture, host latency and full mix
range. These are genuine acceptance gates, not missing automated measurements.

## CPU optimization follow-up

The user authorized the recommendations to profile and optimize the 4x FIR +
ADAA candidate, retaining latency and quality limits. Temporary Release stage
instrumentation attributed about 40% of cascade time to oversampling, 30% to
ADAA and 21% to the host correction FIR. The selected equivalent optimization
mirrors each 65-sample correction ring into 130 entries and moves its write
position backwards. All newest-to-oldest samples are contiguous, eliminating
per-tap wrap branches. Coefficients, double accumulation order, float output,
controls and latency are unchanged. Reset clears both copies; the additional
storage is 520 bytes across two channels. No timing instrumentation remains in
the DSP or build configuration.

Median ratios across the six rate/block cases indicate approximately 47% less
FIR time and 11% less instrumented cascade time. These are component diagnostics
under uncontrolled local scheduling. The original 23.4% worst CPU observation
is historical: a fresh unchanged baseline reached 10.54%, demonstrating a
substantial environment contribution. Do not attribute that entire difference
to the code optimization.

| Uninstrumented matrix | Worst 4x p95 batch mean / block duration |
| --- | --- |
| Fresh unchanged ADAA baseline | 10.54% |
| Optimized ADAA | 6.30% |
| Optimized ADAA repeat | 6.01% |
| Optimized plain triangle | 14.49% |

The candidate meets the unchanged provisional 10% local budget in two runs.
The plain comparison's outlier reinforces that these batch means do not qualify
callback deadlines or support a controlled CPU ranking between algorithms.
Preserve all observations; production host testing remains outstanding.

Evidence directories under `docs/measurements/` are
`middle_oversampling_adaa_cpu_baseline_issue9`,
`middle_oversampling_adaa_optimized_issue9`, its `_repeat`, and
`middle_oversampling_triangle_optimized_issue9`. Each optimized matrix has
900 spectra, 15 filter grids, 144 CPU cases and 90 WAVs. Independent validators
pass artifact checks. All 93 deterministic files match the corresponding
pre-optimization baseline or repeat byte-for-byte. The 4x ADAA filter/alias
gates are unchanged; plain folding still misses the representative alias gate.
An independent scalar original-ring test also matches all factors and both
algorithms exactly across stereo, wraps, interrupted controls and reset
(83,440 assertions). Source and binary provenance accompany the measurements.

## User-supplied listening material

The user provided `bah.WAV` from their local SQAM directory: stereo 44.1 kHz,
418,126 frames (9.4813 seconds). Initial matched 12 dB/0.25 cascade renders are
in `docs/measurements/middle_listening_bah_issue9/`. Drive is applied to **each**
stage. User feedback: "They both sound about equally distorted. I can't really
even hear the original source through the distortion." This expresses neither
an algorithm preference nor acceptance of residual aliases.

The follow-up listening renderer includes a reference and milder recipes:
tanh/folder drive 0/0 dB with threshold 0.75, and 6/0 dB with threshold 0.5;
the 12/12 and 36/36 dB threshold-0.25 stress recipes remain available. For each
recipe compare plain 4x, ADAA 4x and ADAA 8x. These are listening settings, not
new plugin defaults or revised DSP limits. Preserve source rate and input
level, crop reported integer latency, match whole-excerpt RMS within 0.001 dB
and apply one common peak scale. RMS matching does not equal perceptual loudness.
The tool records source SHA256, source/excerpt lengths, build/revision/JUCE
metadata, per-file parameters, alignment and matching gains. Human judgment
and the extreme residual-alias decision keep Q1 approval open.

## Follow-up verification

Full VS2026 Debug and Release builds/CTest pass 171/171 cases in 36.48 and
7.86 seconds respectively. The final listening renderer also builds in both
configurations. A reviewer confirmed FIR equivalence and no remaining concrete
listening-tool defect after the input guard and provenance fixes; the reviewer
did not run tests. Direct CLI checks verify missing input (exit 1, no output),
existing output (exit 2, unchanged), valid mono, silent/corrupt/unsupported-channel
rejection and usage errors.

The gentler user-clip evidence is
`docs/measurements/middle_listening_bah_gentle_issue9/` and its `_repeat`.
Independent RIFF/header/frame/finite/headroom/RMS/source-hash checks pass for
all 13 saved files, and all 13 match the repeat byte-for-byte. Maximum RMS
difference is 6.75e-7 dB. Source snapshots include the renderer and independent
validation script. The user has been offered the moderate comparisons; no
acceptance of the gentle set or extreme alias policy is inferred.

The follow-up all/compare-main graph diff lists 33 files, 178/178 changed symbols
and 14/14 processes, partial=false and truncated=false; aggregate risk HIGH.
The temporary index preserves actual staging. The graph refresh preceded the
last listening-recipe extension; its entry point and local render record had
UNKNOWN callers, confirmed from scoped source/CMake use. Final renderer source
was independently reviewed. Refresh before any later commit; no publication or
tracker acceptance was performed. Whitespace and OKF validation pass.

## Pull request preparation (2026-10-13)

Fresh `windows-vs2026` configuration, complete Debug/Release builds and CTest
pass 171/171 cases each (29.99 s / 7.10 s). OKF link validation passes; all 28
OKF Markdown files also parse as YAML with nonempty `type`, and the root version
is 0.2. Reviewed DSP/listening source hashes match the verified source snapshot.
Publish this evaluation as a draft: human sound acceptance and extreme alias
policy are unresolved, with production S2/C1 integration still outside this
change. Local ticket notes, build outputs and generated recordings stay local.

The final publication graph refresh includes the complete renderer. Staged
all/compare-main audits cover 32 files, 188/188 changed symbols and 14/14 flows,
with partial=false and truncated=false. Aggregate risk remains HIGH for review.
Full-text/BM25 search was unavailable because the local FTS extension could not
load; structured graph and diff analysis completed. This supersedes the earlier
pre-recipe index limitation. No generated measurement artifacts are staged.
