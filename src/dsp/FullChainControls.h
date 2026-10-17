#pragma once
#include "FullChain.h"
#include <array>
#include <string_view>
#include <limits>
namespace disdorktion
{
struct FullChainControl { const char* id; const char* name; float minimum, maximum, defaultValue; bool discrete; };
inline constexpr std::array fullChainControls {
    FullChainControl {"inputGainDb", "Input gain (dB)", -60.0f, 24.0f, 0.0f, false},
    FullChainControl {"inputMute", "Mute input", 0.0f, 1.0f, 0.0f, true},
    FullChainControl {"inputBypass", "Bypass input gain", 0.0f, 1.0f, 0.0f, true},
    FullChainControl {"outputGainDb", "Final gain (dB)", -60.0f, 24.0f, 0.0f, false},
    FullChainControl {"outputMute", "Mute final gain", 0.0f, 1.0f, 0.0f, true},
    FullChainControl {"outputBypass", "Bypass final gain", 0.0f, 1.0f, 0.0f, true},
    FullChainControl {"bitDepth", "Bit depth", 1.0f, 64.0f, 64.0f, true},
    FullChainControl {"captureRateHz", "Capture rate (Hz)", 100.0f, 384000.0f, 384000.0f, false},
    FullChainControl {"crusherBypass", "Bypass crusher", 0.0f, 1.0f, 0.0f, true},
    FullChainControl {"crusherMix", "Crusher mix", 0.0f, 1.0f, 1.0f, false},
    FullChainControl {"crush", "Crush", 0.0f, 1.0f, 0.0f, false},
    FullChainControl {"shape", "Quantizer shape", -1.0f, 1.0f, 0.0f, false},
    FullChainControl {"jitter", "Clock jitter", 0.0f, 1.0f, 0.0f, false},
    FullChainControl {"tanhDriveDb", "Tanh drive (dB)", -24.0f, 36.0f, 0.0f, false},
    FullChainControl {"tanhOutputDb", "Tanh trim (dB)", -60.0f, 12.0f, 0.0f, false},
    FullChainControl {"tanhBypass", "Bypass tanh", 0.0f, 1.0f, 0.0f, true},
    FullChainControl {"folderDriveDb", "Folder drive (dB)", -24.0f, 36.0f, 0.0f, false},
    FullChainControl {"folderOutputDb", "Folder trim (dB)", -60.0f, 12.0f, 0.0f, false},
    FullChainControl {"folderThreshold", "Folder threshold", 0.05f, 1.0f, 1.0f, false},
    FullChainControl {"folderBypass", "Bypass folder", 0.0f, 1.0f, 0.0f, true},
    FullChainControl {"hpCutoffHz", "High-pass cutoff (Hz)", 20.0f, 20000.0f, 20.0f, false},
    FullChainControl {"hpResponse", "High-pass response", 0.0f, 2.0f, 0.0f, true},
    FullChainControl {"hpBypass", "Bypass high-pass", 0.0f, 1.0f, 0.0f, true},
    FullChainControl {"lpCutoffHz", "Low-pass cutoff (Hz)", 20.0f, 20000.0f, 20000.0f, false},
    FullChainControl {"lpResponse", "Low-pass response", 0.0f, 2.0f, 0.0f, true},
    FullChainControl {"lpBypass", "Bypass low-pass", 0.0f, 1.0f, 0.0f, true},
    FullChainControl {"globalMix", "Global dry/wet", 0.0f, 1.0f, 1.0f, false},
    FullChainControl {"chainBypass", "Bypass chain", 0.0f, 1.0f, 0.0f, true}
};
inline float getFullChainControl(const FullChainSettings& settings, std::string_view id) noexcept
{
    if (id == "inputGainDb") return static_cast<float>(settings.input.gain.gainDb);
    if (id == "inputMute") return static_cast<float>(settings.input.gain.mute);
    if (id == "inputBypass") return static_cast<float>(settings.input.gain.bypass);
    if (id == "outputGainDb") return static_cast<float>(settings.finalGain.gainDb);
    if (id == "outputMute") return static_cast<float>(settings.finalGain.mute);
    if (id == "outputBypass") return static_cast<float>(settings.finalGain.bypass);
    if (id == "bitDepth") return static_cast<float>(settings.input.crusher.bitDepth);
    if (id == "captureRateHz") return static_cast<float>(settings.input.crusher.captureRateHz);
    if (id == "crusherBypass") return static_cast<float>(settings.input.crusher.bypass);
    if (id == "crusherMix") return static_cast<float>(settings.input.crusher.mix);
    if (id == "crush") return static_cast<float>(settings.input.crusher.crush);
    if (id == "shape") return static_cast<float>(settings.input.crusher.shape);
    if (id == "jitter") return static_cast<float>(settings.input.crusher.jitter);
    if (id == "tanhDriveDb") return static_cast<float>(settings.middle.tanh.driveDb);
    if (id == "tanhOutputDb") return static_cast<float>(settings.middle.tanh.outputDb);
    if (id == "tanhBypass") return static_cast<float>(settings.middle.tanh.bypass);
    if (id == "folderDriveDb") return static_cast<float>(settings.middle.folder.driveDb);
    if (id == "folderOutputDb") return static_cast<float>(settings.middle.folder.outputDb);
    if (id == "folderThreshold") return static_cast<float>(settings.middle.folder.threshold);
    if (id == "folderBypass") return static_cast<float>(settings.middle.folder.bypass);
    if (id == "hpCutoffHz") return static_cast<float>(settings.output.highPass.cutoffHz);
    if (id == "hpResponse") return static_cast<float>(settings.output.highPass.response);
    if (id == "hpBypass") return static_cast<float>(settings.output.highPass.bypass);
    if (id == "lpCutoffHz") return static_cast<float>(settings.output.lowPass.cutoffHz);
    if (id == "lpResponse") return static_cast<float>(settings.output.lowPass.response);
    if (id == "lpBypass") return static_cast<float>(settings.output.lowPass.bypass);
    if (id == "globalMix") return static_cast<float>(settings.mix);
    if (id == "chainBypass") return static_cast<float>(settings.bypass);
    return std::numeric_limits<float>::quiet_NaN();
}
inline bool setFullChainControl(FullChainSettings& settings, std::string_view id, float value) noexcept
{
    for (const auto& control : fullChainControls)
    {
        if (id != control.id) continue;
        if (!std::isfinite(value) || value < control.minimum || value > control.maximum
            || (control.discrete && value != std::floor(value))) return false;
        auto next = settings;
        if (id == "inputGainDb") next.input.gain.gainDb = value;
        else if (id == "inputMute") next.input.gain.mute = value != 0.f;
        else if (id == "inputBypass") next.input.gain.bypass = value != 0.f;
        else if (id == "outputGainDb") next.finalGain.gainDb = value;
        else if (id == "outputMute") next.finalGain.mute = value != 0.f;
        else if (id == "outputBypass") next.finalGain.bypass = value != 0.f;
        else if (id == "bitDepth") next.input.crusher.bitDepth = static_cast<unsigned>(value);
        else if (id == "captureRateHz") next.input.crusher.captureRateHz = value;
        else if (id == "crusherBypass") next.input.crusher.bypass = value != 0.f;
        else if (id == "crusherMix") next.input.crusher.mix = value;
        else if (id == "crush") next.input.crusher.crush = value;
        else if (id == "shape") next.input.crusher.shape = value;
        else if (id == "jitter") next.input.crusher.jitter = value;
        else if (id == "tanhDriveDb") next.middle.tanh.driveDb = value;
        else if (id == "tanhOutputDb") next.middle.tanh.outputDb = value;
        else if (id == "tanhBypass") next.middle.tanh.bypass = value != 0.f;
        else if (id == "folderDriveDb") next.middle.folder.driveDb = value;
        else if (id == "folderOutputDb") next.middle.folder.outputDb = value;
        else if (id == "folderThreshold") next.middle.folder.threshold = value;
        else if (id == "folderBypass") next.middle.folder.bypass = value != 0.f;
        else if (id == "hpCutoffHz") next.output.highPass.cutoffHz = value;
        else if (id == "hpResponse") next.output.highPass.response = static_cast<FilterResponse>(static_cast<unsigned>(value));
        else if (id == "hpBypass") next.output.highPass.bypass = value != 0.f;
        else if (id == "lpCutoffHz") next.output.lowPass.cutoffHz = value;
        else if (id == "lpResponse") next.output.lowPass.response = static_cast<FilterResponse>(static_cast<unsigned>(value));
        else if (id == "lpBypass") next.output.lowPass.bypass = value != 0.f;
        else if (id == "globalMix") next.mix = value;
        else if (id == "chainBypass") next.bypass = value != 0.f;
        if (!isValidFullChainSettings(next)) return false;
        settings = next; return true;
    }
    return false;
}
}
