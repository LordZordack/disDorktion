#pragma once
#include "AuditionTarget.h"
#include "dsp/InputSection.h"
#include <array>
#include <cmath>

namespace disdorktion::harness
{
inline constexpr std::array inputSectionControls {
    TargetControl {"gainDb", "Input gain (dB)", -60.f, 24.f, 0.f},
    TargetControl {"mute", "Mute gain", 0.f, 1.f, 0.f},
    TargetControl {"gainBypass", "Bypass gain", 0.f, 1.f, 0.f},
    TargetControl {"crusherBypass", "Bypass crusher", 0.f, 1.f, 0.f},
    TargetControl {"bitDepth", "Bit depth", 1.f, 64.f, 64.f},
    TargetControl {"captureRateHz", "Capture rate (Hz)", 100.f, 384000.f, 384000.f},
    TargetControl {"mix", "Mix", 0.f, 1.f, 1.f},
    TargetControl {"crush", "Crush", 0.f, 1.f, 0.f},
    TargetControl {"shape", "Shape", -1.f, 1.f, 0.f},
    TargetControl {"jitter", "Jitter", 0.f, 1.f, 0.f}
};
inline constexpr TargetDescriptor inputSectionDescriptor {"input-section", "Input section", inputSectionControls};

class InputSectionTarget final : public AuditionTarget
{
public:
    const TargetDescriptor& descriptor() const noexcept override { return inputSectionDescriptor; }
    bool prepare(const juce::dsp::ProcessSpec& spec) override { return section.prepare(spec); }
    void reset() noexcept override { section.reset(); }
    bool setParameters(const InputSettings& next) noexcept
    {
        if (!isValidInputSettings(next)) return false;
        auto effective = next;
        effective.gain.bypass |= commonBypass;
        effective.crusher.bypass |= commonBypass;
        if (!section.setParameters(effective)) return false;
        retained = next;
        return true;
    }
    bool setControl(std::string_view id, float value) noexcept override
    {
        auto next = retained;
        if (id == "gainDb") next.gain.gainDb = value;
        else if (id == "captureRateHz") next.crusher.captureRateHz = value;
        else if (id == "mix") next.crusher.mix = value;
        else if (id == "crush") next.crusher.crush = value;
        else if (id == "shape") next.crusher.shape = value;
        else if (id == "jitter") next.crusher.jitter = value;
        else if (id == "bitDepth")
        {
            if (!std::isfinite(value) || value < 1.f || value > 64.f || value != std::floor(value)) return false;
            next.crusher.bitDepth = static_cast<unsigned>(value);
        }
        else if ((id == "mute" || id == "gainBypass" || id == "crusherBypass") && (value == 0.f || value == 1.f))
        {
            if (id == "mute") next.gain.mute = value == 1.f;
            else if (id == "gainBypass") next.gain.bypass = value == 1.f;
            else next.crusher.bypass = value == 1.f;
        }
        else return false;
        return setParameters(next);
    }
    void setBypass(bool value) noexcept override
    {
        commonBypass = value;
        (void) setParameters(retained);
    }
    bool process(juce::dsp::AudioBlock<float> block) noexcept override { return section.process(block); }
    unsigned getLatencySamples() const noexcept override { return section.getLatencySamples(); }
    // Processing-thread access only; AuditionEngine publishes its own atomic copy.
    float getPostGainPeak() const noexcept { return section.getPostGainPeak(); }
private:
    InputSection section;
    InputSettings retained;
    bool commonBypass = false;
};
}
