#pragma once
#include "AuditionTarget.h"
#include "dsp/OutputSection.h"
#include <array>
#include <cmath>
#include <limits>

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
    if(id=="hpCutoffHz") return settings.highPass.cutoffHz;
    if(id=="hpResponse") return static_cast<float>(settings.highPass.response);
    if(id=="hpBypass") return settings.highPass.bypass;
    if(id=="lpCutoffHz") return settings.lowPass.cutoffHz;
    if(id=="lpResponse") return static_cast<float>(settings.lowPass.response);
    if(id=="lpBypass") return settings.lowPass.bypass;
    if(id=="combDelayMs") return settings.comb.delayMs;
    if(id=="combFeedforward") return settings.comb.feedforward;
    if(id=="combFeedback") return settings.comb.feedback;
    if(id=="combDampingHz") return settings.comb.dampingHz;
    if(id=="combMix") return settings.comb.mix;
    if(id=="combDampingEnabled") return settings.comb.dampingEnabled;
    if(id=="combBypass") return settings.comb.bypass;
    if(id=="dispersionFrequencyHz") return settings.dispersion.frequencyHz;
    if(id=="dispersionMix") return settings.dispersion.mix;
    if(id=="dispersionBypass") return settings.dispersion.bypass;
    if(id=="toneDb") return settings.tone.toneDb;
    if(id=="toneBypass") return settings.tone.bypass;
    return std::numeric_limits<float>::quiet_NaN();
}
inline bool setOutputControl(OutputSettings& settings,std::string_view id,float value) noexcept
{
    for(const auto& control:outputSectionControls)
    {
        if(control.id!=id) continue;
        const bool discrete=id=="hpResponse" || id=="lpResponse" || id=="hpBypass" || id=="lpBypass"
            || id=="combDampingEnabled" || id=="combBypass" || id=="dispersionBypass" || id=="toneBypass";
        if(!std::isfinite(value) || value<control.minimum || value>control.maximum
            || (discrete && value!=std::floor(value))) return false;
        auto next=settings;
        if(id=="hpCutoffHz") next.highPass.cutoffHz=value;
        else if(id=="hpResponse") next.highPass.response=static_cast<FilterResponse>(static_cast<unsigned>(value));
        else if(id=="hpBypass") next.highPass.bypass=value!=0.f;
        else if(id=="lpCutoffHz") next.lowPass.cutoffHz=value;
        else if(id=="lpResponse") next.lowPass.response=static_cast<FilterResponse>(static_cast<unsigned>(value));
        else if(id=="lpBypass") next.lowPass.bypass=value!=0.f;
        else if(id=="combDelayMs") next.comb.delayMs=value;
        else if(id=="combFeedforward") next.comb.feedforward=value;
        else if(id=="combFeedback") next.comb.feedback=value;
        else if(id=="combDampingHz") next.comb.dampingHz=value;
        else if(id=="combMix") next.comb.mix=value;
        else if(id=="combDampingEnabled") next.comb.dampingEnabled=value!=0.f;
        else if(id=="combBypass") next.comb.bypass=value!=0.f;
        else if(id=="dispersionFrequencyHz") next.dispersion.frequencyHz=value;
        else if(id=="dispersionMix") next.dispersion.mix=value;
        else if(id=="dispersionBypass") next.dispersion.bypass=value!=0.f;
        else if(id=="toneDb") next.tone.toneDb=value;
        else if(id=="toneBypass") next.tone.bypass=value!=0.f;
        if(!isValidOutputSettings(next)) return false;
        settings=next;return true;
    }
    return false;
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
