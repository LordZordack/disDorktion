# DisDorktion

[Overarching project plan](docs/PROJECT_PLAN.md) · [Published GitHub tickets](docs/TICKET_DRAFTS.md)

## Build and DSP references

- [Build commands, toolchains, and dependency pins](docs/BUILDING.md)
- [DSP processing contract](docs/DSP_CONTRACT.md)
- [Audition application, experiment records, and offline rendering](docs/AUDITION_HARNESS.md)
- [Generated experiment examples](examples/experiments/README.md)
- [Proposed performance budgets and benchmark procedure](docs/PERFORMANCE.md)
- [Production gain design and acceptance limits](docs/okf/methods/gain.md)

### Launch the Release audition app

From a PowerShell terminal in the project root, build and launch:

```powershell
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-release --target DisDorktionAudition --parallel 4
& ".\build\windows-vs2026\DisDorktionAudition_artefacts\Release\DisDorktion Audition.exe"
```

After building, use just the last command to launch again, or double-click
`DisDorktion Audition.exe` in that `Release` folder. Use the Release executable
to audition the latest Release build; a previously built Debug executable can
still contain older module options. In the module menu, select **High-pass** or
**Low-pass**, then choose Butterworth, Chebyshev I, or Chebyshev II under **Response**.

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

The shared **Wave folder** target adds a symmetric triangle transfer with
-24 to +36 dB drive, a 0.05 to 1 fold threshold, and -60 to +12 dB output trim.
Select it in the audition application. Lower thresholds fold sooner and lower
the wet output ceiling; default settings preserve inputs within [-1, 1].
Controls and bypass ramp over 10 ms, with zero module latency. Version-6
records retain its settings. See the [design](docs/okf/methods/wave_folding.md)
and [validation evidence](docs/okf/notes/wave_folder_validation.md).

The independent **High-pass** and **Low-pass** audition targets offer Butterworth,
Chebyshev I (1 dB passband ripple), and Chebyshev II (40 dB stopband attenuation)
responses. Cutoff is 20–20000 Hz and consistently denotes the half-power point;
the effective value is capped at 45% of sample rate. Cutoff, response and bypass
transitions take 10 ms. Version-7 experiment records preserve settings; both
modules report zero added latency. See the [high-pass design](docs/okf/methods/high_pass_filter.md),
[low-pass design](docs/okf/methods/low_pass_filter.md), and their
[high-pass](docs/okf/notes/high_pass_validation.md) /
[low-pass](docs/okf/notes/low_pass_validation.md) evidence. These targets prepare
the output filters for later section integration.

**Design Ethos**

Q1's offline **middle-section comparison** tool evaluates native, 2x, 4x, 8x
and 16x FIR tanh/folding processing. It saves spectra, filter phase and blend
responses, diagnostic CPU timings, and aligned RMS-matched listening files.
The evaluated 4x FIR latency correction reports 92 samples.
The `--adaa` option evaluates cleaner triangle folding with the same 92-sample
4x latency. Representative alias suppression improves, while extreme-setting
residuals and changed character keep approval open. The equivalent FIR layout
optimization passes the provisional local CPU budget in two runs; host timing
and human listening remain outstanding. A separate listening tool renders
matched comparisons from a user-supplied WAV with gentle and stress settings.
See the [design](docs/okf/methods/middle_oversampling.md),
[evidence](docs/okf/notes/middle_oversampling_validation.md), and
[commands](docs/AUDITION_HARNESS.md#middle-section-quality-comparisons-q1).

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
