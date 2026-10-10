#include "Gain.h"
#include <cmath>

namespace disdorktion
{
bool Gain::prepare(const juce::dsp::ProcessSpec& spec) noexcept
{
    if (!dispatcher.prepare(spec)) return false;
    gainSmoother.reset(spec.sampleRate, 0.010);
    bypassSmoother.reset(spec.sampleRate, 0.010);
    reset();
    return true;
}

void Gain::reset() noexcept
{
    gainSmoother.setCurrentAndTargetValue(retained.mute ? 0.0 : linearTarget);
    bypassSmoother.setCurrentAndTargetValue(retained.bypass ? 1.0 : 0.0);
}

bool Gain::setParameters(const GainSettings& settings) noexcept
{
    if (!std::isfinite(settings.gainDb) || settings.gainDb < -60.0f || settings.gainDb > 24.0f)
        return false;

    // Retain the converted dB target even while mute or bypass is active.
    // Identical controls avoid conversion; JUCE preserves identical ramp targets.
    if (settings.gainDb != retained.gainDb)
        linearTarget = std::pow(10.0, static_cast<double>(settings.gainDb) / 20.0);
    retained = settings;
    gainSmoother.setTargetValue(retained.mute ? 0.0 : linearTarget);
    bypassSmoother.setTargetValue(retained.bypass ? 1.0 : 0.0);
    return true;
}

bool Gain::process(juce::dsp::AudioBlock<float> block) noexcept
{
    return dispatcher.process(block, [this](auto chunk) noexcept
    {
        for (std::size_t n = 0; n < chunk.getNumSamples(); ++n)
        {
            const auto g = gainSmoother.getNextValue();
            const auto b = bypassSmoother.getNextValue();
            const auto effectiveGain = static_cast<float>(b + (1.0 - b) * g);
            for (std::size_t c = 0; c < chunk.getNumChannels(); ++c)
                chunk.getChannelPointer(c)[n] *= effectiveGain;
        }
    });
}
}
