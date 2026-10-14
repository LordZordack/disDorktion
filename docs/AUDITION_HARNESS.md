# Audition and measurement harness

## Output section

Select **Output section** for high-pass followed by low-pass at the device rate.
Set each stage's cutoff, response and bypass independently. Common **Bypass**
temporarily bypasses both stages and retains their choices. All response banks
stay warm. Cutoffs range from 20 to 20000 Hz; processing caps each to 45% of the
device rate, while preserving requested Hz in the record. Equal/crossed values
are valid and may strongly attenuate the source. Default wide filtering is not
exact unity; full settled bypass is exact dry.

Version 10 requires `targetId: output-section`, `hpCutoffHz`, `lpCutoffHz`,
`hpResponse`, `lpResponse`, `hpBypass`, `lpBypass` plus common `bypass` and the
existing source/monitor/build fields. Response IDs are `butterworth`,
`chebyshev-1`, `chebyshev-2`. Missing/foreign fields, numeric booleans and
unknown modes reject transactionally. Versions 1-9 preserve their target and
version. Offline/live processing uses the same target; offline monitoring is
disabled, requested frame count is preserved and tails are not appended.

```powershell
& './build/windows-vs2026/Release/disdorktion_render.exe' --experiment './examples/experiments/output-section-wide.json' --output-dir './renders/output-wide-001'
& './build/windows-vs2026/Release/disdorktion_output_measurements.exe' './renders/output-measurements-001'
uv run --no-project --python 3.12 scripts/validate_output_measurements.py './renders/output-measurements-001' --repeat './renders/output-measurements-repeat'
```

The repeat path requires a second measurement run into a new destination.
Measurements include complex magnitude/phase, independent cascade response,
control/bypass stability, inter-stage headroom and zero algorithmic latency.
See the [design](okf/methods/output_section.md) and
[qualification note](okf/notes/output_section_validation.md) for listening and
CPU evidence status.

## Middle section candidate

Select **Middle section (candidate)** for fixed 4x FIR processing of tanh then
ADAA triangle folding. Set tanh and folder drive/trim independently, and set
the fold threshold. Each stage has a bypass; common **Bypass** retains both
selections and keeps the filtered 92-sample path active. The tanh trim controls
the level entering the folder. Quality selection is fixed during this
qualification; isolated Tanh/Wave folder targets retain their native algorithms.

Version-9 records include every stage control and `middleQuality: 4x-fir-adaa-v1`.
Loading restores them while callbacks are detached. Unsupported quality IDs
are rejected. Record versions 1-8 preserve their existing targets.

Shared offline renders save exactly the requested number of frames, including
the processing lead-in, without automatic latency trimming or tail padding.
Their results expose `latencySamples: 92`,
`renderPolicy: fixed-duration-uncompensated` and `tailFlushed: false`.
For generated sources, choose more render frames than source frames to capture
ring-down. File sources loop instead of appending silence. Q1's separate
listening tool aligns and level-matches comparisons and has its own duration
policy. Human sound and extreme-alias acceptance remain open; see the
[section design](okf/methods/middle_section.md) and
[evidence](okf/notes/middle_section_validation.md).

```powershell
& './build/windows-vs2026/Release/disdorktion_render.exe' --experiment './examples/experiments/middle-section-mild.json' --output-dir './renders/middle-mild-001'
```

Create the parent `renders` directory first and choose a fresh output directory.

## High-pass and low-pass targets

Select **High-pass** or **Low-pass**, then set **Cutoff (Hz)** and **Response**.
Both use second-order filters: Butterworth is flat; Chebyshev I has 1 dB
passband ripple; Chebyshev II has finite stopband zeros and a 40 dB stopband
floor. Cutoff always means half power (−3.0103 dB). Defaults are 20 Hz high-pass,
20000 Hz low-pass, Butterworth, and bypass off. Requested cutoff stays in
20–20000 Hz; the display reports its effective cap at `0.45 * sampleRate`.
Changing the audio device preserves the requested cutoff and live mode.

Cutoff, response selection, and bypass have 10 ms transitions. Filter histories
keep running during bypass; fully bypassed samples are exact dry. Latency is
zero added samples, with the usual IIR phase response. Monitoring volume is
applied after measurement and never changes offline renders. The filters have
independent targets and controls; output-section/plugin integration is a later ticket.

