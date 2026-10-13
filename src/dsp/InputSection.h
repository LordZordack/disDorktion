#pragma once
#include "Gain.h"
#include "BitCrusher.h"

namespace disdorktion
{
struct InputSettings
{
    GainSettings gain;
    BitCrusherSettings crusher;
};
inline bool isValidInputSettings(const InputSettings& settings) noexcept
{
    return std::isfinite(settings.gain.gainDb) && settings.gain.gainDb >= -60.0f
        && settings.gain.gainDb <= 24.0f && isValidBitCrusherSettings(settings.crusher);
}
// All calls, including the peak getter, require exclusive processing ownership.
// The harness publishes the peak separately for readers on other threads.
class InputSection
{
public:
    bool prepare(const juce::dsp::ProcessSpec&) noexcept;
    void reset() noexcept;
    bool setParameters(const InputSettings&) noexcept;
    bool process(juce::dsp::AudioBlock<float>) noexcept;
    unsigned int getLatencySamples() const noexcept { return 0; }
    float getPostGainPeak() const noexcept { return postGainPeak; }
private:
    BlockDispatcher dispatcher;
    Gain gain;
    BitCrusher crusher;
    float postGainPeak = 0.0f;
};
}
