# Generated experiment examples

Version-10 output-section records `output-section-wide.json`,
`output-section-narrow.json` and `output-section-crossed.json` use the same
native-rate HP -> LP target as the audition app. They preserve independent
cutoffs, response selections and stage bypasses plus common bypass. Each uses a
five-second 48 kHz stereo log sweep, peak 0.25, block 256. Wide uses
20/20000 Hz Butterworth; narrow uses 800/2500 Hz Chebyshev I; crossed uses
8000/200 Hz Chebyshev II. Listening observations are pending. Use fresh offline
output directories; monitoring does not alter rendered samples.

`middle-section-mild.json` and `middle-section-stress.json` use version 9 and
fixed quality `4x-fir-adaa-v1`. The first uses 0/0 dB drives and threshold 0.75;
the second uses 36/36 dB drives and threshold 0.25. Both store each trim and
bypass explicitly. Their generated source ends at frame 48000 and their render
ends at 49024, capturing ring-down in trailing silence. Output remains an
untrimmed stream with 92 samples of reported delay. These candidate examples
make no listening or residual-alias acceptance claim. See the
[section design](../../docs/okf/methods/middle_section.md).

`high-pass-sweep.json` and `low-pass-sweep.json` are version-7, 48 kHz stereo,
five-second sweeps from 20 Hz to 20 kHz at peak amplitude 0.25, cutoff 1 kHz,
Butterworth, monitor −18 dB and monitoring muted. Restore in the audition app
and switch response to compare Chebyshev I/II, or pass the record to
`disdorktion_render`. The filter measurement tool generates all six mode pairs.

The five foundation JSON files use the complete version 1 [experiment schema](../../docs/AUDITION_HARNESS.md): impulse, sine, two-tone, logarithmic sweep, and seeded white noise. Each uses reference gain at unity, 48 kHz stereo, 256-frame processing, 48,000 source frames, and 48,000 render frames. They are settings examples; `buildIdentity: "example"` makes that provenance explicit and observations make no measured/listening claim.

`gain-sine.json` and `gain-mute.json` use version 2, production target `gain`,
explicit `gainDb: 6` and module `mute`. The first renders a +6 dB sine; the
second renders silence. Monitoring fields are provenance and do not change
offline samples. Sources are mathematically generated; no third-party audio
license is needed. These records describe settled controls, not automation.

The original bit-crusher examples use version 3 and target `bit-crusher`:
`bit-crusher-sine.json`, `bit-crusher-alias.json`, and
`bit-crusher-low-bits.json`. They remain legacy algorithm fixtures. New saves
use version 4 with required `mix`, `crush`, `shape`, `jitter`, and
`jitterSeed` fields. [bit-crusher-expressive.json](bit-crusher-expressive.json)
shows the extension controls together; its settings are illustrative and make
no listening or measurement claim. Prior test and CPU results apply to the
version-3 algorithm only. Expressive tests and render checks passed; new-WAV
listening and CPU qualification remain pending. See the
[validation note](../../docs/okf/notes/bit_crusher_validation.md).

Render from the repository root after building Release:

The `tanh-sine.json`, `tanh-sweep.json`, `tanh-two-tone.json`, and
`tanh-extreme.json` examples use version 5 and target `tanh-saturation`.
They store drive and output trim explicitly. These native-rate settings
examples use generated sources and make no listening acceptance claim.

```powershell
New-Item -ItemType Directory -Force -Path '.\renders'
& '.\build\windows-vs2026\Release\disdorktion_render.exe' `
  --experiment '.\examples\experiments\sweep.json' `
  --output-dir '.\renders\sweep-001'
```

Choose a new output directory on every run. The renderer preserves the example record in `result.json` and adds its actual `rendererBuildIdentity`. To restore these in the GUI, match 48 kHz, stereo, and a 256-frame device block first. Source frames and render frames are independent; a longer generated render contains silence after the source ends. For file experiments, save a successfully loaded WAV/AIFF in the app to obtain the absolute path, actual hash, and original format.

`wave-folder-sine.json`, `wave-folder-sweep.json`, `wave-folder-two-tone.json`,
and `wave-folder-extreme.json` use version 6 and target `wave-folder`, with
explicit drive, threshold, output trim and bypass. They use generated sources;
the settings make no human listening acceptance claim. Lower thresholds also
lower the wet output ceiling. See the
[wave-folder design](../../docs/okf/methods/wave_folding.md).

`full-chain-gentle.json`, `full-chain-destructive.json` and
`full-chain-phase-blend.json` use version 11 and target `full-chain`.
They include every section, global mix, final gain and fixed quality identity.
The render lasts 145024 frames including 1024 explicit silent source-tail frames,
with 92 samples of untrimmed processing latency. Monitoring starts muted at -18 dB.
The phase-blend example bypasses middle nonlinearities and narrows the output
filters at 50% mix to expose phase-sensitive blending. Compare endpoints and
bypasses at matched monitor levels, then record human observations. These
examples do not establish listening acceptance.