Version 7 requires targetId `high-pass` or `low-pass`, finite `cutoffHz` in
[20,20000], and string `response` (`butterworth`, `chebyshev-1`, `chebyshev-2`),
plus existing bypass/source/monitor fields. Invalid or mixed target schemas
reject without changing the prior record; versions 1–6 retain their meaning.

Load [high-pass sweep](../examples/experiments/high-pass-sweep.json) or
[low-pass sweep](../examples/experiments/low-pass-sweep.json), switch responses,
and save settings for comparison. Generate all six source/wet listening pairs
and their exact metadata in a fresh directory:

```powershell
./build/windows-vs2026/Release/disdorktion_cutoff_filter_measurements.exe cutoff-filter-measurements
```

The tool records independent impulse and stepped-sine checks and CPU diagnostics.
Listen at −18 dB monitoring, compare modes/bypass, and also use transient/music
material. Human listening results remain pending in the separate
[high-pass](okf/notes/high_pass_validation.md) and
[low-pass](okf/notes/low_pass_validation.md) notes.

## Production gain target

Select **Gain (dB)** to audition the same production `Gain` implementation used
by the plugin's independent input/output instances. Gain is -60 to +24 dB,
default 0 dB. **Mute module** ramps module output to zero; **Mute monitoring**
only affects live playback after the meters. Bypass returns dry unity, including
when module mute is selected. Gain/mute/bypass transitions use 10 ms envelopes;
latency is zero. Both target changes and record restoration detach the callback.

The **Reference gain** target retains the foundation's 0-4 linear controls.
Restoring records preserves continuous gain values without slider quantization.
New production records use version 2, `targetId: "gain"`, required finite
`gainDb` in [-60,+24], boolean `mute` and `bypass`, and the existing source and
monitor metadata. They omit the linear `gain` field. Version-1 records remain
`reference-gain` with linear `gain`; mixed units/versions are rejected before
restoration. Monitor mute retains its field name `muted`. Save/load/render tests
cover both schemas and bound validation before conversion to float.

Use [gain-sine.json](../examples/experiments/gain-sine.json) or
[gain-mute.json](../examples/experiments/gain-mute.json) with the renderer commands
below. The remaining foundation descriptions apply to the legacy target.

Generate deterministic four-second sine/transient gain/mute/bypass fixtures into
a fresh directory:

```powershell
./build/windows-vs2026/Release/disdorktion_gain_transitions.exe --output-dir gain-transition-listening
```

The generator refuses existing destinations and writes float WAVs plus exact
settings/provenance. The checked-in [settings](measurements/gain_transition_fixtures/settings.json)
and [gain validation](okf/notes/gain_validation.md) record numerical checks;
generated WAVs stay local and listening observations must be recorded separately.

The dedicated **DisDorktion Audition** app auditions the same DSP targets used
by the plugin. It supports `reference-gain`, production `gain`, and
`bit-crusher`, and `tanh-saturation`; the generated plugin Standalone wrapper, `DisDorktion.exe`,
has a separate purpose and does not supply this experiment workflow.

## Tanh saturation target

Select **Tanh saturation** for Drive (dB), Output trim (dB), and bypass.
Drive spans -24 to +36 dB, trim spans -60 to +12 dB, and both default to 0.
Default drive applies tanh coloration. The module has 10 ms smoothers and zero
latency; settled bypass returns dry input and excludes trim. Monitoring volume
and mute remain independent and never alter offline measurements.

Version-5 records require `targetId: "tanh-saturation"`, finite `driveDb` and
`outputDb`, boolean `bypass`, and the existing source/monitor metadata. Gain
and crusher controls retain neutral defaults and mixed target settings are
rejected. Versions 1–4 retain their existing semantics. Use the `tanh-*.json`
examples or save an experiment from the app. Native-rate aliasing is baseline
material for Q1. See [the design](okf/methods/tanh_saturation.md) and
[validation](okf/notes/tanh_saturation_validation.md).

## Bit Crusher target

