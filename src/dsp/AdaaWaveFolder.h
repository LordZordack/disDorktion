#pragma once
#include "WaveFolder.h"
#include <array>

namespace disdorktion
{
// Evaluation-only first-order ADAA. The public boundary supplies integer latency.
class AdaaWaveFolder
{
public:
    bool prepareOversampled(const juce::dsp::ProcessSpec& hostSpec, unsigned factor) noexcept;
    void reset() noexcept;
    bool setParameters(const WaveFolderSettings&) noexcept;
    bool process(juce::dsp::AudioBlock<float>) noexcept;
    double getLatencyInInternalSamples() const noexcept { return 1.5; }
private:
    BlockDispatcher dispatcher;
    WaveFolderSettings retained;
    double driveTarget = 1.0, outputTarget = 1.0;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> drive {1.0}, threshold {1.0},
        output {1.0}, bypass {0.0};
    std::array<double, 2> previousInput {}, correctionOne {}, correctionTwo {};
};
}
