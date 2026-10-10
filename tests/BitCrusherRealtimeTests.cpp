#include "AllocationTracking.h"
#include "dsp/BitCrusher.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

TEST_CASE("Bit crusher processing controls and reset allocate nothing", "[crusher][realtime]")
{
    using namespace disdorktion;
    BitCrusher mono,stereo,unprepared;
    REQUIRE(mono.prepare({48000,8,1})); REQUIRE(stereo.prepare({48000,8,2}));
    juce::AudioBuffer<float> m(1,1027),s(2,1027);m.clear();s.clear();
    auto mb=juce::dsp::AudioBlock<float>(m),sb=juce::dsp::AudioBlock<float>(s);
    bool success=true;
    {
        test::ScopedAllocationTracking tracking;
        for(int i=0;i<64;++i)
        {
            success &= mono.setParameters({bitCrusherDepths[i%11],i%2?100.0f:384000.0f,i%3==0});
            success &= stereo.setParameters({bitCrusherDepths[(i+3)%11],i%2?8000.0f:96000.0f,i%5==0});
            success &= !mono.setParameters({9,100,true});
            success &= !stereo.setParameters({64,std::numeric_limits<float>::quiet_NaN(),true});
            success &= mono.process(mb.getSubBlock(0,0));success &= stereo.process(sb.getSubBlock(0,0));
            success &= mono.process(mb);success &= stereo.process(sb);
            success &= !mono.process(sb);success &= !stereo.process(mb);success &= !unprepared.process(mb);
            mono.reset();stereo.reset();
        }
    }
    const auto counts=test::getAllocationCounts();
    REQUIRE(success);REQUIRE(counts.allocations==0);REQUIRE(counts.deallocations==0);
}

TEST_CASE("Expressive crusher automation and seeded jitter allocate nothing", "[crusher][expressive][realtime]")
{
    using namespace disdorktion;
    BitCrusher crusher; REQUIRE(crusher.prepare({96000,32,2}));
    juce::AudioBuffer<float> audio(2,1027); audio.clear();
    auto view=juce::dsp::AudioBlock<float>(audio);bool success=true;
    {
        test::ScopedAllocationTracking tracking;
        for(unsigned i=0;i<128;++i)
        {
            BitCrusherSettings p {bitCrusherDepths[i%11],i%2?12345.5f:384000.0f,i%3==0};
            p.mix=i%2?.2f:1;p.crush=i%2?.8f:.2f;p.shape=i%2?-.9f:.9f;p.jitter=1;p.jitterSeed=i;
            success &= crusher.setParameters(p); success &= crusher.process(view);
            if(i%8==0) crusher.reset();
        }
    }
    const auto counts=test::getAllocationCounts();
    REQUIRE(success);REQUIRE(counts.allocations==0);REQUIRE(counts.deallocations==0);
}
