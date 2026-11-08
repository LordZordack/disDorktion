---
type: Method
title: "Plugin interface, presets and host state"
description: "Presentation and lifecycle rules for the shared production chain without changing the host parameter schema."
timestamp: 2026-10-17T18:00:00Z
tags: ["audio-plugin", "automation", "state", "presets", "metering"]
resource: "https://github.com/LordZordack/disDorktion/issues/14"
---

# Plugin interface, presets and host state

The custom editor exposes the [full chain](./full_chain.md) in three visible
processing panels, with global mix/final gain and compatibility controls.
Continuous controls use rotary knobs with editable values below them. The
default editor is 1120x740, with a 1000x680 minimum; Global and Compatibility
share a row to preserve screen space. This replaces the initial 1120x850
slider layout without changing parameter ranges or host normalization.
The repository guide `docs/PLUGIN_INTERFACE.md` records ranges, defaults,
host ordering, bypass semantics and versioned restore policy. The UI reuses
production DSP; it introduces no limiter, normalization or alternative quality
path. The [validation note](../notes/plugin_interface_validation.md) separates
implementation rules from measured acceptance.

## Stable sound representation

The original 30 APVTS parameters remain at their original indices; 12
Output parameters append. IDs, ordering and normalized mappings retain host session meaning.
Readable units, percent formatting, response names and frequency slider skew
belong to presentation. In particular, host bit-depth indices are decoded to
the actual permitted DSP bit values before validation or serialization.

The five factory settings are complete production settings. Preset names are
derived by comparing quantized normalized controls within 2e-6, excluding only
output mute and requiring unity/unbypassed legacy trim. Derived identity
survives editor reconstruction and state round trips without new metadata.
Ordinary presets preserve output mute; Initialize restores all defaults.

## Thread ownership and transitions

Host controls are independent atomic scalars latched once per callback, not
an atomic bank. Recall uses ordinary host gestures on the message thread.
It publishes a requested full-chain mode without taking the callback lock;
the audio owner applies the mode and clears histories at the next callback.
Host latency metadata updates belong to message/lifecycle operations. No
callback means the requested mode has not yet reached DSP. Existing 10 ms
ramps do not promise a seamless latency-path transition.

Host restore remains serialized with processor lifecycle. It fully validates
the candidate before changing parameters, notifies listeners outside the
callback lock and synchronizes mode/DSP ownership. State saving takes the
callback lock. Version 1/2 migrations retain gain-only mode; version 3 records
the mode and all chain fields. Malformed, incomplete required, invalid and
future state is rejected as a whole. Unknown extra attributes are ignored.

## Observable levels

Raw bus input and actual returned output are scanned with bounded audio work,
including the exact delayed raw output selected by host bypass. Atomic peak
maximums retain transients between editor polls and overload latches retain
amplitudes above unity. One producer and one exchanging consumer can repeat
an earlier peak for one callback without losing a new peak. Visual decay runs
on the editor timer. Zero-length blocks retain unread peaks; reset/release
clear processor meter history. These final meters complement the chain's
internal diagnostic taps; dry selection can conceal wet-path overload from
the final output meter.

See [full-chain qualification](../notes/full_chain_validation.md) for inherited
DSP evidence and its pending human listening, CPU and host gates.


## Issue #20 integration update

The [Output section](output_section.md) now runs comb -> frequency dispersion
-> tone -> HP -> LP at native rate. All additions default to bypass. Original
30 host parameters keep their order and meanings; 12 new controls append.
Plugin state v4 restores old v1/v2 gain-only sessions and v3 modes with the
new effects bypassed. Successful restore clears full-chain histories and snaps its targets; legacy
gain-only scalar ramps retain their original behavior.
Full-chain latency remains 92 samples; partial dry/wet can color phase-shaped
wet audio. Full mode reports a conservative 20-second tail plus FIR latency,
even while bypassed. Existing five factory sounds are retained.

The editor defaults to 1120x740 with a 1000x680 minimum and 1400x1000 maximum,
providing more space for Output controls. Old evidence below describes the
prior interface; see the current `docs/PLUGIN_INTERFACE.md`.
