#include "AllocationTracking.h"
#include "dsp/ResonantComb.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

TEST_CASE("Resonant comb callback controls reset and rejection allocate nothing", "[resonant-comb][realtime]")
{
    using namespace disdorktion;
    ResonantComb mono,stereo,unprepared;
    REQUIRE(mono.prepare({48000,8,1})); REQUIRE(stereo.prepare({384000,32,2}));
    juce::AudioBuffer<float> m(1,1027),s(2,1027); m.clear(); s.clear(); m.setSample(0,0,1); s.setSample(1,0,1);
    auto mb=juce::dsp::AudioBlock<float>(m),sb=juce::dsp::AudioBlock<float>(s);
    bool success=true;
    {
        test::ScopedAllocationTracking tracking;
        for(int i=0;i<128;++i)
        {
            ResonantCombSettings p; p.delayMs=i%2?40.f:.25f; p.feedback=i%2?-.95f:.95f; p.feedforward=i%2?-1.f:1.f;
            p.dampingHz=i%2?20.f:20000.f; p.dampingEnabled=i%3==0; p.mix=i%2?1.f:0.f; p.bypass=i%3==0;
            success &= mono.setParameters(p); success &= stereo.setParameters(p);
            p.dampingHz=std::numeric_limits<float>::quiet_NaN(); success &= !mono.setParameters(p);
            success &= mono.process(mb.getSubBlock(0,0)); success &= stereo.process(sb.getSubBlock(0,0));
            success &= mono.process(mb); success &= stereo.process(sb);
            success &= !mono.process(sb); success &= !stereo.process(mb); success &= !unprepared.process(mb);
            mono.reset(); stereo.reset(); unprepared.reset();
            success &= mono.process(mb); success &= stereo.process(sb);
        }
    }
    const auto counts=test::getAllocationCounts(); REQUIRE(success); REQUIRE(counts.allocations==0); REQUIRE(counts.deallocations==0);
}
