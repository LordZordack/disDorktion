---
type: Note
title: "Factory and user preset management validation"
description: "Issue 43 preset workflow, persistence contract and evidence ledger, separating automated checks from pending native UI and host acceptance."
timestamp: 2026-11-12T00:00:00Z
tags: ["audio-plugin", "presets", "validation", "state", "automation"]
resource: "https://github.com/LordZordack/disDorktion/issues/43"
---

# Factory and user preset management validation

## Behavior under evaluation

The [interface method](../methods/plugin_interface.md) records the unified
preset menu, immutable factory definitions and writable user files. New plugin
instances, Reset to defaults and Initialize use 0 dB Tanh/Fold Drive, while
shared DSP/harness defaults retain Off. Other factory settings and existing
version-5 saved parameter values are preserved.

Preset identity survives parameter edits and editor reopening. Save retains the
current user file; a factory save creates a same-name user file, with collisions
directed to Save as. Delete applies only to the current user file and resets
defaults after successful deletion. The default folder is JUCE per-user
application data at `DisDorktion/Presets`; only its direct
`.disdorktionpreset` files appear under User presets. External files can be
loaded and remain the current save target.

User snapshots and host state share strict version-5 scalar validation. Bad
parameter input leaves parameters and identity unchanged. Valid states without
valid optional identity metadata restore sound but detach prior file targets.
Asynchronous callbacks must reject stale operations after host restore, reset,
preset changes or a newer action. Cancellation/failure must preserve the
current sound and identity.

## Evidence status

After all review fixes, the complete Debug adapter CTest run passed
**40/40 checks in 27.16 seconds**. The Debug adapter build and complete Release
build both exited successfully. The full Release CTest suite passed
**330/330 checks in 43.98 seconds** (exit 0).

Earlier, the Debug adapter suite passed 35/35 in 137.03 seconds. A direct
editor/file-workflow run passed **2314 assertions in 14 cases**. The
editor snapshot case passed **2057 assertions in one case**; its 1000x680
minimum-size image was visually inspected, with no control overlap and both
drive defaults displaying 0 dB.

Final fixes retain the canonical loaded snapshot as the baseline under
concurrent automation, serialize plugin writers with shared local and per-path
interprocess locks, and prevent a factory Save from overwriting another
instance's user file. Restored user identity requires strict validation of its
referenced file, regardless of extension. Save as confirms its final path after
adding a missing extension; controlled async tests include stale confirmation
after host restore and editor closure. Final source review reported no
unresolved findings. Historical
[interface validation](./plugin_interface_validation.md) and
[Driver results](./driver_adjustment_validation.md) do not qualify issue 43.

| Check | Status |
| --- | --- |
| First-open selection, selection after edits, timer/deferred ordering and editor reopening | Final Debug adapter coverage passed |
| Factory/user identity, overwrite, collisions, external files and independent instances | Final Debug adapter coverage passed |
| Save/load/delete failures, cancellation and invalid snapshots | Final Debug adapter coverage passed |
| Metadata-free/invalid/missing-target host restore and stale asynchronous completion | Final Debug adapter coverage passed |
| Concurrent host serialization/restore, automation and allocation regressions | Final Debug adapter coverage passed |
| Plugin 0 dB defaults, retained harness Off defaults and other factory settings | Debug adapter coverage passed; 0 dB snapshot inspected |
| Debug adapter suite | 40/40 passed after all fixes, 27.16 s |
| Debug adapter and complete Release builds | Passed |
| Complete Release regression suite | 330/330 passed, 43.98 s |
| Native popup adjacency, browser default folder, visible errors and close with open dialog | Manual check pending |
| Live DAW save/restore, automation and preset interaction | Manual host acceptance pending |

## Reproduction and qualification procedure

Configure the full plugin preset with `cmake --preset windows-vs2026`, build
`disdorktion_adapter_tests` using `windows-vs2026-debug`, then run
`ctest --preset windows-vs2026-debug -R "^adapter::" --output-on-failure --no-tests=error`.
Build the complete `windows-vs2026-release` preset and run its full CTest suite
with `--output-on-failure --no-tests=error`. Tests must use temporary libraries
instead of the real user preset folder.

The old selection failure was reproduced before production edits. The test
selected ComboBox index 3 with `sendNotificationAsync`, waited 80 ms, forced
pending timers synchronously and invoked the real `onChange` callback. The
expected factory index 3 failed with actual index 0. The original GUI baseline
had passed 2/2 checks. This demonstrates that timer refresh could overwrite a
deferred selection; the replacement menu retains its captured selection across
timer updates in the controlled editor tests.

The editor uses an injectable `PresetDialogs` interface. Controlled async
tests exercise menu selections, file completion/cancellation and stale callbacks;
the production adapter uses JUCE `PopupMenu::showMenuAsync`, `FileChooser` and
warning dialogs. Source inspection confirms adjacent Factory/User submenu
definitions, a disabled Delete item for non-user identity, serialized menu/file
interactions, safe editor pointers and generation checks. The timer updates
button/status text without rewriting a pending selection.

Manual acceptance must check the adjacent factory/user popup menus, switching
before Initialize and after editing, Save as's default path, cancellation,
error messages, and editor closure while a native dialog is open. Repeat host
state restoration in a DAW, including old metadata-free version-5 states.
No native interactive popup/browser or live DAW smoke test has been performed.
Automated adapter tests and the static editor image do not establish native
browser layout or live host acceptance.

Related design: [plugin interface](../methods/plugin_interface.md),
[full chain](../methods/full_chain.md), [project](../projects/disdorktion.md).
