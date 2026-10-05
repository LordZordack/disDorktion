#pragma once
#include "BlockDispatcher.h"

namespace disdorktion
{
struct GainParameters
{
    float gain = 1.0f;
    bool bypass = false;
};

class ReferenceGain
{
public:
    bool prepare(const juce::dsp::ProcessSpec& spec) noexcept;
    void reset() noexcept;
    bool setParameters(const GainParameters& parameters) noexcept;
    bool process(juce::dsp::AudioBlock<float> block) noexcept;
    unsigned int getLatencySamples() const noexcept { return 0; }
private:
    BlockDispatcher dispatcher;
    GainParameters retained;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmoother {1.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> bypassSmoother {0.0f};
};
}
