#pragma once
#include "HighPass.h"
#include "LowPass.h"
#include "ResonantComb.h"
#include "FrequencyDispersion.h"
#include "Tone.h"

namespace disdorktion
{
struct OutputSettings
{
    CutoffSettings highPass {20.f, false, FilterResponse::butterworth};
    CutoffSettings lowPass {20000.f, false, FilterResponse::butterworth};
    // Append to retain the historical { highPass, lowPass } aggregate contract.
    ResonantCombSettings comb {.bypass=true};
    FrequencyDispersionSettings dispersion {.bypass=true};
    ToneSettings tone {.bypass=true};
};
inline bool isValidOutputSettings(const OutputSettings& settings) noexcept
{
    return isValidCutoffSettings(settings.highPass) && isValidCutoffSettings(settings.lowPass)
        && isValidResonantCombSettings(settings.comb)
        && isValidFrequencyDispersionSettings(settings.dispersion) && isValidToneSettings(settings.tone);
}
// Calls, including diagnostic getters, require exclusive processing ownership.
// Native-rate comb -> dispersion -> tone -> HP -> LP; no added bulk latency.
// New modules default bypassed to preserve the historical HP/LP response.
// Input is nominally [-1,1]. Legacy HP/LP qualification checks [-4,4];
// active cascades are separately stress-tested at extreme resonant settings.
class OutputSection
{
public:
    OutputSection() noexcept { (void) setParameters(OutputSettings{}); }
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
    ResonantComb comb;
    FrequencyDispersion dispersion;
    Tone tone;
    HighPass highPass;
    LowPass lowPass;
    float postHighPassPeak = 0.f;
};
}