Select **Bit Crusher** to audition the shared `BitCrusher` module. Controls
include depth (1–8, 16, 32, or 64 bits), requested capture rate, mix, crush,
shape, jitter, jitter seed, and bypass. Mix/crush/shape/jitter default to
1/0/0/0; the uint32 seed defaults to 1. New version-4 records require
`bitDepth`, `captureRateHz`, `bypass`, `mix`, `crush`, `shape`, `jitter`,
and `jitterSeed`. Version 3 remains readable with neutral extension settings;
version-3 extension keys are rejected. New saves upgrade to v4. Version 1/2
schemas remain unchanged and also reject extension keys.

The rate slider maps logarithmically from 100 Hz to the host rate. Its text
field retains requested Hz from 100 to 384000, even when that request exceeds
the current host rate; effective rate is capped without rewriting the stored
request. Device-rate changes remap the control without changing its value. A
pre-device rate mapping uses 48000 Hz. The renderer uses the same target and
settings as the app. See the [bit crusher examples](../examples/experiments/README.md)
and [validation note](okf/notes/bit_crusher_validation.md). Expressive
Debug/Release/headless suites and render checks pass; extension CPU qualification,
new-WAV listening, and manual GUI restore/save inspection remain pending.

## Input section

Select **Input section** to run production input gain followed by the expressive
bit crusher. Both control groups remain available. **Bypass gain** and
**Bypass crusher** retain individual selections; common **Bypass** forces both
temporarily. **Mute module** mutes gain; a crusher hold may persist until the
next capture. Monitor mute/volume remain independent of rendered samples.

The meter adds **Post-gain** peak. Its overload indicator latches interstage
overshoot even when the fully wet crusher clamps final output below unity.
Version-8 records contain gain/mute, both child bypasses, all crusher controls
including exact uint32 seed, and common bypass. Older record schemas remain
supported for their respective targets. Section processing reports zero latency.

The committed `examples/experiments/input-section-overload.json` reproduces a
generated overload fixture through the existing renderer:

```powershell
./build/windows-vs2026/Release/disdorktion_render.exe --experiment examples/experiments/input-section-overload.json --output-dir input-overload-render
```

The destination must be fresh. Its `result.json` includes `postGainPeak` and
marks overload at either the post-gain or final-output stage. See the
[section design](okf/methods/input_section.md) and
[qualification evidence](okf/notes/input_section_validation.md).

For eight-second gain/crusher interaction schedules, generate source/post-gain/
wet/sequential-reference float WAVs and independently validate the results:

```powershell
./build/windows-vs2026/Release/disdorktion_input_measurements.exe input-section-measurements
uv run --no-project --python 3.12 scripts/validate_input_measurements.py input-section-measurements
```

The fresh destination holds five synthetic fixtures at 48/96 kHz, event settings,
peak/RMS/overload counts, exact reference errors and spectral observations.
Float WAVs preserve values above unity; no normalization is applied. Generated
files provide reproducible listening material, while human observations are
recorded separately in the qualification note.

## Start an audition

Build with the [full Windows preset](BUILDING.md), then launch:

```powershell
& '.\build\windows-vs2026\DisDorktionAudition_artefacts\Debug\DisDorktion Audition.exe'
```

Use `Release` for the Release artifact. Open **Audio / MIDI devices** to choose a working mono/stereo output, sample rate, block size, and MIDI input. The app shows the active specification. It supports 8,000–384,000 Hz, one or two channels, and prepared block sizes 1–65,536 frames.

1. Select a generated source, **Audio file loop**, or **Keyboard / MIDI sine**. Load WAV/AIFF with the file button when using a loop.
2. Set frequencies in Hz, peak amplitude, phase in radians, **Source frames**, **Render frames**, and noise seed. Click **Apply source** to commit the editors. Selecting another source also attempts to apply its settings. Source application stops playback; press **Play** afterward.
3. Monitoring starts muted at −18 dB. Set monitoring volume (−60–0 dB) and unmute to listen. Supported controls for the selected target update live.
4. **Stop** stops playback and releases notes. **Restart** resets source and target history. Generated sources stop automatically at their source length and the button returns to **Play**; press Play to repeat them from the beginning. File loops continue until stopped.
5. Enter observations and save an experiment JSON. Reloading restores a validated record stopped. The device sample rate, channel count, and block size must match the record; otherwise restore reports the mismatch and refuses it. Match the device settings before retrying.

