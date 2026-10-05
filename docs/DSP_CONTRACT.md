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

## Plugin adapter policy

The adapter marks host configuration ready only after successful preparation. Invalid host preparation disables processing even if the module retained an earlier valid configuration. On unprepared or rejected audio processing, clear every output sample with `buffer.clear()` and return without logging, allocation, or host/UI notifications. Clear output-only channels before processing valid input. Rejected parameter updates retain valid controls and do not cause silence.

After successful `prepareToPlay`, synchronize zero module latency with `setLatencySamples(0)`. Do not notify latency changes from `processBlock`.

Host lifecycle and reset calls are serialized with processing: prepare before playback, release after playback stops, and reset on the processing thread or while stopped. An atomic ready flag alone would not make concurrent changes to DSP state safe. Valid host buffers may contain surplus channel storage; process a non-owning view of the active output channels and clear surplus channels. Too few channels reject the block. Adapter reset snapshots valid host controls before snapping the module to their targets. State restoration updates host parameters; a subsequent callback ramps to them, or reset/preparation snaps to them. Persistence may allocate and notify the host and must run outside the module realtime path.
