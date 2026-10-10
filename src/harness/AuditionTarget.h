#pragma once
#include <juce_dsp/juce_dsp.h>
#include <span>
#include <string_view>

namespace disdorktion::harness
{
struct TargetControl
{
    std::string_view id;
    std::string_view name;
    float minimum;
    float maximum;
    float defaultValue;
};

struct TargetDescriptor
{
    std::string_view id;
    std::string_view name;
    std::span<const TargetControl> controls;
};

// Harness adapters translate named controls to each module's own typed parameters.
// Target lifetime and preparation are owned outside the processing callback.
class AuditionTarget
{
public:
    virtual ~AuditionTarget() = default;
    virtual const TargetDescriptor& descriptor() const noexcept = 0;
    virtual bool prepare(const juce::dsp::ProcessSpec&) = 0;
    virtual void reset() noexcept = 0;
    virtual bool setControl(std::string_view id, float value) noexcept = 0;
    virtual void setBypass(bool bypass) noexcept = 0;
    virtual bool process(juce::dsp::AudioBlock<float>) noexcept = 0;
    virtual unsigned int getLatencySamples() const noexcept = 0;
};
}
