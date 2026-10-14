#pragma once
#include "AuditionTarget.h"
#include "dsp/OutputSection.h"

namespace disdorktion::harness
{
inline constexpr std::array outputSectionControls {
    TargetControl {"hpCutoffHz", "High-pass cutoff (Hz)", 20.f,20000.f,20.f},
    TargetControl {"hpResponse", "High-pass response", 0.f,2.f,0.f},
    TargetControl {"hpBypass", "Bypass high-pass", 0.f,1.f,0.f},
    TargetControl {"lpCutoffHz", "Low-pass cutoff (Hz)", 20.f,20000.f,20000.f},
    TargetControl {"lpResponse", "Low-pass response", 0.f,2.f,0.f},
    TargetControl {"lpBypass", "Bypass low-pass", 0.f,1.f,0.f}
};
inline constexpr TargetDescriptor outputSectionDescriptor {"output-section", "Output section", outputSectionControls};
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
        if (!section.setParameters(effective)) return false;
        retained=next; return true;
    }
    bool setControl(std::string_view id, float value) noexcept override
    {
        auto next=retained;
        if (id=="hpCutoffHz") next.highPass.cutoffHz=value;
        else if (id=="lpCutoffHz") next.lowPass.cutoffHz=value;
        else if ((id=="hpResponse" || id=="lpResponse") && std::isfinite(value)
                 && value>=0.f && value<=2.f && value==std::floor(value))
            (id=="hpResponse" ? next.highPass : next.lowPass).response=static_cast<FilterResponse>(static_cast<unsigned>(value));
        else if ((id=="hpBypass" || id=="lpBypass") && (value==0.f || value==1.f))
            (id=="hpBypass" ? next.highPass : next.lowPass).bypass=value==1.f;
        else return false;
        return setParameters(next);
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
