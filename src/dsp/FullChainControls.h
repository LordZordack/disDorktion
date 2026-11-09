#pragma once
#include "FullChain.h"
#include <array>
#include <string_view>
#include <limits>
namespace disdorktion
{
struct FullChainControl { const char* id; const char* name; float minimum, maximum, defaultValue; bool discrete; };
inline constexpr std::array fullChainControls {
    FullChainControl {"driver", "Driver", 1.0f, 4.0f, 1.0f, false},
    FullChainControl {"waveshaperTrimDb", "Waveshaper trim (dB)", -60.0f, 12.0f, 0.0f, false},
    FullChainControl {"waveshaperMix", "Waveshaper mix", 0.0f, 1.0f, 1.0f, false},
    FullChainControl {"inputGainDb", "Input gain (dB)", -60.0f, 24.0f, 0.0f, false},
    FullChainControl {"outputGainDb", "Final gain (dB)", -60.0f, 24.0f, 0.0f, false},
    FullChainControl {"bitDepth", "Bit depth", 1.0f, 64.0f, 64.0f, true},
    FullChainControl {"captureRateHz", "Capture rate (Hz)", 100.0f, 384000.0f, 384000.0f, false},
    FullChainControl {"crusherMix", "Input mix", 0.0f, 1.0f, 1.0f, false},
    FullChainControl {"crush", "Crush", 0.0f, 1.0f, 0.0f, false},
    FullChainControl {"shape", "Quantizer shape", -1.0f, 1.0f, 0.0f, false},
    FullChainControl {"jitter", "Clock jitter", 0.0f, 1.0f, 0.0f, false},
    FullChainControl {"tanhDriveDb", "Tanh drive (dB)", -24.0f, 36.0f, -24.0f, false},
    FullChainControl {"folderDriveDb", "Folder drive (dB)", -24.0f, 36.0f, -24.0f, false},
    FullChainControl {"folderThreshold", "Folder threshold", 0.05f, 1.0f, 1.0f, false},
    FullChainControl {"hpCutoffHz", "High-pass cutoff (Hz)", 20.0f, 20000.0f, 20.0f, false},
    FullChainControl {"hpResponse", "High-pass response", 0.0f, 2.0f, 0.0f, true},
    FullChainControl {"lpCutoffHz", "Low-pass cutoff (Hz)", 20.0f, 20000.0f, 20000.0f, false},
    FullChainControl {"lpResponse", "Low-pass response", 0.0f, 2.0f, 0.0f, true},
    FullChainControl {"globalMix", "Global dry/wet", 0.0f, 1.0f, 1.0f, false},
    FullChainControl {"combDelayMs", "Comb delay (ms)", 0.25f, 40.0f, 5.0f, false},
    FullChainControl {"combFeedforward", "Comb feedforward", -1.0f, 1.0f, 0.0f, false},
    FullChainControl {"combFeedback", "Comb feedback", -0.95f, 0.95f, 0.7f, false},
    FullChainControl {"combDampingHz", "Comb damping (Hz)", 20.0f, 20000.0f, 6000.0f, false},
    FullChainControl {"combMix", "Comb mix", 0.0f, 1.0f, 0.0f, false},
    FullChainControl {"dispersionFrequencyHz", "Dispersion frequency (Hz)", 20.0f, 20000.0f, 500.0f, false},
    FullChainControl {"dispersionMix", "Dispersion mix", 0.0f, 1.0f, 0.0f, false},
    FullChainControl {"toneDb", "Tone (dB)", -6.0f, 6.0f, 0.0f, false},
};
inline float getFullChainControl(const FullChainSettings& settings, std::string_view id) noexcept
{
    if (id == "driver") return settings.driver;
    if (id == "waveshaperTrimDb") return settings.middle.trimDb;
    if (id == "waveshaperMix") return settings.middle.mix;
    if (id == "inputGainDb") return static_cast<float>(settings.input.gain.gainDb);
    if (id == "outputGainDb") return static_cast<float>(settings.finalGain.gainDb);
    if (id == "bitDepth") return static_cast<float>(settings.input.crusher.bitDepth);
    if (id == "captureRateHz") return static_cast<float>(settings.input.crusher.captureRateHz);
    if (id == "crusherMix") return static_cast<float>(settings.input.crusher.mix);
    if (id == "crush") return static_cast<float>(settings.input.crusher.crush);
    if (id == "shape") return static_cast<float>(settings.input.crusher.shape);
    if (id == "jitter") return static_cast<float>(settings.input.crusher.jitter);
    if (id == "tanhDriveDb") return static_cast<float>(settings.middle.tanh.driveDb);
    if (id == "folderDriveDb") return static_cast<float>(settings.middle.folder.driveDb);
    if (id == "folderThreshold") return static_cast<float>(settings.middle.folder.threshold);
    if (id == "hpCutoffHz") return static_cast<float>(settings.output.highPass.cutoffHz);
    if (id == "hpResponse") return static_cast<float>(settings.output.highPass.response);
    if (id == "lpCutoffHz") return static_cast<float>(settings.output.lowPass.cutoffHz);
    if (id == "lpResponse") return static_cast<float>(settings.output.lowPass.response);
    if (id == "globalMix") return static_cast<float>(settings.mix);
    if (id == "combDelayMs") return static_cast<float>(settings.output.comb.delayMs);
    if (id == "combFeedforward") return static_cast<float>(settings.output.comb.feedforward);
    if (id == "combFeedback") return static_cast<float>(settings.output.comb.feedback);
    if (id == "combDampingHz") return static_cast<float>(settings.output.comb.dampingHz);
    if (id == "combMix") return static_cast<float>(settings.output.comb.mix);
    if (id == "dispersionFrequencyHz") return static_cast<float>(settings.output.dispersion.frequencyHz);
    if (id == "dispersionMix") return static_cast<float>(settings.output.dispersion.mix);
    if (id == "toneDb") return static_cast<float>(settings.output.tone.toneDb);
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
        if (id == "driver") next.driver = value;
        else if (id == "waveshaperTrimDb") next.middle.trimDb = value;
        else if (id == "waveshaperMix") next.middle.mix = value;
        else if (id == "inputGainDb") next.input.gain.gainDb = value;
        else if (id == "outputGainDb") next.finalGain.gainDb = value;
        else if (id == "bitDepth") next.input.crusher.bitDepth = static_cast<unsigned>(value);
        else if (id == "captureRateHz") next.input.crusher.captureRateHz = value;
        else if (id == "crusherMix") next.input.crusher.mix = value;
        else if (id == "crush") next.input.crusher.crush = value;
        else if (id == "shape") next.input.crusher.shape = value;
        else if (id == "jitter") next.input.crusher.jitter = value;
        else if (id == "tanhDriveDb") { next.middle.tanh.driveDb = value; next.middle.tanh.bypass = value == -24.f; next.middle.tanh.outputDb=0.f; }
        else if (id == "folderDriveDb") { next.middle.folder.driveDb = value; next.middle.folder.bypass = value == -24.f; next.middle.folder.outputDb=0.f; }
        else if (id == "folderThreshold") next.middle.folder.threshold = value;
        else if (id == "hpCutoffHz") { next.output.highPass.cutoffHz = value; next.output.highPass.bypass = value == 20.f; }
        else if (id == "hpResponse") next.output.highPass.response = static_cast<FilterResponse>(static_cast<unsigned>(value));
        else if (id == "lpCutoffHz") { next.output.lowPass.cutoffHz = value; next.output.lowPass.bypass = value == 20000.f; }
        else if (id == "lpResponse") next.output.lowPass.response = static_cast<FilterResponse>(static_cast<unsigned>(value));
        else if (id == "globalMix") next.mix = value;
        else if (id == "combDelayMs") next.output.comb.delayMs = value;
        else if (id == "combFeedforward") next.output.comb.feedforward = value;
        else if (id == "combFeedback") next.output.comb.feedback = value;
        else if (id == "combDampingHz") next.output.comb.dampingHz = value;
        else if (id == "combMix") next.output.comb.mix = value;
        else if (id == "dispersionFrequencyHz") next.output.dispersion.frequencyHz = value;
        else if (id == "dispersionMix") next.output.dispersion.mix = value;
        else if (id == "toneDb") next.output.tone.toneDb = value;
        if (!isValidFullChainSettings(next)) return false;
        settings = next; return true;
    }
    return false;
}
}
