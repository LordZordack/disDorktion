#pragma once
#include "AuditionTarget.h"
#include "dsp/ResonantComb.h"
#include <array>

namespace disdorktion::harness
{
inline constexpr std::array resonantCombControls {
    TargetControl{"delayMs","Delay (ms)",.25f,40.f,5.f},
    TargetControl{"feedforward","Feedforward",-1.f,1.f,0.f},
    TargetControl{"feedback","Feedback",-.95f,.95f,.7f},
    TargetControl{"dampingHz","Damping (Hz)",20.f,20000.f,6000.f},
    TargetControl{"mix","Mix",0.f,1.f,.5f},
    TargetControl{"dampingEnabled","Damping enabled",0.f,1.f,1.f},
    TargetControl{"bypass","Bypass",0.f,1.f,0.f}};
inline constexpr TargetDescriptor resonantCombDescriptor {"resonant-comb","Resonant comb",resonantCombControls};
inline bool setResonantCombControl(ResonantCombSettings& settings,std::string_view id,float value) noexcept
{
    auto next=settings;
    if(id=="delayMs") next.delayMs=value;
    else if(id=="feedforward") next.feedforward=value;
    else if(id=="feedback") next.feedback=value;
    else if(id=="dampingHz") next.dampingHz=value;
    else if(id=="mix") next.mix=value;
    else if((id=="dampingEnabled" || id=="bypass") && (value==0.f || value==1.f))
    { if(id=="bypass") next.bypass=value!=0.f; else next.dampingEnabled=value!=0.f; }
    else return false;
    if(!isValidResonantCombSettings(next)) return false;
    settings=next;return true;
}
inline float getResonantCombControl(const ResonantCombSettings& s,std::string_view id) noexcept
{
    if(id=="delayMs")return s.delayMs;if(id=="feedforward")return s.feedforward;
    if(id=="feedback")return s.feedback;if(id=="dampingHz")return s.dampingHz;
    if(id=="mix")return s.mix;if(id=="dampingEnabled")return s.dampingEnabled?1.f:0.f;
    return s.bypass?1.f:0.f;
}
class ResonantCombTarget final : public AuditionTarget
{
public:
    const TargetDescriptor& descriptor() const noexcept override {return resonantCombDescriptor;}
    bool prepare(const juce::dsp::ProcessSpec& spec) override {return comb.prepare(spec);}
    void reset() noexcept override {comb.reset();}
    bool setControl(std::string_view id,float value) noexcept override
    {auto next=settings;return setResonantCombControl(next,id,value) && setParameters(next);}
    void setBypass(bool value) noexcept override {auto next=settings;next.bypass=value;(void)setParameters(next);}
    bool setParameters(const ResonantCombSettings& value) noexcept
    {if(!comb.setParameters(value))return false;settings=value;return true;}
    bool process(juce::dsp::AudioBlock<float> block) noexcept override {return comb.process(block);}
    unsigned getLatencySamples() const noexcept override {return comb.getLatencySamples();}
private:
    ResonantComb comb;
    ResonantCombSettings settings;
};
}
