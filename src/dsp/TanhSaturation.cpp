#include "TanhSaturation.h"

namespace disdorktion
{
bool TanhSaturation::prepare(const juce::dsp::ProcessSpec& spec) noexcept
{
    return prepareOversampled(spec, 1);
}

bool TanhSaturation::prepareOversampled(const juce::dsp::ProcessSpec& spec, unsigned factor) noexcept
{
    if (!dispatcher.prepareOversampled(spec, factor)) return false;
    const auto rate = spec.sampleRate * factor;
    drive.reset(rate, 0.010);
    output.reset(rate, 0.010);
    bypass.reset(rate, 0.010);
    reset();
    return true;
}

void TanhSaturation::reset() noexcept
{
    drive.setCurrentAndTargetValue(driveTarget);
    output.setCurrentAndTargetValue(outputTarget);
    bypass.setCurrentAndTargetValue(retained.bypass ? 1.0 : 0.0);
}

bool TanhSaturation::setParameters(const TanhSaturationSettings& parameters) noexcept
{
    if (!isValidTanhSaturationSettings(parameters)) return false;
    if (parameters.driveDb != retained.driveDb)
        driveTarget = std::pow(10.0, static_cast<double>(parameters.driveDb) / 20.0);
    if (parameters.outputDb != retained.outputDb)
        outputTarget = std::pow(10.0, static_cast<double>(parameters.outputDb) / 20.0);
    retained = parameters;
    drive.setTargetValue(driveTarget);
    output.setTargetValue(outputTarget);
    bypass.setTargetValue(retained.bypass ? 1.0 : 0.0);
    return true;
}

bool TanhSaturation::process(juce::dsp::AudioBlock<float> block) noexcept
{
    return dispatcher.process(block, [this](auto chunk) noexcept
    {
        for (std::size_t frame = 0; frame < chunk.getNumSamples(); ++frame)
        {
            // Advance all controls once per frame, including while fully bypassed.
            const double a = drive.getNextValue();
            const double g = output.getNextValue();
            const double b = bypass.getNextValue();
            if (b == 1.0) continue;
            for (std::size_t channel = 0; channel < chunk.getNumChannels(); ++channel)
            {
                auto& sample = chunk.getChannelPointer(channel)[frame];
                const double dry = sample;
                // Double intermediates support every finite float input at maximum drive.
                const double wet = g * std::tanh(a * dry);
                sample = static_cast<float>(b == 0.0 ? wet : b * dry + (1.0 - b) * wet);
            }
        }
    });
}
}
