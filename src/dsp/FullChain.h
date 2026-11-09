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
    MiddleSettings middle {{-24.f,0.f,true},{-24.f,1.f,0.f,true}};
    OutputSettings output = []
    {
        OutputSettings s;
        s.comb.mix=0.f; s.comb.bypass=false; s.comb.dampingEnabled=true;
        s.dispersion.mix=0.f; s.dispersion.bypass=false;
        s.tone.bypass=false; s.highPass.bypass=true; s.lowPass.bypass=true;
        return s;
    }();
    GainSettings finalGain;
    float mix = 1.f;
    float driver = 1.f;
    // Development harness bypass; never exposed as a plugin parameter.
    bool bypass = false;
};
inline bool isValidFullChainSettings(const FullChainSettings& s) noexcept
{
    return isValidInputSettings(s.input) && isValidTanhSaturationSettings(s.middle.tanh)
        && isValidWaveFolderSettings(s.middle.folder) && isValidOutputSettings(s.output)
        && std::isfinite(s.finalGain.gainDb) && s.finalGain.gainDb >= -60.f && s.finalGain.gainDb <= 24.f
        && std::isfinite(s.middle.mix) && s.middle.mix >= 0.f && s.middle.mix <= 1.f
        && std::isfinite(s.middle.trimDb) && s.middle.trimDb >= -60.f && s.middle.trimDb <= 12.f
        && std::isfinite(s.driver) && s.driver >= 1.f && s.driver <= 4.f
        && std::isfinite(s.mix) && s.mix >= 0.f && s.mix <= 1.f;
}
struct FullChainPeaks
{
    float postInputGain = 0.f, postHighPass = 0.f, wet = 0.f, output = 0.f;
};
// All calls require exclusive processing ownership. Prepare allocates; controls,
// reset and processing do not. Dry captures input after Input Gain. Driver is
// confined to the Waveshaper nonlinear branch and leaves this dry path unchanged.
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
