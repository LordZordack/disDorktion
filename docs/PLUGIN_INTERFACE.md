# Plugin interface, automation and state

The editor exposes the shared [full chain](okf/methods/full_chain.md) used by
the audition application. Input gain and crushing feed fixed 4x FIR/ADAA tanh
and folding, then high-pass and low-pass filters, global mixing and final
gain. Mono and stereo use matching input/output layouts. Full-chain latency
is 92 host samples; legacy gain mode reports zero. See
[qualification status](okf/notes/plugin_interface_validation.md) for evidence
and remaining acceptance work.

## Editor controls

Input, Middle and Output panels contain effect controls and independent
bypasses. Global controls provide dry/wet, final gain, output mute, final-gain
bypass and chain bypass. Compatibility displays the active mode and exposes
the original linear trim and its bypass. In legacy mode the distortion/filter
effects are inactive; selecting a factory preset or Initialize requests the
full chain.

Numeric knob fields are editable. Double-click restores a knob's parameter
default. Frequencies display Hz, gains display dB, the legacy trim displays a
linear multiplier, and mix/shape/threshold/jitter values display percentages.
Frequency knobs use a visual skew for mouse travel; host normalization stays
linear. Bit depth and filter response use named choices. The editor starts at
820 by 560 pixels and can resize from 740 by 520 to 1400 by 1000. Compact
knob grids keep the three processing sections visible; Global and Compatibility
share the lower row. Drag knobs vertically or horizontally to adjust values.

| Controls | Range | Default |
| --- | --- | --- |
| Input gain; final gain | -60 to +24 dB | 0 dB |
| Bit depth | 1, 2, 3, 4, 5, 6, 7, 8, 16, 32, 64 bits | 64 bits |
| Requested crusher capture rate | 100 to 384000 Hz | 384000 Hz |
| Crusher mix; global mix | 0 to 100% | 100% |
| Crush; clock jitter | 0 to 100% | 0% |
| Quantizer shape | -100 to +100% | 0% |
| Tanh drive; folder drive | -24 to +36 dB | 0 dB |
| Tanh trim; folder trim | -60 to +12 dB | 0 dB |
| Folder threshold | 5 to 100% | 100% |
| High-pass; low-pass cutoff | 20 to 20000 Hz | 20; 20000 Hz |
| Filter response | Butterworth, Chebyshev I, Chebyshev II | Butterworth |
| Legacy trim | 0 to 4 times | 1 time |
| All mute and bypass switches | Off / On | Off |

Requested capture rate is retained even when the effective rate is capped by
the processing rate. Filter cutoff is capped at 45% of host sample rate. The
defaults enable the processing chain; Initialize is not a transparent bypass.

## Automation compatibility

The schema remains **30 parameters**: two legacy controls plus 28 full-chain
controls. Parameter IDs, version hints, ordering, defaults and normalized host
mappings are retained. UI formatting and frequency mouse skew add no parameters.
The first eight host parameters are `referenceGain`, `referenceBypass`,
`inputGainDb`, `inputMute`, `inputBypass`, `outputGainDb`, `outputMute`,
`outputBypass`; subsequent parameters follow
[FullChainControls.h](../src/dsp/FullChainControls.h), skipping those six gain
controls already emitted. The complete host construction is in
[PluginProcessor.cpp](../src/plugin/PluginProcessor.cpp).

`bitDepth` is a host choice **index** (0 through 10), while DSP settings and
serialized state use actual bit values. For example, host index 8 means 16
bits. The filter response integer values 0, 1 and 2 mean Butterworth,
Chebyshev I and Chebyshev II. Attachments publish ordinary host gestures.

## Bypass and metering

Chain bypass selects delayed dry before final gain, so final gain and mute
still act. The dry split follows legacy trim but precedes input drive.
Individual effect bypasses retain the middle FIR/ADAA boundary. Bypassing all
effects therefore need not equal chain bypass. Host bypass returns exact
delayed **raw bus input**, including bypass of legacy trim and final gain,
while keeping processing histories warm. In legacy mode host bypass has zero
delay.

The input meter measures raw bus input before legacy trim. The output meter
measures the actual final samples returned to the host, including the host
bypass replacement. Each meter combines channels into a peak, retains peaks
between UI polls and latches overload when absolute amplitude exceeds 1.
These meters do not expose internal wet-path overload during dry selection.
The editor polls at 30 Hz and applies display decay; the processor does not
decay an unread peak. A zero-length block does not erase retained peaks.
Prepare, release and DSP reset clear processor meter state. Clear overloads
requests latch clearing at the next callback; a new overload can immediately
relatch. Initialize drains pending peaks and requests overload clearing.

