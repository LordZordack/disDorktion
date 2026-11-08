---
type: Method
title: "Reciprocal Tilt Tone Control"
description: "An isolated dark/bright balance control with a fixed unity pivot and warm bounded state."
timestamp: 2026-11-03T00:00:00Z
tags: ["tone", "filter", "realtime"]
resource: "https://www.dsprelated.com/freebooks/filters/Low_High_Shelving_Filters.html"
---

# Tone

The proposed tone module balances bass against brightness rather than removing
bandwidth like the [output filters](output_section.md). It is isolated from the
production chain until X5. Tone is linear in dB, -6 to +6, default zero; bypass
defaults false. Positive tone means low attenuation and high boost. A first-order
tilt avoids the additional order of linked biquads and the asymmetric overall
level shift of a high shelf alone. Boost requires headroom; no limiter or loudness
normalization is applied. Listening gain matching belongs to the harness.

## Transfer and realization

Julius O. Smith III's linked shelving section, read 2026-11-03, provides background
on first-order shelves and bilinear transformation. The reciprocal tilt below is
a project derivation: g=10^(toneDb/20), K=tan(pi*1000/Fs),
H(z)=((g+K)+(K-g)z^-1)/((1+g*K)+(g*K-1)z^-1).
DC gain is 1/g, Nyquist gain g, and magnitude at the fixed 1 kHz pivot is unity.
The finite audible band approaches shelf endpoints; it need not reach them.
Pivot phase is 2*atan(g)-pi/2. Opposite settings have reciprocal static transfers.
The pivot stays strictly below Nyquist at every supported rate, including 8 kHz.

Use a topology-preserving lowpass state per channel:

```text
q = g*K/(1+g*K)
v = q*(x-state)
low = v+state
state = low+v
wet = g*x+(1/g-g)*low
```

Throughout 8â€“384 kHz and the tone range, 0<q<0.5. The update is a convex
combination of old state and input, so state and low remain bounded by the input
envelope from reset, even under automation. Wet amplitude is bounded by
(g+abs(1/g-g))*X <3.49X. Explicit dry output at g=1 provides exact neutral even
after earlier shaping, while lowpass state keeps following input. Bypass blends
dry/wet and preserves state updates. Zero reported compensating latency does not
imply zero phase or group delay.

Tone ramps linearly in dB over 10 ms; bypass ramps linearly in blend over 10 ms.
Advance once per frame, preserve identical-target ramp progress and cache settled
coefficients. Reset clears double state and snaps to retained targets. Follow the
[module contract](https://github.com/LordZordack/DisDorktion/blob/main/docs/DSP_CONTRACT.md) for transactional validation, mono/stereo,
chunking, empty blocks and preparation. Only prepare may allocate. Nominal input
is [-1,1], with finite [-16,16] qualification. Suppress denormals in processing.

## Acceptance established before implementation

Impulse DFT (ceil(0.1 Fs) samples), sweep and sine checks compare an independently
evaluated complex transfer across 20â€“min(20000,0.45 Fs) Hz: <=0.05 dB magnitude
and <=1e-5 absolute complex error. Verify truncation error, DC/Nyquist gains,
pivot phase and reciprocal settings at -6/-3/0/+3/+6 dB. Neutral/bypass are exact
dry; reset/partition error <=1e-6. Tail after two seconds <1e-6.

Test mono/stereo at 44.1/48/88.2/96/192 kHz plus 8/384 kHz boundaries; blocks
1/32/64/127/256/1024, empty, changing and oversized. Include startup, invalid
transaction preservation, channel independence, repeated targets, full-range
automation and neutral/bypass exits. Nominal automation peak <8; wider envelope
must stay finite. Allocation guards include controls/reset/process.

Proposed p99 ceiling <2% of incoming callback deadline at 48/96/192 kHz stereo,
capacities 32/64/127/256/1024, including control application and dispatch.
Three pinned AC Release runs and independent raw audit are required; quiet-decay
and automation are included. Diagnostics do not establish reference acceptance.
Matched-level guitar, bass, percussion and mix listening requires human verdicts
with source/settings provenance. Dependency listening and GUI gates remain open.

## Production integration (issue 20)

This module is integrated into the native-rate [Output section](output_section.md)
in the fixed comb -> dispersion -> tone -> high-pass -> low-pass order.
It defaults to bypass for Initialize, existing factory sounds and old sessions;
its histories remain warm. The isolated design and limits above remain module
contracts, not a universal peak bound for cascaded resonant inputs. See
[expanded-chain qualification](../notes/expanded_output_validation.md) for
combined-chain stress, state/automation and conditional CPU acceptance.
