#include "AllocationTracking.h"
#include "dsp/FrequencyDispersion.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

TEST_CASE("Frequency dispersion audio controls reset and rejection allocate nothing", "[frequency-dispersion][realtime]")
{
    using namespace disdorktion;
    FrequencyDispersion mono,stereo,unprepared;
    REQUIRE(mono.prepare({8000,1,1})); REQUIRE(stereo.prepare({384000,32,2}));
    juce::AudioBuffer<float> m(1,1027),s(2,1027);m.clear();s.clear();m.setSample(0,0,16);s.setSample(1,0,-16);
    auto mb=juce::dsp::AudioBlock<float>(m),sb=juce::dsp::AudioBlock<float>(s);
    bool success=true,finite=true;
    {
        test::ScopedAllocationTracking tracking;
        for(int i=0;i<128;++i)
        {
            FrequencyDispersionSettings p;p.frequencyHz=i%2?20.f:20000.f;p.mix=i%2?0.f:1.f;p.bypass=i%3==0;
            success&=mono.setParameters(p);success&=stereo.setParameters(p);
            p.mix=std::numeric_limits<float>::quiet_NaN();success&=!mono.setParameters(p);
            success&=mono.process(mb.getSubBlock(0,0));success&=stereo.process(sb.getSubBlock(0,0));
            success&=mono.process(mb);success&=stereo.process(sb);
            success&=!mono.process(sb);success&=!stereo.process(mb);success&=!unprepared.process(mb);
            for(int n=0;n<1027;++n){finite&=std::isfinite(m.getSample(0,n));finite&=std::isfinite(s.getSample(1,n));}
            mono.reset();stereo.reset();unprepared.reset();
            m.clear();s.clear();m.setSample(0,0,16);s.setSample(1,0,-16);
        }
    }
    const auto counts=test::getAllocationCounts();REQUIRE(success);REQUIRE(finite);REQUIRE(counts.allocations==0);REQUIRE(counts.deallocations==0);
}