Peak publication has one audio producer and one UI consumer. Bounded atomic
load/store maximum accumulation and UI exchange avoid a retry loop. A
concurrent exchange can repeat a prior peak for one callback without losing a
new peak; the repeated value only affects visual decay.

## Factory presets and Initialize

The editor's selector provides Initialize, Subtle Warmth, Crunch, Folded and
Digital Destruction. These are complete settings in
[FactoryPresets.h](../src/plugin/FactoryPresets.h); their names describe
intended character, with listening acceptance still pending. They are editor
presets; the host program API continues to expose one program.

Each setting starts from Initialize defaults; the table shows its changes.
The source header remains the complete definition, including every unchanged
default.

| Preset | Changes from defaults |
| --- | --- |
| Initialize | All defaults in the controls table |
| Subtle Warmth | Tanh drive +3 dB, trim -2 dB; folder bypassed; global mix 45%; final gain -0.5 dB |
| Crunch | 8 bits, capture 18000 Hz; tanh drive +9 dB, trim -6 dB; folder bypassed; low-pass 10000 Hz; final gain -2.5 dB |
| Folded | Tanh drive +6 dB; folder drive +9 dB, threshold 45%, trim -3 dB; high-pass 60 Hz; final gain -1.5 dB |
| Digital Destruction | 6 bits, capture 6000 Hz, crush 65%, shape 30%, jitter 15%; tanh drive +15 dB, trim -6 dB; folder drive +12 dB, threshold 30%; high-pass 80 Hz, low-pass 7000 Hz; final gain -1 dB |

All recalls restore legacy trim to 1 and legacy bypass to Off, publish the
preset controls through host parameters, and request full-chain mode. The four
sound presets preserve the current output mute. Initialize restores every
control to its default, including output mute Off. DSP reset retains settings
and clears histories; it differs from Initialize.

Preset identity is derived from current quantized normalized parameter values
with tolerance 2e-6. Matching checks the legacy trim/bypass and all chain
controls except output mute; legacy mode always displays Modified. Manual
edits or automation that break the match display Modified. Returning to the
same settings, reopening the editor, or restoring a matching state derives
the factory name again. No preset identity or editor metadata is persisted.

Recall publishes independent scalar parameters, not an atomic parameter bank.
The audio callback can observe intermediate combinations. Existing 10 ms DSP
ramps reduce ordinary control steps; they do not guarantee seamless changes
between legacy and full-chain latency paths. The message thread requests the
mode through an atomic, and the next callback applies it and clears mode
histories. Hosts that stop callbacks defer this application until processing
resumes. Host latency/tail notification occurs on the message/lifecycle thread.

Preset trims are source dependent. There is no added limiter or automatic
normalization. Settled output, transitions, and representative-source listening
must be assessed before treating preset loudness as accepted.

## Save and restore policy

The binary JUCE XML payload has root `DisDorktionState`. New saves remain
version 3. Parsing and validating the complete candidate precedes publication.

| Version | Required fields and migration |
| --- | --- |
| 1 | `gain`, `bypass`; restore legacy gain mode and default all production controls |
| 2 | `gain`, `bypass` and the six original production gain/mute/bypass fields; restore legacy mode and default later chain controls |
| 3 | `gain`, `bypass`, all 28 chain fields and `mode` equal to `legacyGain` or `fullChain` |

Null/empty input, malformed XML/binary data, wrong root, unsupported or missing
version, invalid mode, missing required fields, nonfinite/out-of-range values,
invalid discrete choices and booleans other than literal `0`/`1` reject the
whole load without changing current settings or mode. Bounds are checked at
source precision before narrowing to float. Unknown extra attributes are
ignored. Older schemas use explicit defaults for fields they did not define.
Future versions are rejected; there is no speculative partial restore.

Restore is a serialized host lifecycle operation. Host/editor parameter
notifications occur outside the callback lock; mode ownership and DSP changes
are synchronized with rendering. State saving holds the callback lock for a
consistent mode/DSP ownership boundary, while ordinary parameters remain
independent atomics. Direct processor callers must provide the same exclusive
lifecycle ownership as JUCE host wrappers. Editor preset operations run on the
message thread, without taking the callback lock. Each instance owns its
parameters, DSP, meters and derived preset matching.