Save and Render use the **last applied source and frame counts**, together with current target settings, monitoring, and observations. Typing into a source editor does not commit it. Click Apply source before saving or rendering changed source settings. Worker activity disables source/settings, selected target controls, observations, and save/restore/render controls until completion; monitoring remains available.

## Signal and measurement semantics

The path is source → input measurements → module → output measurements/render tap → smoothed monitoring gain → audio device. Monitor mute/volume cannot change pre-monitor measurements or offline audio. There is no normalization or limiter. Input/output peak and RMS are linear amplitude readings; live meters describe the latest processed block across channels. Output magnitude above 1 latches overload until UI consumption; the interface keeps the warning visible for two seconds after an overload. Float output can contain values above full scale.

| Source | Behavior |
| --- | --- |
| Impulse | Specified amplitude at sample zero, then zero |
| Sine | `amplitude * sin(phase + 2*pi*frequency*n/sampleRate)` |
| Two-tone | Two sines using the same phase, each at half the specified amplitude |
| Logarithmic sweep | Increasing frequency law from frequency to frequency2, reaching the endpoint at source sample `N-1`; at least two samples |
| Seeded white noise | Unsigned 32-bit LCG: `state = state*1664525 + 1013904223` modulo 2^32; `(state >> 8)/8388608 - 1`, scaled by amplitude |
| Audio file loop | Resident converted PCM wraps exactly at its length, without a crossfade |
| Keyboard / MIDI sine | Monophonic, last held note priority; releasing it returns to the preceding held note |

Generated channels carry identical signals. Reset restarts the sample index and seed, so arbitrary block partitions preserve source samples. Both frequency fields must be finite and strictly between zero and Nyquist, including fields unused by the selected source. Amplitude is 0–1; phase is any finite number.

Keyboard input uses the on-screen JUCE keyboard and its computer-keyboard mapping while focused, plus enabled hardware MIDI inputs. The voice uses MIDI note pitch, velocity, source amplitude, a base peak level of 0.25, and 5 ms attack/release ramps. Frequencies at or above Nyquist are silenced. Stop, focus loss, source changes, and device changes clear notes. A bounded 256-event multiple-producer/single-consumer queue accepts compact note events; producer retries and callback draining are capped. Overflow discards stale events, requests all-notes-off, and shows a notice. Event arrival times use a monotonic clock: playback maps the preceding callback interval into the next block, adding approximately one block of delivery delay. Late events clamp to the first sample; future events remain pending. A saved MIDI record identifies enabled devices/settings and is not a recording of a sample-exact performance. Offline rendering rejects MIDI.

## File loading and reproduction

WAV/AIFF decoding, SHA-256 hashing, and conversion run on a worker. Only resident PCM is read during playback. Completed sources are installed with the callback detached; workers are cancelled/joined on shutdown. Failed loads preserve the last valid source. Device rate/layout changes suspend unavailable file playback and request conversion for the new configuration on the worker.

The shared live/offline conversion policy is `linear;mono-duplicate;stereo-average`: linear interpolation for rate conversion, identical copies for mono-to-stereo, and `(L+R)/2` for stereo-to-mono. **Downsampling has no anti-alias filter.** Use a source already at the target rate for measurements requiring controlled bandwidth. Exact loop wrapping can click when the file endpoints do not join continuously; trim the source appropriately.

The 256 MiB PCM budget covers original decoded data, converted data, and any previous resident PCM retained during replacement. Empty, corrupt, unsupported, oversized, or nonfinite input is rejected. Records store the absolute file path, lowercase SHA-256, original rate/channel format, and conversion policy. Restore/render verifies content and original format, so a changed or missing file fails explicitly. The cached hash avoids hashing in save/display callbacks.

## Version 1 experiment schema

Every field shown here is required and uses a JSON number, boolean, string, or nested object as shown; numeric strings are invalid. See the [five complete examples](../examples/experiments/README.md).

```json
{
  "version": 1,
  "targetId": "reference-gain",
  "gain": 1,
  "bypass": false,
  "sampleRate": 48000,
  "channels": 2,
  "blockSize": 256,
  "renderDurationSamples": 48000,
  "filePath": "",
  "fileHash": "",
  "sourceConversion": "linear;mono-duplicate;stereo-average",
  "originalSampleRate": 0,
  "originalChannels": 0,
  "monitorDb": -18,
  "muted": true,
  "midiDevice": "",
  "buildIdentity": "example",
  "observations": "Example settings; no listening or measurement claim.",
  "source": {
    "kind": "sine",
    "frequency": 440,
    "frequency2": 880,
    "amplitude": 0.25,
    "phase": 0,
    "durationSamples": 48000,
    "seed": 1
  }
}
```

