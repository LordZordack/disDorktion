# Plugin interface, automation and state

The prototype editor exposes Input, Waveshaper, Output and GLOBAL panels.
Mono/stereo production processing reports a fixed **92 host samples** of latency,
including zero section mixes and nonlinear Off states. See the
[design](okf/methods/full_chain.md) and [Driver adjustment status](okf/notes/driver_adjustment_validation.md).

## Routing and controls

Input Gain -> global dry/wet split -> Input crusher/sampler -> Waveshaper ->
Output -> global mixing -> Final Gain. The dry branch is delayed by 92 samples.
Input Gain affects both global branches regardless of section/global mixes.
Driver remains a GLOBAL control, 1x to 4x with default 1x, but acts after the
crusher immediately before tanh/fold, inside the nonlinear wet branch. Global
dry and Waveshaper section dry are unchanged by Driver. Driver has no audible
effect with both nonlinear stages Off, Waveshaper Mix at zero, or global mix at
zero; manual Waveshaper Trim remains active with both stages Off.

Driver uses 10 ms smoothing. A fixed calibrated lookup corrects only the
nonlinear output, before shared Waveshaper Trim and section mixing; compensation
follows the delayed Driver trajectory. Knots at 1, 1.5, 2, 3 and 4 use median RMS
correction across four active factory configurations, sine/two-tone/seeded-noise
signals, -24/-18/-12 dB levels and 48/96 kHz. Corrections are clamped to +/-6 dB
and interpolated linearly in dB against log Driver. There is no live level
tracking or AGC, and nonlinear processing need not preserve perceived loudness.

There are no visible mute or bypass buttons, compatibility controls, Legacy
Trim or Tanh Trim. Whole-plugin bypass remains available through the DAW.
`FullChainSettings::bypass` remains an internal development-harness control,
never a plugin parameter.

| Controls | Range | Initialize default |
| --- | --- | --- |
| GLOBAL Input Gain; Final Gain | -60 to +24 dB | 0 dB |
| GLOBAL Driver | 1x to 4x | 1x |
| GLOBAL dry/wet; Input Mix; Waveshaper Mix | 0 to 100% | 100% |
| Bit depth | 1, 2, 3, 4, 5, 6, 7, 8, 16, 32, 64 bits | 64 bits |
| Requested capture rate | 100 to 384000 Hz | 384000 Hz |
| Crush; jitter | 0 to 100% | 0% |
| Quantizer shape | -100 to +100% | 0% |
| Tanh Drive; Fold Drive | Off at -24 dB, otherwise up to +36 dB | 0 dB |
| Threshold | 5 to 100% | 100% |
| Waveshaper Trim | -60 to +12 dB | 0 dB |
| High-pass; low-pass | 20 to 20000 Hz | Off at 20 Hz; Off at 20000 Hz |
| Filter response | Butterworth, Chebyshev I, Chebyshev II | Butterworth |
| Comb delay | 0.25 to 40 ms | 5 ms |
| Comb feedforward; feedback | -1 to +1; -0.95 to +0.95 | 0; 0.7 |
| Comb damping; mix | 20 to 20000 Hz; 0 to 100% | 6000 Hz; 0% |
| Dispersion frequency; mix | 20 to 20000 Hz; 0 to 100% | 500 Hz; 0% |
| Tone | -6 to +6 dB, unity pivot at 1 kHz | 0 dB |

Input Mix covers the complete crusher/sample-rate reducer, with Input Gain
outside the section. Waveshaper controls run Tanh Drive -> Fold Drive ->
Threshold -> Waveshaper Trim -> Waveshaper Mix. Shared trim follows both
nonlinear stages and precedes the section blend. When both stages settle Off,
the section uses trimmed delayed dry, blended with untrimmed delayed dry;
trim remains active. Individual Off transitions preserve effective delay,
including the ADAA folder contribution.

After section mix fades reach zero, unnecessary wet processing is reset and
suspended. The Waveshaper dry delay continues advancing. Reactivation warms
wet histories with wet contribution held at zero before fading toward the
latest target; this delays the control response without changing audio latency.
Comb history clears after its mix finishes fading to zero, preventing old tails
from returning. Comb damping is always enabled during active processing.

Numeric knob fields remain editable and double-click restores defaults. Hz
controls use logarithmic mouse movement, including capture
rate and HP/LP cutoffs. Host-normalized parameter mapping remains linear.
Tanh/Fold minima, HP minimum and LP maximum display
**Off**. Capture requests are retained while effective rate is host-capped;
active filter cutoffs are capped at 45% of host rate. The editor starts at
1120x740 and resizes from 1000x680 to 1400x1000.

