---
type: Note
title: "v009 faceplate validation"
description: "Editor regression tests, rendered comparisons, and Windows package smoke checks for issue 50."
timestamp: 2026-11-24T00:00:00Z
tags: ["audio-plugin", "editor", "metering", "waveforms", "validation"]
resource: "https://github.com/LordZordack/disDorktion/issues/50"
---

# v009 faceplate validation

The [interface method](../methods/plugin_interface.md) describes the canvas,
parameter binding, and signal display behavior.

The Visual Studio 2026 Release build produced the adapter tests, Standalone
application, and VST3 bundle. The adapter run passed 52 of 52 tests, including
asset geometry and malformed JSON, readout editing and cancellation, preset
accessibility, waveform lifecycle, and uniform scaling. The DSP-only preset
configured without the faceplate target.

```powershell
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026-release --target disdorktion_adapter_tests DisDorktion_Standalone DisDorktion_VST3 --parallel 4
ctest --preset windows-vs2026-release -R "^adapter::" --output-on-failure --no-tests=error
cmake --preset windows-vs2026-dsp-only
```

To regenerate editor snapshots, create a directory, set
`DISDORKTION_EDITOR_SNAPSHOT_DIR` to its absolute path, and rerun the
editor adapter tests. Captures at 1040 × 948 and 624 × 569 were inspected
against the supplied 4× composite. The scope and meter check used a known
stereo signal. Live defaults, display text, and waveforms differ from the
sample values in the assembled artwork.

The copied 38,388,224-byte Standalone started from a temporary directory and
reached input idle in approximately 8.8 seconds on one run. Its working set
was approximately 144 MiB. Windows loaded the copied 38,108,672-byte VST3
module and located its factory export. Both tests ran without adjacent
reference files.

VS2022 configuration requires the unavailable v143 toolset. Native DAW
automation and recall, physical 1080p at 125% display verification, and
comparable baseline and second-editor memory measurements remain pending.

The overload-readout follow-up rebuilt the VST3 and passed all 53 adapter
tests. The new regression confirms that each dBFS readout can take keyboard
focus when overloaded and clears only its own latch.
