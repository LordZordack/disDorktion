#pragma once
#include "dsp/FullChainControls.h"

namespace disdorktion
{
// Plugin startup/reset defaults are intentionally separate from DSP/harness defaults.
inline FullChainSettings pluginDefaultSettings()
{
    FullChainSettings settings;
    setFullChainControl(settings, "tanhDriveDb", 0);
    setFullChainControl(settings, "folderDriveDb", 0);
    return settings;
}
struct FactoryPreset
{
    const char* key;
    const char* name;
    FullChainSettings settings;
};

// Explicit settings reuse production validation and smoothing; no new DSP path.
inline const std::array<FactoryPreset, 5> factoryPresets = []
{
    std::array<FactoryPreset, 5> presets {{
        {"initialize", "Initialize", {}},
        {"subtle-warmth", "Subtle Warmth", {}},
        {"crunch", "Crunch", {}},
        {"folded", "Folded", {}},
        {"digital-destruction", "Digital Destruction", {}}
    }};
    presets[0].settings = pluginDefaultSettings();
    auto& subtle = presets[1].settings;
    subtle.middle.tanh.driveDb = 3;
    subtle.middle.trimDb = -2;
    subtle.middle.folder.driveDb = -24;
    subtle.middle.folder.bypass = true;
    subtle.mix = 0.45f;
    subtle.finalGain.gainDb = -0.5;
    auto& crunch = presets[2].settings;
    crunch.input.crusher.bitDepth = 8;
    crunch.input.crusher.captureRateHz = 18000;
    crunch.middle.tanh.driveDb = 9;
    crunch.middle.trimDb = -6;
    crunch.middle.folder.driveDb = -24;
    crunch.middle.folder.bypass = true;
    crunch.output.lowPass.cutoffHz = 10000;
    crunch.finalGain.gainDb = -2.5;
    auto& folded = presets[3].settings;
    folded.middle.tanh.driveDb = 6;
    folded.middle.folder.driveDb = 9;
    folded.middle.folder.threshold = 0.45;
    folded.middle.trimDb = -3;
    folded.output.highPass.cutoffHz = 60;
    folded.finalGain.gainDb = -1.5;
    auto& digital = presets[4].settings;
    digital.input.crusher.bitDepth = 6;
    digital.input.crusher.captureRateHz = 6000;
    digital.input.crusher.crush = 0.65;
    digital.input.crusher.shape = 0.3;
    digital.input.crusher.jitter = 0.15;
    digital.middle.tanh.driveDb = 15;
    digital.middle.trimDb = -6;
    digital.middle.folder.driveDb = 12;
    digital.middle.folder.threshold = 0.3;
    digital.output.highPass.cutoffHz = 80;
    digital.output.lowPass.cutoffHz = 7000;
    digital.finalGain.gainDb = -1;
    // Canonicalize hidden Off flags through the same mapping used by hosts.
    for (auto& preset : presets)
        for (const auto& control : fullChainControls)
            setFullChainControl(preset.settings, control.id, getFullChainControl(preset.settings, control.id));
    return presets;
}();
}
