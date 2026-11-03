#pragma once
#include "AuditionTarget.h"
#include "dsp/Tone.h"
#include <array>
namespace disdorktion::harness
{
inline constexpr std::array toneControls {
    TargetControl{"toneDb","Tone (dark / bright dB)",-6.f,6.f,0.f},
    TargetControl{"bypass","Bypass",0.f,1.f,0.f}};
inline constexpr TargetDescriptor toneDescriptor{"tone","Tone",toneControls};
inline bool setToneControl(ToneSettings& s,std::string_view id,float value) noexcept
{
    auto next=s;
    if(id=="toneDb")next.toneDb=value;
    else if(id=="bypass" && (value==0.f || value==1.f))next.bypass=value!=0.f;
    else return false;
    if(!isValidToneSettings(next))return false;s=next;return true;
}
class ToneTarget final : public AuditionTarget
{
public:
    const TargetDescriptor& descriptor() const noexcept override {return toneDescriptor;}
    bool prepare(const juce::dsp::ProcessSpec& s) override {return tone.prepare(s);}
    void reset() noexcept override {tone.reset();}
    bool setParameters(const ToneSettings& s) noexcept {if(!tone.setParameters(s))return false;settings=s;return true;}
    bool setControl(std::string_view id,float value) noexcept override
    {auto next=settings;return setToneControl(next,id,value) && setParameters(next);}
    void setBypass(bool value) noexcept override {auto next=settings;next.bypass=value;(void)setParameters(next);}
    bool process(juce::dsp::AudioBlock<float> b) noexcept override {return tone.process(b);}
    unsigned getLatencySamples() const noexcept override {return 0;}
private:
    Tone tone;ToneSettings settings;
};
}
