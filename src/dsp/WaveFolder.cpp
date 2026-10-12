#include "WaveFolder.h"

namespace disdorktion
{
bool WaveFolder::prepare(const juce::dsp::ProcessSpec& spec) noexcept
{
    if (!dispatcher.prepare(spec)) return false;
    drive.reset(spec.sampleRate, 0.010);
    threshold.reset(spec.sampleRate, 0.010);
    output.reset(spec.sampleRate, 0.010);
    bypass.reset(spec.sampleRate, 0.010);
    reset();
    return true;
}

void WaveFolder::reset() noexcept
{
    drive.setCurrentAndTargetValue(driveTarget);
    threshold.setCurrentAndTargetValue(static_cast<double>(retained.threshold));
    output.setCurrentAndTargetValue(outputTarget);
    bypass.setCurrentAndTargetValue(retained.bypass ? 1.0 : 0.0);
}

bool WaveFolder::setParameters(const WaveFolderSettings& parameters) noexcept
{
    if (!isValidWaveFolderSettings(parameters)) return false;
    if (parameters.driveDb != retained.driveDb)
        driveTarget = std::pow(10.0, static_cast<double>(parameters.driveDb) / 20.0);
    if (parameters.outputDb != retained.outputDb)
        outputTarget = std::pow(10.0, static_cast<double>(parameters.outputDb) / 20.0);
    retained = parameters;
    drive.setTargetValue(driveTarget);
    threshold.setTargetValue(static_cast<double>(retained.threshold));
    output.setTargetValue(outputTarget);
    bypass.setTargetValue(retained.bypass ? 1.0 : 0.0);
    return true;
}

bool WaveFolder::process(juce::dsp::AudioBlock<float> block) noexcept
{
    return dispatcher.process(block, [this](auto chunk) noexcept
    {
        for (std::size_t frame = 0; frame < chunk.getNumSamples(); ++frame)
        {
            // All four controls advance once per frame, including while fully bypassed.
            const double a = drive.getNextValue();
            const double t = threshold.getNextValue();
            const double g = output.getNextValue();
            const double b = bypass.getNextValue();
            if (b == 1.0) continue;
            for (std::size_t channel = 0; channel < chunk.getNumChannels(); ++channel)
            {
                auto& sample = chunk.getChannelPointer(channel)[frame];
                const double dry = sample;
                // Double intermediates remain finite for every finite float input.
                const double z = a * dry;
                double folded = z;
                if (std::abs(z) > t)
                {
                    const double r = std::fmod(std::abs(z) / t, 4.0);
                    double segment;
                    if (r <= 1.0) segment = r;
                    else if (r <= 3.0) segment = 2.0 - r;
                    else segment = r - 4.0;
                    // Multiplication preserves the negative segments of the periodic curve.
                    folded = (std::signbit(dry) ? -1.0 : 1.0) * t * segment;
                }
                const double wet = g * folded;
                sample = static_cast<float>(b == 0.0 ? wet : b * dry + (1.0 - b) * wet);
            }
        }
    });
}
}
