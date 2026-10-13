# DSP module contract

This document defines the interface and processing behavior for the reference gain foundation. Validation observations are recorded separately in the [foundation validation note](okf/notes/foundation_validation.md). The first implementation milestone is **F1, gain foundation**. **F2, audition integration**, follows later.

## Interface

Prepare modules with `juce::dsp::ProcessSpec` and process planar audio through a non-owning `juce::dsp::AudioBlock<float>`. A common header documents signatures and compile-time requirements. It must not force unrelated modules into one universal parameter structure or inheritance tree.

```cpp
bool prepare(const juce::dsp::ProcessSpec& spec);
void reset() noexcept;
bool setParameters(const TypedParameters& parameters) noexcept;
bool process(juce::dsp::AudioBlock<float> block) noexcept;
unsigned int getLatencySamples() const noexcept;
```

Each module supplies its own typed parameter structure. Preparation accepts a finite sample rate in `[8000, 384000]` Hz, block capacity in `[1, 65536]`, and exactly one or two channels. A failed preparation preserves an existing valid preparation; a new module stays unprepared. Successful preparation establishes required storage and resets history against retained parameter targets.

## Q1 preparation at the internal rate

`BlockDispatcher`, `TanhSaturation`, and `WaveFolder` additionally expose
`prepareOversampled(hostSpec, factor)`. Validate the host specification against
the domain above and a power-of-two factor in 1–16, then translate rate and
capacity to the internal domain (up to 6.144 MHz and 1,048,576 frames). Do not
validate the translated specification as a host specification. Native `prepare`
keeps its original domain. Invalid requests preserve the previous preparation
and smoothers; successful preparation retains parameter targets and uses the
actual internal rate for 10 ms ramps.

The Q1 `MiddleSection` evaluation boundary exposes the usual host module
interface plus stopped-processing factor, latency-correction and folding-mode
configuration. It splits host blocks before upsampling and keeps filtering
active in every individual module-bypass state. It reports integer host latency.
Nominal inputs are [-1,1]; the resampling/correction filters can ring or overshoot,
so arbitrary finite float extremes are not promised to remain finite.

The separate `AdaaWaveFolder` is an internal evaluation processor, not a host
`Module`: its nominal linear/bypass delay is fractional (1.5 internal samples).
The surrounding boundary corrects and reports host latency. Native WaveFolder
transfer/bypass behavior remains available separately. See the
[Q1 design](okf/methods/middle_oversampling.md) for controls, correction and
open quality/performance gates.

## Storage and thread use

Only `prepare` may allocate. Audio blocks are caller-owned views of valid writable storage; modules do not resize or own audio. Stereo channels must point to separate sample storage. Future modules that need scratch audio or control storage allocate it during preparation, up to the validated capacity. Do not use block-sized stack arrays in callbacks.

Parameter changes and reset run on the processing thread or while processing is stopped. The adapter transports controls using cached lock-free atomic reads at the block boundary. Module methods are not concurrently callable. Check that supported atomic types are lock-free on the target.

Invalid parameter updates return `false` and preserve the entire previous parameter set. Each module documents parameter units, valid bounds, defaults, and transitions. Processing, reset, and parameter application must not allocate, throw, block, access files or logging, or call UI code. Denormal suppression is scoped to the audio callback.

## Processing behavior

Unprepared processing or a channel-count mismatch returns `false` without modifying samples or history. A valid zero-length block succeeds without changing history or advancing ramps.

For valid nonempty blocks, preserve channel count and sample count. Split an oversized block into successive non-owning sub-blocks no larger than the prepared maximum. Do not allocate, truncate, or pad. Cost is proportional to supplied audio, and work within each chunk is bounded by prepared capacity.

DSP histories are independent for each channel and module instance. Controls may be shared across channels. Advance smoothing once per sample frame so stereo channels receive the same control value. Reset deterministically clears audio history and sets smoothers immediately to retained targets. Latency is an integer sample count independent of incoming block size; the reference processor reports zero.

Audio samples are finite floating-point values within the documented operating envelope of each module. Invalid control values are explicitly handled. Arbitrary NaN/Inf audio sanitization is outside this reference module contract.

