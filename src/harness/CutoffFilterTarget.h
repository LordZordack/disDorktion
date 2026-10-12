#pragma once
#include "AuditionTarget.h"
#include "dsp/CutoffFilter.h"
#include <array>

namespace disdorktion::harness
{
inline constexpr std::array highPassControls {TargetControl {"cutoffHz","Cutoff (Hz)",20.f,20000.f,20.f},
    TargetControl {"response","Response",0.f,2.f,0.f}};
inline constexpr std::array lowPassControls {TargetControl {"cutoffHz","Cutoff (Hz)",20.f,20000.f,20000.f},
    TargetControl {"response","Response",0.f,2.f,0.f}};
inline constexpr TargetDescriptor highPassDescriptor {"high-pass","High-pass",highPassControls};
inline constexpr TargetDescriptor lowPassDescriptor {"low-pass","Low-pass",lowPassControls};
template<bool High> class CutoffFilterTarget final : public AuditionTarget
{
public:
    const TargetDescriptor& descriptor() const noexcept override { if constexpr(High) return highPassDescriptor; else return lowPassDescriptor; }
    bool prepare(const juce::dsp::ProcessSpec& spec) override { return filter.prepare(spec); }
    void reset() noexcept override { filter.reset(); }
    bool setControl(std::string_view id,float value) noexcept override
    {
        auto next=settings;
        if(id=="cutoffHz") next.cutoffHz=value;
        else if(id=="response" && std::isfinite(value) && value>=0 && value<=2 && std::floor(value)==value)
            next.response=static_cast<FilterResponse>(static_cast<unsigned>(value));
        else return false;
        return setParameters(next);
    }
    void setBypass(bool value) noexcept override {auto next=settings;next.bypass=value;(void)setParameters(next);}
    bool setParameters(const CutoffSettings& value) noexcept
    {if(!filter.setParameters(value))return false;settings=value;return true;}
    bool process(juce::dsp::AudioBlock<float> block) noexcept override {return filter.process(block);}
    unsigned getLatencySamples() const noexcept override {return filter.getLatencySamples();}
private:
    CutoffFilter<High> filter;
    CutoffSettings settings {High?20.f:20000.f,false,FilterResponse::butterworth};
};
}
