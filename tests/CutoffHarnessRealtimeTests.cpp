#include "AllocationTracking.h"
#include "harness/AuditionEngine.h"
#include <catch2/catch_test_macros.hpp>
TEST_CASE("Filter harness atomic controls and callback allocate nothing", "[cutoff-filter][harness-realtime]")
{
    using namespace disdorktion;
    for(const char* id:{"high-pass","low-pass"})
    {
        harness::AuditionEngine engine;REQUIRE(engine.selectTarget(id));REQUIRE(engine.prepare({48000,32,2}));
        juce::AudioBuffer<float> audio(2,1027);audio.clear();bool ok=true;
        {
            test::ScopedAllocationTracking tracking;
            for(unsigned n=0;n<100;++n)
            {
                ok &= engine.setFilterCutoffHz(n%2?20.f:20000.f);ok &= engine.setFilterResponse(static_cast<FilterResponse>(n%3));
                engine.setBypass(n%5==0);ok &= engine.process(juce::dsp::AudioBlock<float>(audio),false);
                ok &= !engine.setFilterCutoffHz(0);ok &= !engine.setFilterResponse(static_cast<FilterResponse>(99));engine.reset();
            }
        }
        const auto counts=test::getAllocationCounts();REQUIRE(ok);REQUIRE(counts.allocations==0);REQUIRE(counts.deallocations==0);
    }
}
