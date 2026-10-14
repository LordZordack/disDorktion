---
type: Method
title: "Native-Rate Output Filter Section"
description: "Ordered high-pass and low-pass composition with independent controls and reproducible section qualification."
timestamp: 2026-10-14T03:46:45Z
tags: ["output-section", "filter", "realtime", "qualification"]
---

# Output section

The reusable section processes the existing [high-pass](high_pass_filter.md)
then [low-pass](low_pass_filter.md) at the host sample rate. It uses independent
filter histories and the established module interface. Each filter has
Butterworth, Chebyshev I (1 dB ripple, peak 0 dB), and Chebyshev II (40 dB
stopband) responses. Requested cutoffs remain in 20-20000 Hz, capped for
processing at 0.45 times sample rate. Defaults are 20 Hz HP, 20000 Hz LP,
Butterworth, both active. This wide setting is filtered and is not exact unity.

Cutoff retains its child half-power meaning. The combined magnitude is the
product of both responses, so a child cutoff need not be the section's -3 dB
point. Equal Butterworth cutoffs produce approximately -6.02 dB at their shared
cutoff. Crossed cutoffs are valid and can attenuate the entire audible band;
they are not reordered or constrained against each other. Chebyshev ripples
combine, and Type II transmission zeros and stopband floors remain part of the
response. These are design expectations; measured errors are in the
[validation note](../notes/output_section_validation.md).

All cutoff, response and bypass transitions retain the children's 10 ms ramps.
The audition target exposes independent bypasses plus a common bypass that
temporarily bypasses both, preserving stage choices for release. All six banks
continue processing during bypass. Settled full bypass is exact dry signal.
Simultaneous fades can produce phase-dependent attenuation; ordered-module
agreement, finite output and bounded peaks are the acceptance criteria.

The section is in-place, with no scratch audio or processing-time allocation.
Its dispatcher validates block shape before either child runs, then splits
oversized audio into non-owning chunks. Invalid prepare/settings calls preserve
the previous configuration, histories and settings. Settings validate both
stages before applying either. Reset clears histories and snaps retained
targets. Module methods require exclusive processing ownership; the shared
harness publishes valid scalar controls through lock-free atomics at callback
boundaries. Independent scalar reads do not promise a whole-state snapshot.

Algorithmic latency is zero samples in every bypass state. Frequency-dependent
phase/group delay is separate and is exported with the complex response.
Nominal unit-peak inputs are qualified; the post-HP peak scan verifies the LP's
[-4,4] input envelope. No limiter or normalization is inserted. The getter is
for the processing owner, and retains its previous value for empty/rejected
processing calls.

## Acceptance limits established before implementation

| Check | Limit |
| --- | --- |
| Ordered modules, including controls | Maximum sample error <= 1e-6 |
| Dynamic stage-order negative control | Reversed chain differs by > 1e-3 |
| Independent double direct-form BLT impulse | Maximum sample error <= 6e-6 |
| Independent complex response | Absolute complex error <= 6e-5 |
| Stepped-sine response above -60 dB | Magnitude error <= 0.1 dB |
| Below -60 dB and transmission zeros | Absolute response error <= 6e-5 |
| Rapid automation with unit-peak sources | Finite output, peak < 8, inter-stage peak <= 4 |
| Minimum-cutoff tail after two seconds | Peak < 1e-6 |
| Realtime methods | Zero heap allocations and deallocations |
| Isolated CPU p99 | Strictly < 10% of callback deadline |
| Algorithmic latency | 0 samples |

Numerical limits are measured gates, not a general convolution error proof.
The independent oracle derives analog poles/zeros and bilinear-transform
coefficients, rounds the HP output to float at the inter-stage boundary, and
uses a double direct-form LP recurrence. It shares no runtime TPT state or
coefficients. Static linear filtering commutes, so the dynamic order test also
checks an inverted chain with a deliberately asymmetric schedule.

The matrix covers 8/44.1/48/96/192/384 kHz, all nine response pairs, wide,
narrow, equal, crossed and endpoint cutoff pairs, every stage bypass state,
mono/stereo, variable/empty/oversized blocks, resets, rejected calls and device
changes. The isolated benchmark includes controls, dispatch and the peak scan,
following the repository's `docs/PERFORMANCE.md` procedure. Quick
diagnostics are distinct from three-run pinned AC reference qualification.

## Shared reproduction and alternatives

Select **Output section** in the audition app. Version 10 records preserve both
cutoffs, response IDs, stage bypasses and common bypass; versions 1-9 retain
their original schema and target. Offline rendering uses the same target and
preserves the requested frame count. Zero added latency does not remove filter
tails; renders do not automatically append tail samples.

A tool-local cascade would not supply the reusable module or shared audition
workflow. A new filter topology would invalidate independent module evidence.
Reuse was selected; any future module change requires affected acceptance
checks to repeat. The combined 10% section CPU budget passed all 600 pinned AC
reference cases with an independent raw-data audit; this does not separately
qualify each child's 5% budget. Listening and reference CPU gates left open by the
independent filter notes remain prerequisites for final S3 acceptance.

Related: [project](../projects/disdorktion.md),
[processing contract](processing_contract.md),
[input section pattern](input_section.md).
