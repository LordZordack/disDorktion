#pragma once
#include "HighPass.h"
#include "LowPass.h"

namespace disdorktion
{
struct OutputSettings
{
    CutoffSettings highPass {20.f, false, FilterResponse::butterworth};
    CutoffSettings lowPass {20000.f, false, FilterResponse::butterworth};
};
inline bool isValidOutputSettings(const OutputSettings& settings) noexcept
{
    return isValidCutoffSettings(settings.highPass) && isValidCutoffSettings(settings.lowPass);
}
// Calls, including diagnostic getters, require exclusive processing ownership.
// Input is nominally [-1,1]; qualification checks the LP input envelope [-4,4].
class OutputSection
{
public:
    bool prepare(const juce::dsp::ProcessSpec&) noexcept;
    void reset() noexcept;
    bool setParameters(const OutputSettings&) noexcept;
    bool process(juce::dsp::AudioBlock<float>) noexcept;
    unsigned getLatencySamples() const noexcept { return 0; }
    float getPostHighPassPeak() const noexcept { return postHighPassPeak; }
    double getEffectiveHighPassCutoffHz() const noexcept { return highPass.getEffectiveCutoffHz(); }
    double getEffectiveLowPassCutoffHz() const noexcept { return lowPass.getEffectiveCutoffHz(); }
private:
    BlockDispatcher dispatcher;
    HighPass highPass;
    LowPass lowPass;
    float postHighPassPeak = 0.f;
};
}
