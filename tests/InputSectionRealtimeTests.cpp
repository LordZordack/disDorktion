#include "AllocationTracking.h"
#include "dsp/InputSection.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Input section lifecycle control rejection and oversized processing allocate nothing", "[input-section][realtime]")
{
    using namespace disdorktion;
    InputSection section;
    juce::AudioBuffer<float> b(2,4099), wrong(1,19); b.clear();wrong.clear();
    bool ok=true;
    {
        test::ScopedAllocationTracking tracking;
        ok &= section.prepare({96000,127,2});
        for(int event=0;event<32;++event)
        {
            InputSettings s;
            s.gain={event%2?24.f:-60.f,event%3==0,event%4==0};
            s.crusher={bitCrusherDepths[event%bitCrusherDepths.size()],1777.25f,event%4==1,.63f,.71f,-.8f,.9f,static_cast<std::uint32_t>(event)};
            ok &= section.setParameters(s);
            s.crusher.mix=2; ok &= !section.setParameters(s);
            s.crusher.mix=.63f;s.gain.gainDb=25;ok &= !section.setParameters(s);
            ok &= !section.prepare({96000,0,2});
            ok &= !section.process(juce::dsp::AudioBlock<float>(wrong));
            ok &= section.process(juce::dsp::AudioBlock<float>(b).getSubBlock(0,0));
            for(int n=0;n<4099;++n){b.setSample(0,n,.5f);b.setSample(1,n,-.25f);}
            ok &= section.process(juce::dsp::AudioBlock<float>(b));
            ok &= std::isfinite(section.getPostGainPeak()) && section.getLatencySamples()==0;
            for(int c=0;c<2;++c)for(int n=0;n<4099;++n)ok &= std::isfinite(b.getSample(c,n));
            section.reset();
        }
    }
    const auto counts=test::getAllocationCounts();
    REQUIRE(ok);REQUIRE(counts.allocations==0);REQUIRE(counts.deallocations==0);
}
