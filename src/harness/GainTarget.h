#pragma once
#include "AuditionTarget.h"
#include "dsp/Gain.h"
#include <array>

namespace disdorktion::harness
{
inline constexpr std::array gainControls { TargetControl {"gainDb", "Gain (dB)", -60.0f, 24.0f, 0.0f},
                                         TargetControl {"mute", "Module mute", 0.0f, 1.0f, 0.0f} };
inline constexpr TargetDescriptor gainDescriptor {"gain", "Gain", gainControls};
class GainTarget final : public AuditionTarget
{
public:
    const TargetDescriptor& descriptor() const noexcept override { return gainDescriptor; }
    bool prepare(const juce::dsp::ProcessSpec& spec) override { return gain.prepare(spec); }
    void reset() noexcept override { gain.reset(); }
    bool setControl(std::string_view id, float value) noexcept override
    {
        auto next = parameters;
        if (id == "gainDb") next.gainDb = value;
        else if (id == "mute" && (value == 0.0f || value == 1.0f)) next.mute = value == 1.0f;
        else return false;
        return setParameters(next);
    }
    void setBypass(bool value) noexcept override { auto next = parameters; next.bypass = value; (void) setParameters(next); }
    bool setParameters(const GainSettings& value) noexcept
    {
        if (!gain.setParameters(value)) return false;
        parameters = value; return true;
    }
    bool process(juce::dsp::AudioBlock<float> block) noexcept override { return gain.process(block); }
    unsigned getLatencySamples() const noexcept override { return gain.getLatencySamples(); }
private:
    Gain gain;
    GainSettings parameters;
};
}
