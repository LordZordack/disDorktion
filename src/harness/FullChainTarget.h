#pragma once
#include "AuditionTarget.h"
#include "dsp/FullChainControls.h"
namespace disdorktion::harness
{
inline constexpr auto fullChainTargetControls = []
{
    std::array<TargetControl, fullChainControls.size()> result {};
    for (std::size_t i=0;i<result.size();++i)
    {
        const auto& c=fullChainControls[i];
        result[i]={c.id,c.name,c.minimum,c.maximum,c.defaultValue};
    }
    return result;
}();
inline constexpr TargetDescriptor fullChainDescriptor {"full-chain","Full chain (candidate)",fullChainTargetControls};
class FullChainTarget final : public AuditionTarget
{
public:
    const TargetDescriptor& descriptor() const noexcept override { return fullChainDescriptor; }
    bool prepare(const juce::dsp::ProcessSpec& spec) override { return chain.prepare(spec); }
    void reset() noexcept override { chain.reset(); }
    bool setParameters(const FullChainSettings& settings) noexcept
    {
        if (!chain.setParameters(settings)) return false;
        retained=settings; return true;
    }
    bool setControl(std::string_view id,float value) noexcept override
    {
        auto next=retained;
        return setFullChainControl(next,id,value) && setParameters(next);
    }
    void setBypass(bool value) noexcept override { auto next=retained;next.bypass=value;(void)setParameters(next); }
    bool process(juce::dsp::AudioBlock<float> block) noexcept override { return chain.process(block); }
    unsigned getLatencySamples() const noexcept override { return chain.getLatencySamples(); }
    FullChainPeaks getPeaks() const noexcept { return chain.getPeaks(); }
private:
    FullChain chain;
    FullChainSettings retained;
};
}
