---
type: Method
title: "Middle section oversampling and latency"
description: "Q1 evaluation boundary, fractional latency correction, and evidence requirements before production middle-section acceptance."
timestamp: 2026-10-12T00:00:00Z
tags: ["audio-dsp", "oversampling", "aliasing", "latency"]
resource: "https://github.com/LordZordack/disDorktion/issues/9"
---

# Middle section oversampling and latency

Q1 evaluates the nonlinear boundary for [DisDorktion](../projects/disdorktion.md).
See [measured results and remaining gates](../notes/middle_oversampling_validation.md).
Production assembly is S2 (#11); the global mixer and host adapter are C1 (#13).

## Boundary and preparation

`MiddleSection` runs upsampling, [tanh](./tanh_saturation.md),
[triangle folding](./wave_folding.md), then downsampling. The input gain and
[intentional bit crusher](./bit_crushing.md) run outside it. Output filters also
run outside it. The comparison factors are 1, 2, 4, 8 and 16; changing factor or
latency mode requires stopped processing and preparation, never a live callback.

Pinned JUCE 8.0.14 provides maximum-quality equiripple half-band FIR stages.
The constructor receives log2(factor), not the linear multiplier. Stage zero
uses normalized transition widths 0.05 up / 0.06 down and attenuation targets
-90 / -75 dB; subsequent stages use 0.10 / 0.12 and targets -80 / -65,
-70 / -55, then -60 / -45 dB. These are design inputs from the pinned source,
not measured rejection guarantees for the nonlinear cascade.

The existing native module contract accepts host rates 8–384 kHz, capacities
1–65536 and mono/stereo. Explicit `prepareOversampled(hostSpec, factor)` validates
that domain and power-of-two factors 1–16 before translating the rate/capacity.
It permits internal rates through 6.144 MHz and capacities through 1,048,576
without relaxing native `prepare` limits. Preparation retains settings;
invalid requests preserve running history and smoothers. Controls ramp over
`floor(hostRate * factor * 0.010)` internal frames. Parameters/reset/process
run on the processing thread or while stopped; preparation may allocate.

Host blocks are split at the prepared capacity **before** calling JUCE's
upsampler. Zero blocks do not advance history. Both module bypass controls
operate inside the high-rate boundary; the FIR path stays active even when
both are bypassed. Thus bypass preserves latency and still filters the signal.
Native isolated exact-bypass behavior remains available at factor 1.

## Integer latency and dry alignment

JUCE's optional integer correction uses a Thiran interpolator. The initial
4x candidate measured 61 samples of reported delay but failed the phase and
blend targets. Retain it as `MiddleLatencyCorrection::juceThiran` for comparison.

The evaluated default leaves JUCE's fractional delay uncompensated. When the
raw FIR latency is fractional, it adds a host-rate 65-tap windowed-sinc delay:

```text
fraction = ceil(rawLatency) - rawLatency
center = 32 + fraction
tap[n] = Kaiser(n, 65, beta=8.6) * sinc(n - center)
normalize taps by their sum for unity DC
reportedLatency = lround(rawLatency + center)
```

Integer raw latency omits this correction. The correction is an approximation
whose magnitude and residual phase are measured; it is not an ideal fractional
delay. Filter state is independent per channel. Current factors report 0, 49,
92, 97 and 99 host samples for 1x–16x, independent of host sample rate, block
partition and module bypass. At 48 kHz, 4x adds approximately 1.917 ms.

C1 must capture global dry before input gain, delay it by the same reported
integer sample count, and report that count to the host from preparation.
Use a linear amplitude mix, `(1-mix)*delayedDry + mix*wet`; final output gain
affects both. Test all mix endpoints and intermediate settings. Delay alignment
does not cancel nonlinear changes, FIR ringing or the output filters' phase;
full-chain blend/listening checks remain C1 work. Host bypass is a separate
adapter policy and must preserve the chosen latency contract.

## Decision limits and comparisons

Project engineering targets, declared before measurement: filter-only magnitude
within 0.1 dB, residual delay-corrected phase within 5 degrees and half-blend
magnitude within 0.2 dB through 0.4 host Fs; no callback heap activity; fixed
integer latency across bypass; finite nominal audio. Representative high-tone
alias power at 12 dB drive should improve by at least 15 dB over native wherever
native off-harmonic power is above -80 dBc. -60 dBc is a practical residual goal,
including stress reporting; it is not a perceptual guarantee. Local CPU p95
**batch mean** below 10% of block duration is provisional and does not establish
individual callback deadline tails.

Single-tone FFT inputs use coprime bins, steady-state warmup and rectangular
windows. Off-harmonic power excludes true in-band odd harmonics and checks
alias/harmonic collisions through odd order 127. This estimate misses colliding
higher-order aliases and includes numerical floor/leakage; near-floor results
must not be interpreted as exact alias powers. Two-tone spectra contain useful
intermodulation as well as aliases and are not assigned an alias score. 16x is
a higher-rate comparison, not ground truth. Listening files are integer-aligned,
RMS matched and peak scaled together; generated files supply no human verdict.

## Sources and alternatives

[JUCE's official API](https://docs.juce.com/develop/classjuce_1_1dsp_1_1Oversampling.html)
documents raised-rate nonlinear processing, FIR/IIR phase and latency tradeoffs,
and integer delay compensation. FIR is the phase-oriented baseline; native
avoids filtering/latency but aliases, while IIR is a lower-latency alternative
with a different phase response. Increased rate alone does not establish an
acceptable alias floor for sharp triangle corners or heavily driven cascades.

Kahles, Esqueda Flores and Välimäki, *Oversampling for Nonlinear Waveshaping:
Choosing the Right Filters*, JAES 67(6), 440–449 (2019),
[DOI 10.17743/jaes.2019.0012](https://research.aalto.fi/en/publications/oversampling-for-nonlinear-waveshaping-choosing-the-right-filters/),
compares interpolation/decimation choices using a perceptual measure for hard
clipping at 8x. Its tested signal/filter conditions do not prove our 4x triangle
cascade meets a fixed dBc target. Here it motivates measuring both filter
behavior and nonlinear spectra, followed by listening, rather than choosing a
factor from filter specifications alone. The numerical limits above are ours.

If residual folding aliases are unacceptable, consider corner antialiasing or
continuous-time/antiderivative treatment, not merely more stages. The existing
[Buchla research](../papers/virtual_analog_buchla_259_wavefolder.md) and
[Parker et al. continuous-time convolution paper](https://dafx.de/paper-archive/2016/dafxpapers/20-DAFx-16_paper_41-PN.pdf)
identify relevant families of techniques. The separate ADAA evaluation below is
implemented but remains unqualified for production. Any later M3/M4 transfer,
control, history or alias-treatment
revision must repeat its affected native and oversampled validation before S2.

## Separate first-order ADAA candidate

The user asked to keep approval open and evaluate cleaner folding.
`MiddleFoldAlgorithm::adaaTriangle` selects a separate `AdaaWaveFolder` during
stopped-processing preparation. The native triangle remains the default
evaluation option. The [source](../papers/continuous_time_waveshaping.md) informs
the quotient; the periodic triangle primitive and compensation are project derivations.

For `z = drive*x`, `r = fmod(abs(z)/T, 4)`, define the even periodic primitive:

```text
P(z,T) = T*T * r*r/2                   for 0 <= r <= 1
         T*T * (1-(r-2)*(r-2)/2)       for 1 < r <= 3
         T*T * (r-4)*(r-4)/2           for 3 < r < 4
wet = outputGain * (P(zNew,T)-P(zPrevious,T))/(zNew-zPrevious)
```

The current drive/threshold apply to current and previous **raw** input. Equal
values evaluate the triangle directly. Within the central linear region use
their average; midpoint evaluation otherwise requires `abs(delta)/T < 1e-6`,
so large complete-cycle jumps cannot be confused with near-equal pairs. Double
intermediates are used. Constant signals converge to the original triangle;
varying signals have changed harmonics and transients.

Dry module bypass uses the adjacent raw-input average. After the internal
wet/bypass blend apply the symmetric shelf `[-1/8, 5/4, -1/8]`. Its linear/bypass
response is `cos(w/2)*(1.25-0.25*cos(w))`, with nominal delay 1.5 **internal**
samples. Add `1.5/factor` to host latency before fractional correction, in all
bypass states. Histories advance while bypassed and reset to zero. The shelf's
coefficient absolute sum is 1.5: corrected wet magnitude is bounded by
`1.5 * outputGain * T`, with transient overshoot possible. Nominal input is
[-1,1]; arbitrary extreme bypass inputs have no finite float output guarantee.

4x/8x/16x filter-only responses meet the provisional passband/phase/mix targets.
Native/2x ADAA fail the magnitude budget and remain comparison material.
Many-cycle folding may suppress wanted harmonics. Do not call the method
universally alias-free or assign its driven nonlinear response one LTI phase
curve. The equivalent mirrored-ring FIR optimization passes the provisional
local CPU budget in two runs; production timing and listening gates remain open.
See the [follow-up evidence](../notes/middle_oversampling_validation.md).
