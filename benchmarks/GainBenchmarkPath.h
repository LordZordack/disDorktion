#pragma once
#include "dsp/Gain.h"
#include "dsp/ReferenceGain.h"

// Shared timing harness selects a single production instance, a pair, or the
// plugin's legacy trim plus pair. Storage and preparation remain outside timing.
class GainBenchmarkPath
{
public:
    explicit GainBenchmarkPath(unsigned stages) noexcept : count(stages) {}
    bool prepare(const juce::dsp::ProcessSpec& spec) noexcept
    {
        return count >= 1 && count <= 3 && input.prepare(spec) && output.prepare(spec)
            && legacy.setParameters({4.0f, false}) && legacy.prepare(spec);
    }
    bool setParameters(const disdorktion::GainSettings& settings) noexcept
    {
        return input.setParameters(settings) && (count == 1 || output.setParameters(settings));
    }
    bool process(juce::dsp::AudioBlock<float> block) noexcept
    {
        return (count != 3 || legacy.process(block)) && input.process(block)
            && (count == 1 || output.process(block));
    }
private:
    unsigned count;
    disdorktion::Gain input, output;
    disdorktion::ReferenceGain legacy;
};
