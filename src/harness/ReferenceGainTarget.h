#pragma once
#include "AuditionTarget.h"
#include "dsp/ReferenceGain.h"
#include <array>

namespace disdorktion::harness
{
inline constexpr std::array referenceGainControls { TargetControl {"gain", "Gain", 0.0f, 4.0f, 1.0f} };
inline constexpr TargetDescriptor referenceGainDescriptor {"reference-gain", "Reference gain", referenceGainControls};
inline constexpr std::array targetRegistry { referenceGainDescriptor };
inline constexpr std::span<const TargetDescriptor> auditionTargets() noexcept { return targetRegistry; }

class ReferenceGainTarget final : public AuditionTarget
{
public:
    const TargetDescriptor& descriptor() const noexcept override { return referenceGainDescriptor; }
    bool prepare(const juce::dsp::ProcessSpec& spec) override { return gain.prepare(spec); }
    void reset() noexcept override { gain.reset(); }
    bool setControl(std::string_view id, float value) noexcept override
    {
        if (id != "gain") return false;
        return setParameters({value, parameters.bypass});
    }
    void setBypass(bool bypass) noexcept override { (void) setParameters({parameters.gain, bypass}); }
    bool setParameters(const GainParameters& value) noexcept
    {
        if (!gain.setParameters(value)) return false;
        parameters = value;
        return true;
    }
    bool process(juce::dsp::AudioBlock<float> block) noexcept override { return gain.process(block); }
    unsigned int getLatencySamples() const noexcept override { return gain.getLatencySamples(); }
private:
    ReferenceGain gain;
    GainParameters parameters;
};
}
