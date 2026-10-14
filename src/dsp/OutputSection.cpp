#include "OutputSection.h"
#include <algorithm>

namespace disdorktion
{
bool OutputSection::prepare(const juce::dsp::ProcessSpec& spec) noexcept
{
    if (!isValidProcessSpec(spec)) return false;
    // All three use the same validated domain and cannot allocate or reject it.
    (void) dispatcher.prepare(spec);
    (void) highPass.prepare(spec);
    (void) lowPass.prepare(spec);
    postHighPassPeak = 0.f;
    return true;
}
void OutputSection::reset() noexcept
{
    highPass.reset(); lowPass.reset(); postHighPassPeak = 0.f;
}
bool OutputSection::setParameters(const OutputSettings& settings) noexcept
{
    if (!isValidOutputSettings(settings)) return false;
    (void) highPass.setParameters(settings.highPass);
    (void) lowPass.setParameters(settings.lowPass);
    return true;
}
bool OutputSection::process(juce::dsp::AudioBlock<float> block) noexcept
{
    float peak = 0.f;
    const auto valid = dispatcher.process(block, [this, &peak](auto chunk) noexcept
    {
        (void) highPass.process(chunk);
        for (std::size_t channel=0; channel<chunk.getNumChannels(); ++channel)
            for (std::size_t frame=0; frame<chunk.getNumSamples(); ++frame)
                peak = std::max(peak, std::abs(chunk.getChannelPointer(channel)[frame]));
        (void) lowPass.process(chunk);
    });
    if (valid && block.getNumSamples()!=0) postHighPassPeak = peak;
    return valid;
}
}
