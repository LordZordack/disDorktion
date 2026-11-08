#pragma once
#include "AuditionTarget.h"
#include "dsp/OutputSection.h"
#include "dsp/FullChainControls.h"

namespace disdorktion::harness
{
inline constexpr std::array outputSectionControls {
    TargetControl {"hpCutoffHz", "High-pass cutoff (Hz)", 20.f,20000.f,20.f},
    TargetControl {"hpResponse", "High-pass response", 0.f,2.f,0.f},
    TargetControl {"hpBypass", "Bypass high-pass", 0.f,1.f,0.f},
    TargetControl {"lpCutoffHz", "Low-pass cutoff (Hz)", 20.f,20000.f,20000.f},
    TargetControl {"lpResponse", "Low-pass response", 0.f,2.f,0.f},
    TargetControl {"lpBypass", "Bypass low-pass", 0.f,1.f,0.f},
    TargetControl {"combDelayMs", "Comb delay (ms)", .25f,40.f,5.f},
    TargetControl {"combFeedforward", "Comb feedforward", -1.f,1.f,0.f},
    TargetControl {"combFeedback", "Comb feedback", -.95f,.95f,.7f},
    TargetControl {"combDampingHz", "Comb damping (Hz)", 20.f,20000.f,6000.f},
    TargetControl {"combMix", "Comb mix", 0.f,1.f,.5f},
    TargetControl {"combDampingEnabled", "Comb damping enabled", 0.f,1.f,1.f},
    TargetControl {"combBypass", "Bypass comb", 0.f,1.f,1.f},
    TargetControl {"dispersionFrequencyHz", "Dispersion corner (Hz)", 20.f,20000.f,500.f},
    TargetControl {"dispersionMix", "Dispersion mix", 0.f,1.f,1.f},
    TargetControl {"dispersionBypass", "Bypass dispersion", 0.f,1.f,1.f},
    TargetControl {"toneDb", "Tone (dB)", -6.f,6.f,0.f},
    TargetControl {"toneBypass", "Bypass tone", 0.f,1.f,1.f}
};
inline constexpr TargetDescriptor outputSectionDescriptor {"output-section", "Output section", outputSectionControls};
inline bool isOutputControl(std::string_view id) noexcept
{
    for(const auto& control:outputSectionControls) if(control.id==id) return true;
    return false;
}
inline float getOutputControl(const OutputSettings& settings,std::string_view id) noexcept
{
    FullChainSettings chain;chain.output=settings;
    return getFullChainControl(chain,id);
}
inline bool setOutputControl(OutputSettings& settings,std::string_view id,float value) noexcept
{
    if(!isOutputControl(id)) return false;
    FullChainSettings chain;chain.output=settings;
    if(!setFullChainControl(chain,id,value)) return false;
    settings=chain.output;return true;
}
class OutputSectionTarget final : public AuditionTarget
{
public:
    const TargetDescriptor& descriptor() const noexcept override { return outputSectionDescriptor; }
    bool prepare(const juce::dsp::ProcessSpec& spec) override { return section.prepare(spec); }
    void reset() noexcept override { section.reset(); }
    bool setParameters(const OutputSettings& next) noexcept
    {
        if (!isValidOutputSettings(next)) return false;
        auto effective=next;
        effective.highPass.bypass |= commonBypass;
        effective.lowPass.bypass |= commonBypass;
        effective.comb.bypass |= commonBypass;
        effective.dispersion.bypass |= commonBypass;
        effective.tone.bypass |= commonBypass;
        if (!section.setParameters(effective)) return false;
        retained=next; return true;
    }
    bool setControl(std::string_view id, float value) noexcept override
    {
        auto next=retained;
        return setOutputControl(next,id,value) && setParameters(next);
    }
    void setBypass(bool value) noexcept override { commonBypass=value; (void) setParameters(retained); }
    bool process(juce::dsp::AudioBlock<float> block) noexcept override { return section.process(block); }
    unsigned getLatencySamples() const noexcept override { return section.getLatencySamples(); }
private:
    OutputSection section;
    OutputSettings retained;
    bool commonBypass=false;
};
}
