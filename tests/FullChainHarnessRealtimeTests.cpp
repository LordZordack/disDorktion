#include "AllocationTracking.h"
#include "harness/AuditionEngine.h"
#include <catch2/catch_test_macros.hpp>
using namespace disdorktion;
using namespace disdorktion::harness;
TEST_CASE("Full chain harness scalar automation reset and oversized callbacks allocate nothing", "[full-chain][harness][realtime]")
{
    AuditionEngine engine;REQUIRE(engine.selectTarget("full-chain"));REQUIRE(engine.prepare({96000,7,2}));
    juce::AudioBuffer<float> audio(2,1031);audio.clear();
    bool ok=true;
    {
        test::ScopedAllocationTracking tracking;
        for(unsigned event=0;event<16;++event)
        {
            ok &= engine.setFullChainControl("globalMix",event%2?.5f:1.f);
            ok &= engine.setFullChainControl("chainBypass",static_cast<float>(event%2));
            ok &= engine.setFullChainControl("inputGainDb",event%2?24.f:0.f);
            ok &= engine.setFullChainControl("folderThreshold",event%2?.05f:1.f);
            ok &= !engine.setFullChainControl("hpResponse",.5f);
            engine.setFullChainJitterSeed(event);
            ok &= engine.process(juce::dsp::AudioBlock<float>(audio),false);
            engine.reset();
        }
    }
    const auto counts=test::getAllocationCounts();REQUIRE(ok);REQUIRE(counts.allocations==0);REQUIRE(counts.deallocations==0);
}