For reference gain, finite input magnitude must be at most `std::numeric_limits<float>::max() / 4` to keep the full gain range finite. Nominal audio in `[-1, 1]` is supported without clipping; gain may produce output up to magnitude 4. No limiter or normalization is applied.

## Reference gain behavior

The gain parameter is linear amplitude in `[0, 4]`, with default `1`. Bypass defaults to `false`. Reject nonfinite or out-of-range gain values. Use a 10 ms linear gain ramp and a 10 ms bypass blend. Let `b` be bypass blend (`0` means effect, `1` means bypass); output is:

```text
output = input * (b + (1 - b) * gain)
```

Both smoothers advance once per frame and remain continuous across block and chunk boundaries. Reset snaps them to retained targets. This is a foundation reference; M1 decides production gain controls and ranges.

After configuring ramp duration, preparation and reset explicitly set the current and target values:

```cpp
gainSmoother.setCurrentAndTargetValue(retainedGain);
bypassSmoother.setCurrentAndTargetValue(retainedBypass ? 1.0f : 0.0f);
```

At every supported sample rate, the initial unity block preserves every sample; there is no startup fade. Define a 10 ms ramp as `floor(sampleRate * 0.010)` frames. When a target changes, advance before applying the first frame, and reach the target on the final ramp frame.

## Production gain (M1)

`Gain` implements the same module interface with `GainSettings { gainDb, mute,
bypass }`. Finite gain is inclusive [-60,+24] dB, default 0; booleans default
false. Invalid settings retain the complete previous set. Conversion is
`10^(gainDb/20)`, cached in double precision. Gain and bypass use double linear
envelopes over `floor(sampleRate * 0.010)` frames, advancing once per frame.
Mute ramps gain to exact zero; dB changes while muted are retained for unmute.
Bypass takes precedence over module mute and returns dry unity. Both envelopes
keep advancing when bypassed. Reset/preparation snaps to retained controls.

The single-instance finite input envelope is float maximum /16; a production
pair uses /256; the plugin pair plus legacy reference trim uses /1024. Nominal
audio is not clipped or normalized. Each instance owns its own state and reports
zero latency. [Design, sources, alternatives and tolerances](okf/methods/gain.md)
were recorded before implementation.

The plugin processes legacy reference trim, production input gain, then production
output gain. First two parameter IDs and linear meanings (`referenceGain`,
`referenceBypass`) are preserved. New IDs are `inputGainDb`, `inputMute`,
`inputBypass`, `outputGainDb`, `outputMute`, `outputBypass`. Version-1 XML restores
the legacy stage and resets production controls to unity/unmuted/unbypassed;
version-2 XML requires all eight values. All state fields are strictly parsed and
validated before publication. Host normalized-float transport retains its existing
precision limits, including underflow for the smallest subnormal legacy gains.
State operations and bulk harness restoration require lifecycle synchronization;
independent live parameter atomics do not promise an atomic whole-state snapshot.

## Production bit crusher

`BitCrusher` follows the module interface above with float32 audio and int64
quantization codes. Depth accepts 1–8, 16, 32, or 64 bits (default 64); signed
rounding is nearest with ties away from zero, saturated to
`[-1, 1 - 2^(1-B)]` for depth B. Capture rate accepts 100–384000 Hz and is
capped at host rate (default 384000 Hz). Capture-rate, mix, crush, shape,
jitter, and bypass transitions use 10 ms smoothers; depth changes requantize
the held raw sample on the next frame without resetting phase. Mix defaults to
1, crush/shape/jitter to 0, jitter seed to uint32 1, and bypass to off.

The original PCM result is preserved exactly when crush and shape are zero.
Otherwise, let d0=2^(1-B), d=d0+(1-d0)crush^3, and p=2^(2shape). The enhanced
path shapes x as sign(x)|x|^p, rounds to the nearest d grid with ties away from
zero, clamps to [-1,1], then applies the inverse power. Blend that result with
the legacy PCM result using w=max(crush,|shape|). This keeps neutral controls
on the legacy path and makes the transition continuous at neutrality. The
enhanced grid is symmetric and may have saturation endpoints off-grid; it does
not promise exactly 2^B codes. Mix linearly blends dry input and wet output
with exact endpoints; bypass independently overrides mix with exact dry output.

