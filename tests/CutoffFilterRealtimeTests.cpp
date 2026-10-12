#include "AllocationTracking.h"
#include "dsp/HighPass.h"
#include "dsp/LowPass.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Cutoff filters allocate nothing during modulation reset or rejected calls", "[cutoff-filter][realtime]")
{
    using namespace disdorktion;
    HighPass high; LowPass low,unprepared;
    REQUIRE(high.prepare({48000,32,2})); REQUIRE(low.prepare({48000,32,1}));
    juce::AudioBuffer<float> stereo(2,1027),mono(1,1027); stereo.clear();mono.clear();
    bool ok=true;
    {
        test::ScopedAllocationTracking tracking;
        for(int n=0;n<100;++n)
        {
            auto mode=static_cast<FilterResponse>(n%3);
            ok &= high.setParameters({n%2?20.f:20000.f,n%5==0,mode});
            ok &= low.setParameters({n%2?20000.f:20.f,n%3==0,mode});
            ok &= !high.setParameters({0,true,mode});
            ok &= high.process(juce::dsp::AudioBlock<float>(stereo));
            ok &= low.process(juce::dsp::AudioBlock<float>(mono));
            ok &= !high.process(juce::dsp::AudioBlock<float>(mono));
            ok &= !unprepared.process(juce::dsp::AudioBlock<float>(mono));
            high.reset();low.reset();unprepared.reset();
        }
    }
    auto count=test::getAllocationCounts(); REQUIRE(ok); REQUIRE(count.allocations==0); REQUIRE(count.deallocations==0);
}
