#pragma once
#include "AuditionTarget.h"
#include "dsp/TanhSaturation.h"
#include <array>

namespace disdorktion::harness
{
inline constexpr std::array tanhSaturationControls {
    TargetControl {"driveDb", "Drive (dB)", -24.0f, 36.0f, 0.0f},
    TargetControl {"outputDb", "Output (dB)", -60.0f, 12.0f, 0.0f}
};
inline constexpr TargetDescriptor tanhSaturationDescriptor {"tanh-saturation", "Tanh saturation", tanhSaturationControls};
class TanhSaturationTarget final : public AuditionTarget
{
public:
    const TargetDescriptor& descriptor() const noexcept override { return tanhSaturationDescriptor; }
    bool prepare(const juce::dsp::ProcessSpec& spec) override { return saturation.prepare(spec); }
    void reset() noexcept override { saturation.reset(); }
    bool setControl(std::string_view id, float value) noexcept override
    {
        auto next = settings;
        if (id == "driveDb") next.driveDb = value;
        else if (id == "outputDb") next.outputDb = value;
        else return false;
        return setParameters(next);
    }
    void setBypass(bool value) noexcept override { auto next = settings; next.bypass = value; (void) setParameters(next); }
    bool setParameters(const TanhSaturationSettings& value) noexcept
    {
        if (!saturation.setParameters(value)) return false;
        settings = value;
        return true;
    }
    bool process(juce::dsp::AudioBlock<float> block) noexcept override { return saturation.process(block); }
    unsigned getLatencySamples() const noexcept override { return saturation.getLatencySamples(); }
private:
    TanhSaturation saturation;
    TanhSaturationSettings settings;
};
}
