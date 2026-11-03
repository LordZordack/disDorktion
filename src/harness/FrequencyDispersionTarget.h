#pragma once
#include "AuditionTarget.h"
#include "dsp/FrequencyDispersion.h"
#include <array>

namespace disdorktion::harness
{
inline constexpr std::array frequencyDispersionControls {
    TargetControl{"frequencyHz","Dispersion corner (Hz)",20.f,20000.f,500.f},
    TargetControl{"mix","Mix",0.f,1.f,1.f},
    TargetControl{"bypass","Bypass",0.f,1.f,0.f}};
inline constexpr TargetDescriptor frequencyDispersionDescriptor {"frequency-dispersion","Frequency dispersion",frequencyDispersionControls};
inline bool setFrequencyDispersionControl(FrequencyDispersionSettings& settings,std::string_view id,float value) noexcept
{
    auto next=settings;
    if(id=="frequencyHz")next.frequencyHz=value;
    else if(id=="mix")next.mix=value;
    else if(id=="bypass" && (value==0.f || value==1.f))next.bypass=value!=0.f;
    else return false;
    if(!isValidFrequencyDispersionSettings(next))return false;
    settings=next;return true;
}
inline float getFrequencyDispersionControl(const FrequencyDispersionSettings& s,std::string_view id) noexcept
{
    if(id=="frequencyHz")return s.frequencyHz;
    if(id=="mix")return s.mix;
    return s.bypass?1.f:0.f;
}
class FrequencyDispersionTarget final : public AuditionTarget
{
public:
    const TargetDescriptor& descriptor() const noexcept override {return frequencyDispersionDescriptor;}
    bool prepare(const juce::dsp::ProcessSpec& spec) override {return dispersion.prepare(spec);}
    void reset() noexcept override {dispersion.reset();}
    bool setControl(std::string_view id,float value) noexcept override
    {auto next=settings;return setFrequencyDispersionControl(next,id,value) && setParameters(next);}
    void setBypass(bool value) noexcept override {auto next=settings;next.bypass=value;(void)setParameters(next);}
    bool setParameters(const FrequencyDispersionSettings& value) noexcept
    {if(!dispersion.setParameters(value))return false;settings=value;return true;}
    bool process(juce::dsp::AudioBlock<float> block) noexcept override {return dispersion.process(block);}
    unsigned getLatencySamples() const noexcept override {return dispersion.getLatencySamples();}
private:
    FrequencyDispersion dispersion;
    FrequencyDispersionSettings settings;
};
}
