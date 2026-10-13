#include "InputSection.h"
#include <algorithm>

namespace disdorktion
{
bool InputSection::prepare(const juce::dsp::ProcessSpec& spec) noexcept
{
    if (!isValidProcessSpec(spec)) return false;
    // All three share this validated domain; no child can reject or allocate.
    (void) dispatcher.prepare(spec);
    (void) gain.prepare(spec);
    (void) crusher.prepare(spec);
    postGainPeak = 0.0f;
    return true;
}
void InputSection::reset() noexcept
{
    gain.reset();
    crusher.reset();
    postGainPeak = 0.0f;
}
bool InputSection::setParameters(const InputSettings& settings) noexcept
{
    if (!isValidInputSettings(settings)) return false;
    (void) gain.setParameters(settings.gain);
    (void) crusher.setParameters(settings.crusher);
    return true;
}
bool InputSection::process(juce::dsp::AudioBlock<float> block) noexcept
{
    float peak = 0.0f;
    const auto valid = dispatcher.process(block, [this, &peak](auto chunk) noexcept
    {
        (void) gain.process(chunk);
        for (std::size_t channel = 0; channel < chunk.getNumChannels(); ++channel)
            for (std::size_t frame = 0; frame < chunk.getNumSamples(); ++frame)
                peak = std::max(peak, std::abs(chunk.getChannelPointer(channel)[frame]));
        (void) crusher.process(chunk);
    });
    if (valid && block.getNumSamples() != 0) postGainPeak = peak;
    return valid;
}
}
