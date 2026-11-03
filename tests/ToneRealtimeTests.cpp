#include "AllocationTracking.h"
#include "dsp/Tone.h"
#include <catch2/catch_test_macros.hpp>
TEST_CASE("Tone processing controls and reset allocate nothing", "[tone][realtime]")
{
    disdorktion::Tone tone;REQUIRE(tone.prepare({384000,32,2}));
    juce::AudioBuffer<float> buffer(2,4097);buffer.clear();
    bool ok=true;
    {
        disdorktion::test::ScopedAllocationTracking tracking;
        for(int event=0;event<128;++event)
        {
            buffer.clear();buffer.setSample(0,0,16);buffer.setSample(1,0,-16);
            ok &= tone.setParameters({event%2?6.f:-6.f,event%3==0});
            ok &= tone.process(juce::dsp::AudioBlock<float>(buffer));
            for(int c=0;c<2;++c)for(int n=0;n<4097;++n)ok &= std::isfinite(buffer.getSample(c,n));
            tone.reset();
        }
    }
    const auto counts=disdorktion::test::getAllocationCounts();
    REQUIRE(ok);REQUIRE(counts.allocations==0);REQUIRE(counts.deallocations==0);
}
