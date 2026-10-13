---
type: Paper
title: "Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution"
description: "Source for an antiderivative quotient and its linear-response delay, informing Q1's separate triangle-folder evaluation."
timestamp: 2026-10-12T00:00:00Z
tags: ["audio-dsp", "waveshaping", "antialiasing"]
authors: ["Julian D. Parker", "Vadim Zavalishin", "Efflam Le Bivic"]
resource: "https://www.dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf"
---

# Continuous-time waveshaping

Parker, Zavalishin and Le Bivic, DAFx-16, Brno, September 5–9, 2016.
[Full source](https://www.dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf).
The source gives the conference dates; a separate publication day is not recorded.

Focused source review: sections 2 and 4. Equation (9) derives a first
antiderivative difference quotient from a linearly reconstructed input and
rectangular continuous-time convolution. Section 2.3 treats near-equal-input
numerical conditioning. Equation (17) gives a two-sample average in the linear
case, with half-sample delay and frequency droop. These effects require accounting
for latency and coloration; the technique is not an ideal brick-wall filter.

DisDorktion derives its own periodic triangle primitive and evaluates it in the
[middle boundary](../methods/middle_oversampling.md). Its 3-tap compensation,
control policy and engineering budgets are project choices, not paper results.
See [local measurements](../notes/middle_oversampling_validation.md). This focused
entry does not reproduce the paper's complete benchmarks or feedback extension.
