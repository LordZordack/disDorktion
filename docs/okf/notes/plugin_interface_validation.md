---
type: Note
title: "Plugin interface qualification"
description: "Evidence scope and outstanding checks for production controls, presets, meters and host state."
timestamp: 2026-11-08T00:00:00Z
tags: ["audio-plugin", "validation", "state", "presets", "metering"]
resource: "https://github.com/LordZordack/disDorktion/issues/14"
---

## Historical evidence scope after issue #38

The results below describe the recorded pre-issue-38 implementation and test
conditions. They do not qualify the revised Driver routing, section mixes,
nonlinear/filter Off endpoints, inactive history handling, defaults or v5-only
plugin state. Standalone module/harness controls may retain their own bypasses.
See the [current contract and focused evidence](./issue38_validation.md) for
what has actually been checked; no new CPU or listening result is inferred.

## Earlier validation record

# Plugin interface qualification

The [interface method](../methods/plugin_interface.md) and repository guide
`docs/PLUGIN_INTERFACE.md` describe the implemented contracts. Evidence below
is local VS2026 verification, not human listening or DAW acceptance.

## Recorded local evidence, 2026-10-17

- Configured the repository VS2026 full and DSP-only presets. Full Debug and
  Release builds succeeded, including VST3, Standalone, audition and test targets.
  DSP-only Release built independently. Sandbox MSBuild file tracking initially
  failed with access denied; approved build execution succeeded.
- Full Release: **255/255 CTest cases pass, 25.90 s**. DSP-only Release:
  **197/197 pass, 196.55 s**, with compilation competing during that run.
- An initial full Debug run passed 254/255 but timed out the existing output
  filter response case at 120 s during competing Release compilation. Its exact
  named isolated retry passed in **12.69 s** without code or timeout changes.
  Final full Debug without competing builds: **255/255 pass, 25.12 s**.
- The final Debug editor subset passed **5/5 cases, 703 assertions** after the
  snapshot-export correction. All 30 controls are visible at 1000x800,
  1120x850 and 1600x1100. The rendered snapshots were visually inspected at
  all three sizes. A minimum-size ComboBox height defect was corrected, and
  dB/linear values now display one/two decimal places. Numeric entry parsers,
  attachment round trips, active-editor schema restore/rejection, preset actions,
  Initialize, modified/reopened identity and instance isolation are covered.
- Preset cases exercise supported rates and block sizes, all schemas, reset,
  preserved mute, derived matching, deterministic offline state recall and
  allocation-free audio processing. A synchronized concurrent test covers
  legacy-to-full mode requests for all three state versions; scalar preset
  publication is still deliberately not a transactional bank. Meter tests cover
  transient retention through quiet/zero blocks and actual host-bypass output.
- Objective fixture grid: **160 rows** at 48/96 kHz, mono/stereo, block 127;
  100 ms warmup, 250 ms measurement. Sine is 997 Hz at 0.25 peak, two-tone
  components 997/1499 Hz at 0.125 peak each, transient peaks 0.5. The 60 settled
  rows cover all five presets and three fixtures; 100 transition rows cover all
  25 ordered preset pairs on the sine.
- Initial fixture levels informed explicit final-gain trims: Subtle -0.5 dB,
  Crunch -2.5 dB, Folded -1.5 dB, Digital -1 dB. Crunch uses the supported
  8-bit choice. Final grid maximum peak is **0.784227**, maximum absolute
  settled RMS offset from Initialize is **1.628324 dB**, and maximum sample
  delta is **0.407933**. The aggregate validator enforces peak <=1, settled
  RMS within +/-3 dB, complete unique coverage, finite values, and equivalent
  mono/identical-stereo data. Sample delta is reported, not a click metric.
- The repeated Release grid has identical CSV SHA-256
  `19b0e5048b848b6a2449d1daf1011e506476f06e114299348e8f1ab212d85073`.
  Data and producer executable hash are in repository artifacts
  `docs/measurements/plugin_interface_issue14/preset-levels.csv` and
  `docs/measurements/plugin_interface_issue14/summary.json`. The validator checks
  aggregate data; it does not independently recompute output audio samples.
