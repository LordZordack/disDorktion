---
type: Note
title: "Audition harness validation"
description: "Issue #3 validation evidence and remaining manual device and listening checks, distinguished from tool implementation."
timestamp: 2026-10-10T00:00:00Z
tags: ["dsp", "harness", "validation", "realtime"]
---

# Audition harness validation

See the [audition measurement method](../methods/audition_measurement.md), [project](../projects/disdorktion.md), [foundation validation](foundation_validation.md), [build instructions](https://github.com/LordZordack/DisDorktion/blob/main/docs/BUILDING.md), and [harness guide](https://github.com/LordZordack/DisDorktion/blob/main/docs/AUDITION_HARNESS.md). Repository documentation links become available on GitHub when this work is published.

## Evidence status

Issue #3 implements shared reference-target processing, deterministic sources, resident file loops, keyboard/MIDI voice and queues, the dedicated audition app, strict experiment persistence, and exact-length offline float-WAV/JSON rendering. Implementation availability is distinct from qualification evidence.

Local Visual Studio 2026 configure/build/test verification produced:

| Configuration | Build | CTest result |
| --- | --- | --- |
| `windows-vs2026-debug` | Full targets, including audition app, renderer, VST3, and plugin Standalone | 47/47 passed, 0 failures |
| `windows-vs2026-release` | Full targets, including audition app, renderer, VST3, and plugin Standalone | 47/47 passed, 0 failures |
| `windows-vs2026-dsp-only-release` | Shared DSP/harness, renderer, and headless tests | 35/35 passed, 0 failures |

Commands used the configure/build/test presets in `docs/BUILDING.md`, with `--output-on-failure --no-tests=error` for CTest. Eight root `disdorktion*.vcxproj` projects in the headless build were inspected and contained no GUI/audio-device module `.cpp` sources. These are local build results, not hosted VS2022 CI results. Raw work evidence is retained locally under `.tools/issue-3/` and the build directories.

All five checked-in generated JSON examples rendered successfully through the CLI at exactly 48,000 frames each. At their recorded unity gain, input/output peak and RMS matched. Help returned 0; unknown/missing/duplicate arguments, an existing output directory, and malformed JSON each returned 2. These are functional checks of the examples and renderer, not sound acceptance or performance qualification.

The Release audition app opened a responding window and closed gracefully with exit 0. An initial smoke attempt raced window readiness; the reattached process did not expose an exit code. The later controlled launch/close supplied the successful evidence. Window capture exposed three text encoding artifacts; their strings were changed to ASCII. Follow-up full Debug and Release builds completed successfully, and each configuration again passed all 46 CTest cases.

## Automated and inspection scope

The user reported that completed generated playback left the button at Stop and required Restart. The controller now publishes completion, the UI restores Play, and the next playback rewinds the source and target on the audio thread. The user confirmed that the fix works. This confirmation covers completion/replay only; it does not qualify the outstanding hardware and listening checks below.

Publication verification includes a callback regression for all five generated sources, exact and partial block endings, and immediate replay or replay after an idle callback. Both full suites passed all 47 cases after this addition; the headless suite passed all 35 cases. CI report destinations use absolute paths so CTest's preset working directory cannot redirect reports away from the artifact upload paths.

The added suites cover mathematical source/reference behavior, arbitrary block partitions, monitor-independent measurements, exact loop/render lengths, format conversion, hashes and schema validation, float output, cancellation and no-overwrite publication, MIDI transport/release, overload persistence, and prepared callback behavior. The dedicated realtime executable uses replaceable global C++ allocation instrumentation on the current thread. This does not cover every C allocator, custom allocator, other thread, scheduler delay, or device driver.

Focused prepared-callback checks passed with zero tracked C++ allocations. Source review found that a fresh GUI loading object initially omitted the previous live loop from its local PCM budget. The loader now accepts external resident bytes and the GUI supplies the retained live PCM, preserving the total 256 MiB budget across replacement. This fix was reviewed and included in the verification above.

GitNexus `detect_changes(scope="all")` included new intent-to-add files: 531 changed symbols across 51 files and 22 affected flows, with CRITICAL scope risk and no partial/truncated indication. The broad harness/UI/renderer/test/build change was reviewed with source and focused checks; the risk was not treated as an all-clear from empty callers. No commit was made as part of documentation work.

No new numerical performance qualification or listening result is claimed. The foundation's reference-gain diagnostic timing observations remain those in its separate note.

## Outstanding manual checks

Actual audio-device/driver audition, hardware MIDI, audible WAV/AIFF loops and keyboard playback, gain/bypass transitions, monitor volume/mute, live meter/overload display, device changes, save/reload, and shutdown during loading/rendering require manual exercise and user listening. These checks remain outstanding until explicitly recorded. Hosted VS2022 CI outcomes require the actual workflow run; local VS2026 builds cannot establish them.

Measurement limitations include unfiltered linear downsampling, exact loop joins without crossfades, and MIDI arrival timing with about one block of delivery delay. Future module acceptance must record its own sound, numerical, aliasing, latency, and performance evidence.
