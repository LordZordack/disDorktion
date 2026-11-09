---
type: Note
title: "Nonlinear wet Driver adjustment validation"
description: "Approved Driver routing and fixed compensation calibration method, with adjustment verification tracked separately from historical issue-38 results."
timestamp: 2026-11-09T00:00:00Z
tags: ["audio-dsp", "validation", "driver", "calibration", "latency"]
resource: "https://github.com/LordZordack/disDorktion/issues/38"
---

# Nonlinear wet Driver adjustment validation

## Approved contract

GLOBAL Driver remains 1x to 4x, default 1x. It acts after the crusher immediately
before tanh/fold inside the nonlinear wet branch. Global dry and Waveshaper
section dry remain unchanged. Fixed compensation applies only to nonlinear
output before shared Waveshaper Trim and section mixing. Manual trim retains
its existing behavior, including both nonlinear stages Off.

Driver uses 10 ms smoothing; compensation follows its delayed trajectory.
Production latency remains 92 host samples. Driver must have no audible effect
when both nonlinear stages are Off, Waveshaper Mix is zero, or global mix is
zero. The adjustment keeps parameter IDs and persistence schemas (plugin v5,
full-chain experiment v17). Saved sounds with Driver above 1x intentionally
change; defaults at Driver 1x retain their prior behavior.

## Approved calibration method

Replace unconditional inverse gain with a fixed calibrated lookup at Driver
knots **1, 1.5, 2, 3 and 4**. For each knot, calculate RMS correction relative
to Driver 1x across four active factory configurations, three source classes
(sine, two-tone and seeded noise), three levels (-24, -18 and -12 dB), and two
sample rates (48 and 96 kHz). Use the median correction across that matrix and
clamp it to **+/-6 dB**. Interpolate linearly in dB against log Driver.

The runtime lookup is fixed: it does not measure the incoming signal, perform
live level tracking or implement AGC. It cannot guarantee constant perceived
loudness for other material or settings. Manual trim remains available.

## Evidence status

The offline calibration tool built and ran using the production 4x FIR/ADAA
configuration. It measured 72 fixtures at each of five knots (360 rows).

| Driver | Fixed correction (dB) |
| --- | ---: |
| 1x | 0 |
| 1.5x | -1.9492362033 |
| 2x | -2.7902677615 |
| 3x | -3.5984053728 |
| 4x | -3.9769051932 |

Across nonunity rows, median absolute RMS error relative to Driver 1x improves
from 3.5041 dB with inverse compensation to 2.6883 dB with this curve. Every knot
improves its fixture median, but 188 individual rows improve and 100 regress.
In particular, Subtle Warmth's pooled median error grows from 0.6805 to 3.0579 dB;
the other three configurations improve. This is an aggregate calibration result,
not uniform matching or a perceptual loudness guarantee. Manual trim is needed
for some settings and material.

An independent audit checked all fixture identities and calibration arithmetic.
Recalibrating after implementation reproduced the complete JSON exactly, and
all five production coefficients match the audited report.

## Implementation verification

- Complete `windows-vs2026-release` build passed, including VST3, standalone,
  audition, tests and the calibration tool.
- All **315 configured CTest checks** passed: 314 passed initially; the existing
  Output impulse/complex-response test timed out, then passed on isolated rerun
  in 30.83 seconds. No source change was needed for that rerun.
- All **84 Python tests** passed. The realtime suite passed 19 cases/90
  assertions, including Driver automation, compensation and inactive transitions
  without allocation.
- New regressions verify crusher/section-dry independence, fixed curve bounds,
  nonlinear compensation before shared trim/mix, delayed automation trajectories,
  callback partition invariance and invalid Driver rejection. The first routing
  regression was observed failing against the previous implementation.
- Independent source review found no actionable defects. Global dry isolation
  and target changes during long idle intervals also received source review;
  the reviewer suggested optional extra regression cases for those combinations.

These are functional, calibration and allocation checks. Human listening,
live DAW acceptance and renewed reference CPU qualification remain unperformed.
VST3 installation verification is recorded in the working ticket.

Verification should cover routing and dry invariance, Driver automation and
delayed compensation, Driver 1x/default equivalence, both-stages-Off and zero-mix
endpoints, fixed 92-sample latency, and unchanged parameter/state round trips.
Historical [issue-38 results](./issue38_validation.md) apply to its earlier
pre-split Driver/inverse-compensation routing only.

Related design: [full chain](../methods/full_chain.md),
[Waveshaper](../methods/middle_section.md),
[gain](../methods/gain.md),
[plugin interface](../methods/plugin_interface.md),
[project](../projects/disdorktion.md).
