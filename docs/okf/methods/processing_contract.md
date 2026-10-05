---
type: Method
title: "Real-time DSP processing contract"
description: "Implemented module interface and callback behavior for DisDorktion's JUCE DSP foundation, including the reference gain processor."
timestamp: 2026-10-04T00:00:00Z
tags: ["dsp", "real-time-audio", "processing-contract", "gain"]
resource: "https://github.com/LordZordack/DisDorktion/blob/main/docs/DSP_CONTRACT.md"
---

# Real-time DSP processing contract

This method records the implemented interface, storage, threading, block handling, reset, latency, and adapter rules for DisDorktion's DSP foundation. The detailed [DSP contract](https://github.com/LordZordack/DisDorktion/blob/main/docs/DSP_CONTRACT.md) defines the signatures and reference gain behavior. The [DisDorktion project](../projects/disdorktion.md) tracks how this foundation fits the plugin.

## Design

Modules prepare against `juce::dsp::ProcessSpec` and process non-owning planar `juce::dsp::AudioBlock<float>` views. Preparation is the only allocation point. Processing preserves channel and sample counts, including oversized inputs dispatched in bounded sub-blocks. Histories are independent per channel and instance; controls advance once per sample frame.

The reference gain accepts finite linear amplitude in `[0, 4]`, defaults to unity, and uses 10 ms gain and bypass blends. Invalid updates preserve the prior complete parameter set. Reset clears audio history and snaps smoothing state to retained targets. The adapter clears rejected audio blocks and synchronizes zero latency after successful preparation.

## Evaluation status

The reference gain contract is implemented and its unit correctness is verified; see the [foundation validation note](../notes/foundation_validation.md). Production distortion modules remain unqualified. CPU performance is not qualified; the recorded benchmark work was performed on battery power. The proposed budgets and benchmark procedure are in [PERFORMANCE.md](https://github.com/LordZordack/DisDorktion/blob/main/docs/PERFORMANCE.md).

## Design choices

A typed structural C++20 contract lets each module validate its own controls without a universal parameter object or inheritance tree. JUCE audio views and smoothing reuse the pinned framework; a JUCE-free core would need its own view and smoothing infrastructure. Keeping DSP outside `AudioProcessor` lets headless tests exercise it without editor or host state. The adapter handles host controls, persistence and failure output while sharing the reference implementation with VST3 and Standalone.
