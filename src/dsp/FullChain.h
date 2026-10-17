#pragma once
#include "InputSection.h"
#include "MiddleSection.h"
#include "OutputSection.h"
#include <memory>

namespace disdorktion
{
struct FullChainSettings
{
    InputSettings input;
    MiddleSettings middle;
    OutputSettings output;
    GainSettings finalGain;
    float mix = 1.f;
    bool bypass = false;
};
inline bool isValidFullChainSettings(const FullChainSettings& s) noexcept
{
    return isValidInputSettings(s.input) && isValidTanhSaturationSettings(s.middle.tanh)
        && isValidWaveFolderSettings(s.middle.folder) && isValidOutputSettings(s.output)
        && std::isfinite(s.finalGain.gainDb) && s.finalGain.gainDb >= -60.f && s.finalGain.gainDb <= 24.f
        && std::isfinite(s.mix) && s.mix >= 0.f && s.mix <= 1.f;
}
struct FullChainPeaks
{
    float postInputGain = 0.f, postHighPass = 0.f, wet = 0.f, output = 0.f;
};
// All calls require exclusive processing ownership. Prepare allocates; controls,
// reset and processing do not. Dry captures input before any production drive.
class FullChain
{
public:
    FullChain();
    ~FullChain();
    bool prepare(const juce::dsp::ProcessSpec&);
    void reset() noexcept;
    bool setParameters(const FullChainSettings&) noexcept;
    bool process(juce::dsp::AudioBlock<float>) noexcept;
    unsigned int getLatencySamples() const noexcept;
    FullChainPeaks getPeaks() const noexcept { return peaks; }
private:
    struct State;
    std::unique_ptr<State> state;
    FullChainSettings retained;
    FullChainPeaks peaks;
};
}