## Automation, presets and persistence

The current prototype has **27 host parameters**, defined in order by
[FullChainControls.h](../src/dsp/FullChainControls.h). Old prototype parameter
order, normalization and state compatibility are intentionally not preserved.
Bit depth is a host choice index 0 to 10; serialized state uses actual bit values.
Filter response values 0 to 2 mean Butterworth, Chebyshev I and Chebyshev II.

The Driver adjustment keeps these parameter IDs and persistence schemas,
including plugin state version 5 and full-chain experiment version 17. Existing
saved sounds with Driver above 1x intentionally change; default Driver 1x
retains its prior behavior.

The preset button opens Save preset, Save preset as, Load preset, Delete preset,
Reset to defaults, and adjacent Factory presets and User presets submenus.
Factory presets are Initialize, Subtle Warmth, Crunch, Folded and Digital
Destruction; [FactoryPresets.h](../src/plugin/FactoryPresets.h) defines their
settings. Their names describe intended character; no new listening acceptance
or loudness matching is claimed. Preset selection remains available after
editing controls. The button retains the current preset name and indicates
modified settings; returning controls to their saved values clears that status.

Save preset overwrites the current user file. Saving a factory preset creates
a user file with the same name; an existing same-name file directs you to Save
preset as. Factory definitions stay unchanged. Save preset as chooses a path
and name, initially under JUCE's per-user application-data directory at
`DisDorktion/Presets` (`%APPDATA%/DisDorktion/Presets` on Windows).
Save as adds `.disdorktionpreset` when no extension is supplied and asks before
replacing an existing file at that final path. Load accepts valid preset XML
with any extension, including files outside that folder. Such a file becomes the current
user preset and Save targets it, but User presets lists only
`.disdorktionpreset` files directly inside the default folder, refreshed when
the menu opens. Saving outside it does not add a menu entry.

Delete preset is enabled only for the current user preset. Successful deletion
resets the plugin to defaults. Reset to defaults, a new plugin instance and the
Initialize factory preset set Tanh Drive and Fold Drive to **0 dB**. Their
**Off** endpoints at -24 dB remain available. Shared DSP and audition-harness
defaults retain both stages Off. DSP history reset retains current settings.

User preset files contain XML `DisDorktionState` version 5 scalar snapshots,
with the same strict parameter validation as host state. Writes use a temporary
file followed by replacement. Invalid files are rejected before parameters or
preset identity change. Cancelled dialogs and failed operations retain the
current sound and identity; errors are displayed. Asynchronous actions are
discarded if a newer operation, host restore or preset change makes their
captured target stale. Recall publishes independent scalar host parameters,
so intermediate combinations may be observed.

New binary JUCE XML saves use `DisDorktionState` **version 5** with every current
control required. Older versions are rejected. Parsing validates the complete
candidate before publication; malformed, incomplete, nonfinite, out-of-range
or unsupported values reject the whole load. Unknown extra attributes are
ignored. Optional host-state preset metadata retains factory/user identity and
the user save target across editor reopening and host restore. Missing or invalid
identity metadata, including a missing or invalid user file, detaches the save target while
valid parameter values still restore. Save then uses Save preset as and Delete
is disabled. Existing valid version-5 states without metadata remain accepted
with their stored drive values; they do not inherit a previously loaded user
file target. Successful restore resets DSP, bypass delay and meters. Host
save/restore uses synchronized identity snapshots; ordinary controls are
independent atomics. See the
[preset validation record](okf/notes/preset_management_validation.md) for the
automated evidence and remaining native UI/host checks.

## Host bypass, meters and qualification

DAW bypass returns latency-aligned raw bus input, bypassing Input Gain, Driver,
compensation and Final Gain while keeping the processing chain serviced. The
raw input meter precedes Input Gain; final output metering includes host bypass
replacement. Peak retention and overload latches are consumed by the editor at
30 Hz. Reset/release clear meter state; clearing overloads takes effect at the
next callback and a new overload may relatch.

Full-chain tail reporting remains conservatively 20 seconds plus FIR latency;
this is an allowance for recursive histories, not an absolute silence promise.
No limiter or automatic normalization is inserted. Historical qualification
records predate this prototype revision. The
[issue #38 validation note](okf/notes/issue38_validation.md) preserves the earlier
automated passes. Verification of the Driver adjustment is separately tracked
in the [current validation note](okf/notes/driver_adjustment_validation.md).