`source.kind` accepts `impulse`, `sine`, `two-tone`, `sweep`, `noise`, `file`, or `midi`. Both duration counts are integers in 1–536,870,000. `source.durationSamples` controls generated source length; `renderDurationSamples` is the exact offline output length for **every** source. A longer generated render continues with silence after the source finishes. A shorter render truncates the source; files repeat for the requested render length. Source frames do not trim a loaded file loop. Noise seed is an unsigned integer in 0–4,294,967,295, including zero. Sweep endpoints must increase.

`originalSampleRate` is 0–384,000 and `originalChannels` is 0–2. File records additionally require a positive original rate, one/two original channels, an absolute path, and a 64-character lowercase hexadecimal hash. Generated examples use empty file fields and zero original format. `buildIdentity` preserves the identity recorded with the experiment; renderer identity is reported separately. JSON input files are limited to 1 MiB. Unsupported versions, target IDs, conversion policies, malformed values, or invalid specifications fail validation.

## Offline rendering

The CLI and shared harness are available in full and DSP-only builds and require no audio device. Create an existing parent directory and choose a new output directory:

```powershell
New-Item -ItemType Directory -Force -Path '.\renders'
& '.\build\windows-vs2026\Release\disdorktion_render.exe' `
  --experiment '.\examples\experiments\sine.json' `
  --output-dir '.\renders\sine-001'
& '.\build\windows-vs2026\Release\disdorktion_render.exe' --help
```

For DSP-only use `build/windows-vs2026-dsp-only/Release/disdorktion_render.exe`. Relative arguments resolve against the current directory. The app's Render button asks for a parent folder and chooses a fresh `disdorktion-render` directory there.

A successful render publishes three files together:

- `source.wav`: 32-bit float WAV of the exact supplied input.
- `rendered.wav`: 32-bit float WAV of pre-monitor module output.
- `result.json`: version, exact frame count (`samples`), input/output peak and RMS over all channel samples, overload, latency samples, original experiment record, and `rendererBuildIdentity` for the current renderer.

Rendering prepares/resets a fresh target, uses recorded block size, and processes an exact final partial block. It checks the WAV RIFF size budget before writing. An existing output destination is rejected, including an empty directory; the parent must already exist. A staging directory is published by a no-replace rename only after all files succeed. Failure or cancellation cleans incomplete staging; cancellation is an I/O failure. Exit codes are **0** for success/help, **2** for invalid arguments/requests, and **1** for processing/I/O failure. Monitoring fields remain provenance in the experiment and do not change rendered audio.

The target registry supports reference gain, production gain, bit crusher,
tanh saturation, and wave folder.
The bit crusher's expressive extension uses version-4 records; its new controls
and persistence behavior are described above.

## Wave folder

Select **Wave folder** for drive (-24 to +36 dB), fold threshold (0.05 to 1),
output trim (-60 to +12 dB), and bypass. Threshold is an input fold boundary
after drive and also sets the wet output ceiling before trim. Controls ramp
over 10 ms; default settings preserve inputs in [-1, 1]. Monitoring remains
independent. The module has zero latency and runs at the native sample rate.

Version **6** requires target `wave-folder` and explicit numeric `driveDb`,
`threshold`, `outputDb`, and boolean `bypass`. Versions 1–5 retain their field
meanings and reject `threshold`; version 6 rejects gain/crusher fields.
Restoring a record detaches processing before applying its complete state.
Live scalar updates are independent atomic values, not a whole-state snapshot.
See [the design](okf/methods/wave_folding.md) and
[validation evidence](okf/notes/wave_folder_validation.md).

## Middle section quality comparisons (Q1)

This offline evaluation uses shared DSP and is available in full and DSP-only
builds. It compares native/2x/4x/8x/16x maximum-quality FIR for tanh, folder and
their cascade. Choose a new output directory:

```powershell
cmake --build --preset windows-vs2026-release --target disdorktion_middle_measurements
& './build/windows-vs2026/Release/disdorktion_middle_measurements.exe' './renders/middle-comparison-001'
uv run --no-project --python 3.12 scripts/validate_middle_measurements.py './renders/middle-comparison-001'
& './build/windows-vs2026/Release/disdorktion_middle_measurements.exe' './renders/middle-adaa-001' --adaa
```

`--adaa` selects the separate first-order antialiased triangle candidate. It
retains the settled DC fold curve but changes transients and harmonic levels;
its latency includes the internal averaging/compensation. The plain triangle
comparison remains the default. Neither option is production-qualified yet.

Existing destinations return exit 2; processing/I/O failure returns exit 1 and
retains partial output for diagnosis. A successful run returns 0 after writing
900 spectral cases (compressed full `spectra.csv.gz` and `alias_summary.csv`),
15 impulse filter/phase/group-delay and dry-blend responses, 144 diagnostic CPU
cases, 90 listening WAVs and `result.json`. Record the evaluated source snapshot
alongside results when using an uncommitted working tree; build identity records
starting revision/configuration. CPU figures include preallocated input copying
and report batch means under uncontrolled local conditions.

For a repeat, use another fresh directory, then pass `--repeat OTHER_DIRECTORY`
to the validator. It compares spectral summaries, filter data, compressed spectra
and all listening WAVs; timings and provenance JSON vary. The validator reports
engineering gates separately from artifact validity and never approves listening.

Compare factors within the same `path-kind-drive-threshold` filename group.
Files are integer-delay aligned, RMS matched and jointly peak scaled. Tones,
transients and sweeps are synthetic; capture listener/playback details and add
real material before final musical acceptance. See the [design](okf/methods/middle_oversampling.md)
and [measured limitations](okf/notes/middle_oversampling_validation.md).

### Compare your own WAV

```powershell
cmake --build --preset windows-vs2026-release --target disdorktion_middle_listening
& './build/windows-vs2026/Release/disdorktion_middle_listening.exe' 'C:/path/to/input.wav' './renders/middle-listening-001'
```

The input must be mono/stereo WAV, 8--384 kHz, finite and within [-1,1]. The tool
uses up to the first 30 seconds, preserving rate and input level. It writes a
reference plus plain 4x, ADAA 4x and ADAA 8x for four cascade recipes:

| Recipe | Tanh drive | Folder drive | Threshold |
| --- | --- | --- | --- |
| mild | 0 dB | 0 dB | 0.75 |
| moderate | 6 dB | 0 dB | 0.5 |
| stress-12 | 12 dB | 12 dB | 0.25 |
| stress-36 | 36 dB | 36 dB | 0.25 |

All 13 floating-point WAVs have the same length, integer-delay alignment,
whole-excerpt RMS matching within 0.001 dB and one common peak scale capped at
0.8. Start with moderate or mild settings if the stress settings obscure the
source. Keep playback volume fixed; compare source recognition, attacks,
high-note whistles and the desired distortion character. `result.json` records
source SHA256, build/revision/JUCE identity, parameters, latency and matching
gains. Save a source snapshot beside uncommitted results. Existing output returns
2; invalid input/processing/I/O returns 1 with partial output retained. Success
returns 0 and does not supply listening approval.

## Extend a target

`src/harness/AuditionTarget.h` defines a descriptor and adapter interface. `ReferenceGainTarget.h` translates the `gain` control and bypass into the module's typed parameters, delegating prepare, reset, process, and latency reporting to `ReferenceGain`.

To add an accepted module, supply its descriptor and adapter, extend the target registry and engine selection/storage, and add its controls to the interface and validated versioned record format. The registry alone does not make another module selectable. Extend offline restoration with the same adapter and validation. Target creation, replacement, prepare/reset, multi-field restoration, and destruction require processing to be detached. Only supported live scalar setters and meter reads may run concurrently with processing. Keep source/monitoring ownership in the harness and reuse production DSP.

Add mathematical, block-partition, monitor-independence, record round-trip, exact-length, and callback allocation checks for the new target, and document listening/measurement evidence in the [OKF bundle](okf/index.md). See [harness validation](okf/notes/harness_validation.md) for current evidence and unperformed checks. Source inspection and C++ allocation instrumentation have narrower scope than complete realtime or audible qualification.
