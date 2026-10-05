#include "ReferenceGain.h"

namespace disdorktion
{
bool ReferenceGain::prepare(const juce::dsp::ProcessSpec& spec) noexcept
{
    if (!dispatcher.prepare(spec)) return false;
    gainSmoother.reset(spec.sampleRate, 0.010);
    bypassSmoother.reset(spec.sampleRate, 0.010);
    reset();
    return true;
}

void ReferenceGain::reset() noexcept
{
    gainSmoother.setCurrentAndTargetValue(retained.gain);
    bypassSmoother.setCurrentAndTargetValue(retained.bypass ? 1.0f : 0.0f);
}

bool ReferenceGain::setParameters(const GainParameters& parameters) noexcept
{
    if (!std::isfinite(parameters.gain) || parameters.gain < 0.0f || parameters.gain > 4.0f) return false;
    retained = parameters;
    gainSmoother.setTargetValue(retained.gain);
    bypassSmoother.setTargetValue(retained.bypass ? 1.0f : 0.0f);
    return true;
}

bool ReferenceGain::process(juce::dsp::AudioBlock<float> block) noexcept
{
    return dispatcher.process(block, [this](auto chunk) noexcept
    {
        for (std::size_t n = 0; n < chunk.getNumSamples(); ++n)
        {
            const auto g = gainSmoother.getNextValue();
            const auto b = bypassSmoother.getNextValue();
            const auto effectiveGain = b + (1.0f - b) * g;
            for (std::size_t c = 0; c < chunk.getNumChannels(); ++c)
                chunk.getChannelPointer(c)[n] *= effectiveGain;
        }
    });
}
}
