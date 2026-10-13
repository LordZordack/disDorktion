#pragma once
#include "BlockDispatcher.h"

namespace disdorktion
{
struct TanhSaturationSettings
{
    float driveDb = 0.0f;
    float outputDb = 0.0f;
    bool bypass = false;
};
inline bool isValidTanhSaturationSettings(const TanhSaturationSettings& p) noexcept
{
    return std::isfinite(p.driveDb) && p.driveDb >= -24.0f && p.driveDb <= 36.0f
        && std::isfinite(p.outputDb) && p.outputDb >= -60.0f && p.outputDb <= 12.0f;
}
class TanhSaturation
{
public:
    bool prepare(const juce::dsp::ProcessSpec&) noexcept;
    bool prepareOversampled(const juce::dsp::ProcessSpec& hostSpec, unsigned factor) noexcept;
    void reset() noexcept;
    bool setParameters(const TanhSaturationSettings&) noexcept;
    bool process(juce::dsp::AudioBlock<float>) noexcept;
    unsigned int getLatencySamples() const noexcept { return 0; }
private:
    BlockDispatcher dispatcher;
    TanhSaturationSettings retained;
    double driveTarget = 1.0, outputTarget = 1.0;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> drive {1.0}, output {1.0}, bypass {0.0};
};
}