Jitter uses a deterministic xorshift32 generator (seed default 1; seed 0 maps
to a fixed nonzero state), one shared random value per capture interval, and a
compensated clock. Map each PRNG value r to u=(r+0.5)/2^31-1 in [-1,1).
At each frame, its threshold is hostRate + u*0.9*smoothedJitter*
(hostRate-smoothedEffectiveRate),
where u is uniform in [-1,1). It is bounded below by the effective rate,
fades to zero at full rate, and permits at most one capture per host frame.
Near full rate jitter therefore has less effect; with jitter zero the original
fixed clock is unchanged. Mix/crush/shape/jitter use double linear smoothers,
advance once per frame, and snap to retained targets on reset. Channels retain
independent holds on a shared capture clock. The first frame captures
immediately and phase persists across blocks. Reset clears histories and clock,
snaps smoothers to retained targets, and reseeds the PRNG. A seed change
reseeds on the next frame without resetting capture phase. Latency is zero.

See the [bit crushing method](okf/methods/bit_crushing.md) and its
[validation note](okf/notes/bit_crusher_validation.md).

## Input section (S1)

`InputSection` implements the host module interface with
`InputSettings { GainSettings gain; BitCrusherSettings crusher; }`. It validates
the entire specification/settings pair before child mutation, then processes
each capacity-bounded chunk through Gain followed by BitCrusher. Both children
retain their original parameter, transition, bypass and clock semantics.
Crusher dry/mix input is post-gain; gain mute can leave a captured sample held
until the next capture. Native rate, zero latency, no normalization/limiter,
finite input envelope float maximum /16. Rejected processing and valid empty
blocks do not alter state; reset clears both histories and interstage peak.

`getPostGainPeak()` is an exclusive processing-thread getter for the last
successful nonempty call, aggregated across chunks. The shared harness publishes
its own atomic copy and latches overload before crusher clamping can hide it.
The harness-only common bypass forces effective child bypasses while retaining
their separate selections and continuing their processing. Version-8 records
preserve this state. See [input section design](okf/methods/input_section.md).

## Tanh saturation

`TanhSaturation` uses `TanhSaturationSettings { driveDb, outputDb, bypass }`.
Drive is finite [-24,+36] dB and wet output trim is finite [-60,+12] dB;
both default to 0 dB and bypass defaults off. Settled wet output is
`10^(outputDb/20) * tanh(10^(driveDb/20) * input)`. Default drive therefore
applies `tanh(input)`. Output is bounded by the wet trim gain, which may exceed
nominal unity. All finite float inputs are supported using double intermediates.
Drive/output gain and bypass use double linear-amplitude 10 ms ramps, advanced
once per frame. Identical targets preserve ramp progress. Reset/preparation
snaps to retained targets. Bypass excludes drive and trim, and settled bypass
returns the original sample exactly. Channels have no audio history and share
frame controls; instances have independent envelopes. Latency is zero.

The harness selects `tanh-saturation`, with version-5 experiment records storing
drive and trim explicitly. Native-rate aliasing is measured for Q1; the module
does not own the future shared oversampling boundary. See the
[design and acceptance limits](okf/methods/tanh_saturation.md).

## Plugin adapter policy (both gain implementations)

The adapter marks host configuration ready only after successful preparation. Invalid host preparation disables processing even if the module retained an earlier valid configuration. On unprepared or rejected audio processing, clear every output sample with `buffer.clear()` and return without logging, allocation, or host/UI notifications. Clear output-only channels before processing valid input. Rejected parameter updates retain valid controls and do not cause silence.

After successful `prepareToPlay`, synchronize zero module latency with `setLatencySamples(0)`. Do not notify latency changes from `processBlock`.

Host lifecycle and reset calls are serialized with processing: prepare before playback, release after playback stops, and reset on the processing thread or while stopped. An atomic ready flag alone would not make concurrent changes to DSP state safe. Valid host buffers may contain surplus channel storage; process a non-owning view of the active output channels and clear surplus channels. Too few channels reject the block. Adapter reset snapshots valid host controls before snapping the module to their targets. State restoration updates host parameters; a subsequent callback ramps to them, or reset/preparation snaps to them. Persistence may allocate and notify the host and must run outside the module realtime path.
