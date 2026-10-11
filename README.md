# DisDorktion

[Overarching project plan](docs/PROJECT_PLAN.md) · [Published GitHub tickets](docs/TICKET_DRAFTS.md)

## Build and DSP references

- [Build commands, toolchains, and dependency pins](docs/BUILDING.md)
- [DSP processing contract](docs/DSP_CONTRACT.md)
- [Audition application, experiment records, and offline rendering](docs/AUDITION_HARNESS.md)
- [Generated experiment examples](examples/experiments/README.md)
- [Proposed performance budgets and benchmark procedure](docs/PERFORMANCE.md)
- [Production gain design and acceptance limits](docs/okf/methods/gain.md)

The reusable gain module supports -60 to +24 dB, an explicit mute, and 10 ms
smoothed gain/bypass changes. Select **Gain (dB)** in the audition app; module
mute and monitoring mute are separate controls. The plugin has independent
input/output gain instances. Its original linear reference controls remain as
a legacy trim so existing host automation and saved states keep their meaning.
See the [gain validation note](docs/okf/notes/gain_validation.md) for recorded test results and remaining listening
and performance qualification gates.

The shared **Bit Crusher** target adds signed PCM quantization and sample
holding, with independent channel holds and a shared capture clock. Select it
in the audition app to adjust bit depth, capture rate, mix, crush, shape, jitter,
seed, and bypass. The capture-rate slider is logarithmic; its text field stores
requested Hz independently from the host-capped effective rate. New version-4
records expose the expressive controls while version 3 retains legacy behavior.
The design is documented in the [bit crushing method](docs/okf/methods/bit_crushing.md).
The prior 86-test suites and CPU baseline validate the legacy algorithm only.
The expressive Debug/Release/headless suites, render checks, and three-run CPU
baseline pass. New listening, GUI inspection, VS2022 CI, and the graph gate
remain pending. See the [validation note](docs/okf/notes/bit_crusher_validation.md).

The shared **Tanh saturation** target provides -24 to +36 dB drive and an
independent -60 to +12 dB output trim. Select it in the audition application;
the transfer is `outputGain * tanh(driveGain * input)`, with 10 ms control and
bypass ramps and zero module latency. Version-5 experiment records preserve
these settings. Native-rate measurements provide comparison material for Q1's
shared middle-section anti-aliasing work. See the
[design](docs/okf/methods/tanh_saturation.md) and
[validation evidence](docs/okf/notes/tanh_saturation_validation.md).

**Design Ethos**
Design modularily. Each state (demodulation, bit crush, tanh, etc.) should be its own separate module that can be chained together to output the final result. (In series, though maybe later in parallel can be experimented with)

To test each function, a simple GUI either with prerecorded looped input or simple keyboard MIDI sinwave synth input should be used. Settings for the given module should be modifiable, and there should be a way to control output volume. This way, each module is able to be a sort of "standalone plugin". (but more importantly, it allows for rapid testing)

**Three-Stage Distortion Plugin Techniques**
<hr>
Input Stage: Receiving the signal

- input gain
- Demodulation: modulate the signal to a higher frequency, then peform demodulation. Allow user to change phase of demodulation oscillator to provide distortion that might be found in radio. 
- Bit Crushing: Reduces the bit depth and sample rate to create a Crunchy, aliased character at the input source. Adds digital distortion by lowering quality dynamically.
    - Select number of bits
    - Select sample rate


Middle Stage: Processing the signal
- Hyperbolic Tangent (tanh): A soft clipping algorithm that simulates analog saturation and generates natural harmonic generation.

- Wave Folding: Maps signal amplitudes back towards zero beyond a certain threshold, creating rich, complex harmonics and aggressive distortion profiles based on the non-linear folding function.

Output Stage: Outputting the signal
- Resonant Comb Filter: Mixes the input with a delayed version of itself, creating a series of evenly spaced resonances and notches. This colors the sound in a hollow, metallic, or phasey way that highlights specific frequencies.

- Frequency Warping: Uses an all-pass filter in a feedback network to introduce frequency-dependent time delays. This creates non-linear phase distortion that bends and stretches the harmonic structure without changing the fundamental pitches themselves.

- Low pass
- High pass
- output gain
- tone

**Suggested Resources for Further Study:**
- "Digital Audio Signal Processing" and "Digital Audio FX" by Udo Zoelzer: These textbooks provide foundational mathematical and theoretical bases for digital signal processing, covering sampling, quantization, non-linear processing like waveshaping, and delay-based effects including filters.
- "Introduction to Digital Filters with Audio Applications" by Julius O. Smith III: An excellent resource for understanding digital filters, offering an intuitive approach with the mathematics that underlie them.
- "Physical Audio Signal Processing" by Julius O. Smith III: A comprehensive guide covering physical modeling of analog audio effects and sound synthesis.
Key Research Articles & Academic Papers:
- "Oversampling for Nonlinear Waveshaping: Choosing the Right Filters": Discusses classic nonlinear signal processing for wave and phase shaping, specifically addressing aliasing reduction and computational considerations.
- "Digital Emulation of Distortion Effects by Wave and Phase Signal Shaping": Explores wave and phase signal shaping techniques for the digital emulation of distortion effects using super-resolution frequency-domain analysis.
- "Modulation and Delay Line Based Digital Audio Effects": Examines the classification and technical implementation of audio effects based on delay lines, such as flanging and chorus, alongside modulation techniques.
- "Spectral Delay Filters": Investigates the use of all-pass filters in cascade to introduce frequency-dependent time delays beneficial for audio effects processing.
