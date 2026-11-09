---
type: Method
title: "Resonant comb filtering"
description: "Reviewed isolated feedforward and damped feedback comb design and acceptance limits for issue 17."
timestamp: 2026-11-08T00:00:00Z
tags: ["comb", "delay", "feedback", "dsp"]
resource: "https://www.dsprelated.com/freebooks/pasp/Comb_Filters.html"
---

## Production usage update (issue #38)

Production issue #38 always enables comb damping, exposes no damping/bypass button, and clears history after its mix fade reaches zero before suspending wet work. Isolated harness settings below retain their own controls.
See the [current contract and evidence](../notes/issue38_validation.md).

## Module design and earlier evidence

# Resonant comb filtering

Design authorized on 2026-11-01 following an Antigravity supplied-plan review.
Implementation and measured evidence are tracked separately in the
[validation note](../notes/resonant_comb_validation.md). Related:
[processing contract](./processing_contract.md), [audition method](./audition_measurement.md),
and [project](../projects/disdorktion.md). Production integration belongs to X5.

## Proposed algorithm and controls

For each independent channel, read delayed internal signal d using linear
interpolation, compute l=(1-a)d+a*lPrevious, v=x+feedback*l, wet=v+feedforward*d,
and output=(1-mix)x+mix*wet. Write v after reading d. Maintain lPrevious on every
frame, even with damping off. Use double history and float input/output.

| Control | Inclusive range | Default |
| --- | --- | --- |
| Delay | 0.25-40 ms | 5 ms |
| Feedforward | -1 to 1 | 0 |
| Feedback | -0.95 to 0.95 | 0.7 |
| Requested damping | 20-20000 Hz | 6000 Hz |
| Damping enabled | false/true | true |
| Mix | 0-1 | 0.5 |
| Bypass | false/true | false |

Validate requested settings independently of sample rate; effective damping is
min(requested,0.45*sampleRate). When enabled, a=exp(-2*pi*effectiveHz/sampleRate);
when disabled target a=0. Negative feedback shifts the resonances. With integer
delay D and damping disabled the wet transfer is
H(z)=(1+feedforward*z^-D)/(1-feedback*z^-D). This supports feedforward notches,
feedback ringing, and their combination. Allpass interpolation was considered;
linear interpolation was selected for simple bounded time-varying behavior.
Its frequency-dependent attenuation changes high-frequency decay and tuning.

Advance controls once per sample frame, before applying the frame. Delay uses
a 100 ms linear ramp; all other continuous controls and bypass use 10 ms.
Durations are floor(sampleRate*seconds). Repeated targets do not restart ramps.
Full-range delay ramps move less than 0.398 samples/frame (nominally 0.3975), keeping read time
forward. Pitch glides are intentional. No promise of inaudible automation is made.
Changing delay also resamples history; upward shifts of high-frequency material
can alias. Linear interpolation attenuates high frequencies but is not a
bandlimited resampler, and this native-rate module makes no alias-free
modulation claim. Include bright material and full-range sweeps in listening.
Bypass blends to exact dry after settling while histories continue running.
Reset clears history and snaps smoothers to retained targets.

## Contract, headroom and tail

Follow the F1 contract for invalid preparation/settings, mono/stereo,
zero/oversized blocks and deterministic reset. Allocate delay storage only in
prepare: ceil(sampleRate*0.040)+4 double samples per channel. Stage allocations
before replacing valid storage. Processing, control updates and reset allocate
nothing, block on nothing, and perform no I/O/UI work. Scope
juce::ScopedNoDenormals around processing; retain the linear recurrence without
an arbitrary audible-signal floor.

The nominal input is [-1,1]; the finite input envelope is float maximum/128.
Linear interpolation and the damping recurrence are convex combinations. From
cleared state, with input peak X, |v|<=20X and |wet|<=40X by induction since
|feedback|<=0.95. Smoothed coefficients stay inside these bounds, so arbitrary
control changes retain this bound. No limiter or normalization is applied.

There is a direct path, hence reported algorithmic latency is zero; delay is
the audible effect parameter. Recursive decay is asymptotic. Tail estimates
must include damping and fractional interpolation, and describe their input
and static-control assumptions. A fixed 11-second capture is insufficient for
all damped settings. Bypass does not clear or freeze the tail. Fixed-duration
harness renders explicitly request trailing silence; file sources still loop.

`getTailSeconds()` bounds a unit impulse's static retained-control response at
1e-6 amplitude (-120 dB), independent of mix/bypass. It is not a guarantee for
arbitrary previously driven state or ongoing automation. For q=|feedback|,
D=ceil(delaySamples), solve q(1-a)*rho^(-D)+a/rho=1 for rho in (a,1).
The positive majorant bounds |v[n]| by rho^n; the wet numerator adds at most
|feedforward|*rho^(n-D). Round the bound upward. At zero feedback the only tail
is the feedforward delay, or zero when feedforward is also zero. The getter
returns zero before preparation. Capture windows for spectral measurements
must additionally bound the omitted impulse's l1 sum, not just peak amplitude.

## Acceptance limits set before implementation

Test mono/stereo at 8/44.1/48/96/192/384 kHz and capacities
1/32/64/127/256/1024, including empty/irregular/oversized blocks. Require finite
bounded output at settings endpoints and the input envelope, deterministic
reset, channel independence, transactional invalid reprepare and layout changes.
Controls at identical absolute frames must be block-invariant within 2e-6
absolute nominal-input error. Compare independent samplewise modulation within
2e-5. Verify ramp endpoints and exact dry bypass after settling.

Analytical impulse trains verify delay, signed feedback and decay. Complex
response tolerance is normalized error <=1e-4 where expected magnitude >=1e-4,
absolute error <=1e-5 near zeros. Captured impulse windows must have a bounded
residual below the tolerance; high feedback uses long captures or exact
time-domain comparisons rather than an unjustified short FFT.

Proposed isolated CPU ceiling is p99 <5% of the incoming deadline. Reference
matrix: stereo 48/96 kHz, capacities 32/64/127/256/1024, endpoints, modulation,
bypass, oversized callbacks and quiet decay, three pinned AC Release runs with
raw samples and provenance. Diagnostic results do not establish reference
acceptance. Listening must assess metallic coloration, ringing, notches,
level/headroom and delay glides using recorded settings.

## Verified references and review disposition

Julius O. Smith's [Comb Filters](https://www.dsprelated.com/freebooks/pasp/Comb_Filters.html)
provides the feedforward/feedback equations and signed-feedback resonance
locations. His [Delay-Line and Signal Interpolation](https://www.dsprelated.com/freebooks/pasp/Delay_Line_Signal_Interpolation.html)
explains linear interpolation's amplitude error in feedback and the allpass
alternative. The particular ranges, smoothing times and budget above are
project design decisions, not claims made by these sources.

Antigravity reviewed the supplied ticket, without repository inspection.
Adopted suggestions: slower delay ramp, explicit denormal protection, effective
damping clamp, continuous damping history, buffer margin, tail-aware frequency
tests, and reprepare/quiet-tail coverage. A reverse read trajectory alone does
not invalidate the contraction proof. Review CPU estimates remain predictions.

## Production integration (issue 20)

This module is integrated into the native-rate [Output section](output_section.md)
in the fixed comb -> dispersion -> tone -> high-pass -> low-pass order.
It defaults to bypass for Initialize, existing factory sounds and old sessions;
its histories remain warm. The isolated design and limits above remain module
contracts, not a universal peak bound for cascaded resonant inputs. See
[expanded-chain qualification](../notes/expanded_output_validation.md) for
combined-chain stress, state/automation and conditional CPU acceptance.
