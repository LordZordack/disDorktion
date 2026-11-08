---
type: Method
title: "Frequency dispersion"
description: "Issue 18 normalized allpass cascade, stability bounds and qualification gates."
timestamp: 2026-11-03T00:00:00Z
tags: ["allpass", "dispersion", "transients", "dsp"]
resource: "https://www.dafx.de/paper-archive/2009/papers/paper_36.pdf"
---

# Frequency dispersion

Approved 2026-11-03 after a supplied-plan Antigravity review. Isolated X3 module;
production integration remains X5. Related: [project](../projects/disdorktion.md),
[contract](./processing_contract.md), [spectral-delay paper](../papers/spectral_delay_filters.md)
and [validation](../notes/frequency_dispersion_validation.md).

## Objective and alternatives

Disperse distorted transients into chirp-like sweeps. Static linear allpass
processing changes phase/group delay and crest factor, while preserving sinusoidal
frequencies and fully wet magnitude. Dry/wet blends cause frequency cancellation;
coefficient automation can create sidebands. Neither pitch-transparent modulation
nor alias-free automation is promised.

The 2009 spectral-delay structure motivates cascading allpasses; its chirp-envelope
equalizer and delayed outer feedback are omitted here. True frequency-axis remapping
is a separate design, described by Evangelista and Cavaliere, DAFx 2007:
https://dafx.labri.fr/main/papers/p269.pdf . A plain serial allpass cascade does not
implement that operator. Kilohearts' documented Disperser controls provide a current
creative-effects reference (https://kilohearts.com/products/disperser); this design
does not reproduce its undocumented implementation or focused Q control.

## Algorithm and controls

32 first-order normalized lattice sections per channel, with double state:

```
g = sqrt(1-a*a)
y = a*x + g*s
sNext = g*x - a*s
```

The static section transfer is (a+z^-1)/(1+a*z^-1). Requested frequency is
20–20000 Hz, default 500; mix 0–1, default 1; bypass defaults false in isolation.
Reject invalid/nonfinite settings transactionally. Set f=min(requested,0.45*Fs),
t=tan(pi*f/Fs), a=clamp((t-1)/(t+1),-0.99,0.99).
Expose effective Hz=Fs/pi*atan((1+a)/(1-a)). Positive coefficients concentrate
delay toward Nyquist, negative toward DC. The lower safety limit corresponds to
about 76.8 Hz at 48 kHz and 614 Hz at 384 kHz. Requested values are retained, but
effective values can be clamped. Bilinear mapping only approximately preserves
physical-frequency behavior across rates.

Coefficient ramps take floor(Fs*0.100) frames; mix/bypass ramps floor(Fs*0.010).
Advance once before each frame. Histories continue through dry mix and bypass.
Full dry endpoints return exact input; reset clears state and snaps targets.
At a=0, wet is a 32-sample delay. Algorithmic latency reports zero because phase
dispersion is the intended effect; there is no universal compensating delay.

## Uniform automation and headroom proof

Each section matrix is orthogonal: y²+sNext²=x²+s² for every coefficient.
Summing over sections and frames bounds output energy plus remaining history by
input energy plus initial history energy. This does not alone bound infinite
duration, bounded-amplitude input.

For r=.99 and S=sqrt((1+r)/(1-r))=sqrt199, |s|<=S*X is invariant whenever
|x|<=X: sqrt(1-a²)+|a|*S<=S for |a|<=r. Cauchy–Schwarz gives
|y|<=sqrt(1+S²)*X=sqrt200*X. By induction through 32 sections, output and
intermediate signals are bounded by 200^16*X=6.5536e36*X from reset, including
arbitrary coefficient trajectories within bounds. Document finite input |x|<=16,
so the bound is approximately 1.049e38, below float maximum with margin.
Double intermediates hold all states. No limiter or normalization is applied.
This very conservative bound is a safety envelope, not expected musical gain.

## Qualification gates

Before implementation: user approved design, normalized recurrence, coefficient
safety limit and input envelope above. Capture-length and tail derivations are
specified by implementation and checked independently by measurement tooling.
Static group delay is 32*(1-a²)/(1+2*a*cos(w)+a²).

- Mono/stereo, 8/44.1/48/96/192/384 kHz, capacities 1/32/64/127/256/1024,
  empty/irregular/oversized blocks, rejection, reset and channel independence.
- No audio-thread allocation/blocking/I/O. Partition agreement <=2e-6 for
  nominal input and controls at identical absolute frames.
- Static complex response absolute error <=2e-4, fully wet magnitude error
  <=0.01 dB. Group-delay error <=max(0.02 samples,0.1%) including DC/Nyquist.
  Compute Re(DFT(n*h)/DFT(h)); bound both omitted l1 and first-moment tails.
- Unit-impulse static response below -120 dB within one second, subject to
  independently proven capture bounds; any failure remains visible.
- Three pinned AC Release runs, stereo p99 <5% deadline at 44.1/48/96/192 kHz,
  blocks 64/256/1024, static/automated/bypass/quiet-tail. Record conditions.
- Render transient/bass/percussion and endpoint sweeps; record human listening
  separately from generated audio. Qualification is incomplete without it.

## Production integration (issue 20)

This module is integrated into the native-rate [Output section](output_section.md)
in the fixed comb -> dispersion -> tone -> high-pass -> low-pass order.
It defaults to bypass for Initialize, existing factory sounds and old sessions;
its histories remain warm. The isolated design and limits above remain module
contracts, not a universal peak bound for cascaded resonant inputs. See
[expanded-chain qualification](../notes/expanded_output_validation.md) for
combined-chain stress, state/automation and conditional CPU acceptance.
