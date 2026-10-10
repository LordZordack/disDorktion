#pragma once
#include "BlockDispatcher.h"

namespace disdorktion
{
struct GainSettings
{
    float gainDb = 0.0f;
    bool mute = false;
    bool bypass = false;
};

// Control and processing calls require exclusive ownership, as defined by the
// module contract. Each instance owns its envelopes and retained dB target.
class Gain
{
public:
    bool prepare(const juce::dsp::ProcessSpec& spec) noexcept;
    void reset() noexcept;
    bool setParameters(const GainSettings& settings) noexcept;
    bool process(juce::dsp::AudioBlock<float> block) noexcept;
    unsigned int getLatencySamples() const noexcept { return 0; }

private:
    BlockDispatcher dispatcher;
    GainSettings retained;
    double linearTarget = 1.0;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> gainSmoother {1.0};
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> bypassSmoother {0.0};
};
}
