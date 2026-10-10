# Audition and measurement harness

The dedicated **DisDorktion Audition** app auditions the same `ReferenceGain` DSP used by the plugin. Its initial target is `reference-gain`, with linear gain 0–4 (default 1) and smoothed bypass. Accepted modules, sections, and chains can be added later. The generated plugin Standalone wrapper, `DisDorktion.exe`, has a separate purpose and does not supply this experiment workflow.

## Start an audition

Build with the [full Windows preset](BUILDING.md), then launch:

```powershell
& '.\build\windows-vs2026\DisDorktionAudition_artefacts\Debug\DisDorktion Audition.exe'
```

Use `Release` for the Release artifact. Open **Audio / MIDI devices** to choose a working mono/stereo output, sample rate, block size, and MIDI input. The app shows the active specification. It supports 8,000–384,000 Hz, one or two channels, and prepared block sizes 1–65,536 frames.

1. Select a generated source, **Audio file loop**, or **Keyboard / MIDI sine**. Load WAV/AIFF with the file button when using a loop.
2. Set frequencies in Hz, peak amplitude, phase in radians, **Source frames**, **Render frames**, and noise seed. Click **Apply source** to commit the editors. Selecting another source also attempts to apply its settings. Source application stops playback; press **Play** afterward.
3. Monitoring starts muted at −18 dB. Set monitoring volume (−60–0 dB) and unmute to listen. Module gain and bypass update live.
4. **Stop** stops playback and releases notes. **Restart** resets source and target history. Generated sources stop automatically at their source length and the button returns to **Play**; press Play to repeat them from the beginning. File loops continue until stopped.
5. Enter observations and save an experiment JSON. Reloading restores a validated record stopped. The device sample rate, channel count, and block size must match the record; otherwise restore reports the mismatch and refuses it. Match the device settings before retrying.

Save and Render use the **last applied source and frame counts**, together with current gain, bypass, monitoring, and observations. Typing into a source editor does not commit it. Click Apply source before saving or rendering changed source settings. Worker activity disables source/settings, gain/bypass, observations, and save/restore/render controls until completion; monitoring remains available.

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

## Extend a target

`src/harness/AuditionTarget.h` defines a descriptor and adapter interface. `ReferenceGainTarget.h` translates the `gain` control and bypass into the module's typed parameters, delegating prepare, reset, process, and latency reporting to `ReferenceGain`.

To add an accepted module, supply its descriptor and adapter, extend the target registry and engine selection/storage, and add its controls to the interface and validated versioned record format. The current engine and schema explicitly support only reference gain; the descriptor registry alone does not make another module selectable. Extend offline restoration with the same adapter and validation. Target creation, replacement, prepare/reset, multi-field restoration, and destruction require processing to be detached. Only supported live scalar setters and meter reads may run concurrently with processing. Keep source/monitoring ownership in the harness and reuse production DSP.

Add mathematical, block-partition, monitor-independence, record round-trip, exact-length, and callback allocation checks for the new target, and document listening/measurement evidence in the [OKF bundle](okf/index.md). See [harness validation](okf/notes/harness_validation.md) for current evidence and unperformed checks. Source inspection and C++ allocation instrumentation have narrower scope than complete realtime or audible qualification.
