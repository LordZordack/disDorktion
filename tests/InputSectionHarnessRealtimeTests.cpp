#include "AllocationTracking.h"
#include "harness/AuditionEngine.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Input section harness controls and overload publication allocate nothing", "[input-section][harness-realtime]")
{
    using namespace disdorktion;
    harness::AuditionEngine engine;
    REQUIRE(engine.selectTarget("input-section"));
    REQUIRE(engine.prepare({48000, 7, 2}));
    juce::AudioBuffer<float> audio(2, 1027); audio.clear();
    bool ok=true;
    {
        test::ScopedAllocationTracking tracking;
        for (unsigned n=0;n<100;++n)
        {
            ok &= engine.setGainDb(n%2 ? -60.f : 24.f);
            engine.setModuleMuted(n%3==0);
            engine.setInputGainBypass(n%4==0); engine.setInputCrusherBypass(n%5==0);
            ok &= engine.setBitDepth(bitCrusherDepths[n%11]);
            ok &= engine.setCaptureRateHz(n%2 ? 100.f : 384000.f);
            ok &= engine.setCrush(n%2 ? .8f : .2f); ok &= engine.setQuantizerShape(n%2 ? -.8f : .8f);
            ok &= engine.setCrusherMix(n%2 ? .2f : 1.f); ok &= engine.setJitter(n%2 ? 0.f : 1.f);
            engine.setJitterSeed(n); engine.setBypass(n%7==0);
            ok &= engine.process(juce::dsp::AudioBlock<float>(audio),false);
            (void) engine.meters(); (void) engine.consumeOverload();
            ok &= !engine.setGainDb(25.f); ok &= !engine.setBitDepth(15);
            engine.reset();
        }
    }
    const auto count=test::getAllocationCounts();
    REQUIRE(ok); REQUIRE(count.allocations==0); REQUIRE(count.deallocations==0);
}
