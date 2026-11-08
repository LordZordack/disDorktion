#include "OutputSection.h"
#include <algorithm>

namespace disdorktion
{
bool OutputSection::prepare(const juce::dsp::ProcessSpec& spec) noexcept
{
    if (!isValidProcessSpec(spec)) return false;
    // Comb is the only allocation/failure point after spec validation. Its
    // prepare is transactional; do it before resetting any other live stage.
    if (!comb.prepare(spec)) return false;
    (void) dispersion.prepare(spec);
    (void) tone.prepare(spec);
    (void) dispatcher.prepare(spec);
    (void) highPass.prepare(spec);
    (void) lowPass.prepare(spec);
    postHighPassPeak = 0.f;
    return true;
}
void OutputSection::reset() noexcept
{
    comb.reset(); dispersion.reset(); tone.reset();
    highPass.reset(); lowPass.reset(); postHighPassPeak = 0.f;
}
bool OutputSection::setParameters(const OutputSettings& settings) noexcept
{
    if (!isValidOutputSettings(settings)) return false;
    (void) comb.setParameters(settings.comb);
    (void) dispersion.setParameters(settings.dispersion);
    (void) tone.setParameters(settings.tone);
    (void) highPass.setParameters(settings.highPass);
    (void) lowPass.setParameters(settings.lowPass);
    return true;
}
bool OutputSection::process(juce::dsp::AudioBlock<float> block) noexcept
{
    float peak = 0.f;
    const auto valid = dispatcher.process(block, [this, &peak](auto chunk) noexcept
    {
        (void) comb.process(chunk);
        (void) dispersion.process(chunk);
        (void) tone.process(chunk);
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