- Antigravity supplied-context plan review and independent final source review
  found no remaining important implementation defect after the recorded fixes.
  Local review/log files and snapshots are under `.tickets/issue14-*` and
  `.tickets/issue-14-agy-review.txt`.
- Refreshed GitNexus includes the new plugin symbols. Change analysis requested
  all files through an alternate intent-to-add index, preserving the user's
  staging area: **22 files, 163 symbols, six affected flows, HIGH risk**.
  Those flows cover rendering, host bypass, restore, prepare and reset. The
  structured tracked-file result had no partial/truncated flags; index flow
  extraction itself reports capped candidate paths, and virtual host callbacks
  still require source verification. This is a lower-bound graph assessment,
  not evidence that no other host path is affected. No commit/publication ran.
- OKF validation passes after replacing two links that left the OKF bundle with
  repository path references. `git diff --check` passes for tracked changes.

## Reproduction

```powershell
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-debug --parallel 4
cmake --build --preset windows-vs2026-release --parallel 4
ctest --preset windows-vs2026-debug --output-on-failure --no-tests=error
ctest --preset windows-vs2026-release --output-on-failure --no-tests=error
cmake --preset windows-vs2026-dsp-only
cmake --build --preset windows-vs2026-dsp-only-release --parallel 4
ctest --preset windows-vs2026-dsp-only-release --output-on-failure --no-tests=error
$env:DISDORKTION_PRESET_REPORT = (Join-Path (Get-Location) 'docs/measurements/plugin_interface_issue14/preset-levels.csv')
& ./build/windows-vs2026/Release/disdorktion_adapter_tests.exe '[measurement]' --reporter compact
uv run --no-project --python 3.12 scripts/validate_plugin_preset_measurements.py docs/measurements/plugin_interface_issue14/preset-levels.csv --summary docs/measurements/plugin_interface_issue14/summary.json --executable build/windows-vs2026/Release/disdorktion_adapter_tests.exe
uv run --no-project --python 3.12 scripts/okf_manager.py --validate
```

Run the suites without competing builds when investigating timeouts. The
`DISDORKTION_EDITOR_SNAPSHOT_DIR` environment variable optionally exports
editor snapshots from the `[editor]` subset; use an existing absolute directory.

## Evidence limits

### Compact knob follow-up

The user reported that the initial editor did not fit their screen. The rotary
layout supersedes the earlier slider screenshots: default820x560, minimum740x520,
maximum1400x1000. Numeric fields, double-click defaults and all30 APVTS controls
remain available. Global and Compatibility share a lower row.

Release VST3, Standalone and adapter targets rebuilt successfully. The final
Release adapter run passed **39/39 cases, 312475 assertions**, including the five
editor cases. Layout checks now assert rotary style and pairwise non-overlap at
740x520,820x560,1200x800, and snapshot renders were inspected. Named previews are
under `.tickets/issue14-editor-{740,820,1200}.png`. OKF and tracked whitespace
checks pass. Earlier full Debug/Release and headless counts remain evidence for
the initial implementation; this UI follow-up reran the Release adapter suite.

Editor impact was LOW for resolved callers; constructor and virtual layout
edges were UNKNOWN and confirmed from createEditor, addControl and resize
tests. Parameter normalization, state and DSP were not modified.

Concurrent stress checks establish behavior for their tested schedules, not a
proof of race freedom. Preset gain compensation is source dependent. Raw levels
are preserved; there is no new limiter. Human listening with representative
audio is required to judge abruptness, character and practical loudness.

## Remaining acceptance gates

Rendered layout inspection at minimum/default/enlarged sizes is complete;
interactive keyboard/focus operation, real device metering, human audition
and practical loudness evaluation remain pending. Automated numeric entry,
attachment updates, reopening and instance isolation do not establish live
host behavior.
REAPER automation/session save/restore/offline render, pluginval and pinned
VS2022 CI remain unverified until run.

Issue 13 is merged and supplies the shared chain. Its
[qualification record](./full_chain_validation.md) remains inherited evidence;
issue 14 does not replace or waive its pending listening, reference CPU,
dependency, host and release qualification gates. Release packaging belongs to
issue 15. No release-readiness claim follows from the interface implementation.
