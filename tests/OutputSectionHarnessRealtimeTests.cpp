#include "AllocationTracking.h"
#include "harness/AuditionEngine.h"
#include <catch2/catch_test_macros.hpp>
#include <thread>
using namespace disdorktion;
TEST_CASE("Output harness scalar changes and reset allocate nothing", "[output-section][harness-realtime]")
{
    harness::AuditionEngine engine; REQUIRE(engine.selectTarget("output-section"));REQUIRE(engine.prepare({48000,7,2}));
    juce::AudioBuffer<float> audio(2,1031);audio.clear();bool ok=true;
    {
        test::ScopedAllocationTracking tracking;
        for(unsigned n=0;n<100;++n)
        {
            ok &= engine.setOutputCutoffHz(true,n%2?20.f:20000.f);ok &= engine.setOutputCutoffHz(false,n%2?20000.f:20.f);
            ok &= engine.setOutputResponse(true,static_cast<FilterResponse>(n%3));ok &= engine.setOutputResponse(false,static_cast<FilterResponse>((n+1)%3));
            engine.setOutputStageBypass(true,n%3==0);engine.setOutputStageBypass(false,n%5==0);engine.setBypass(n%7==0);
            ok &= !engine.setOutputCutoffHz(true,0.f);
            ok &= engine.process(juce::dsp::AudioBlock<float>(audio),false);engine.reset();
        }
    }
    const auto counts=test::getAllocationCounts();REQUIRE(ok);REQUIRE(counts.allocations==0);REQUIRE(counts.deallocations==0);
}
TEST_CASE("Output live scalar controls publish concurrently with callbacks", "[output-section][harness-realtime]")
{
    harness::AuditionEngine engine;REQUIRE(engine.selectTarget("output-section"));REQUIRE(engine.prepare({48000,64,2}));
    std::atomic<bool> start{false},done{false},valid{true};juce::AudioBuffer<float> audio(2,64);
    std::thread producer([&] {
        while(!start.load())std::this_thread::yield();
        for(unsigned n=0;n<2000;++n)
        {
            const bool ok=engine.setOutputCutoffHz(true,n%2?20.f:20000.f)&&engine.setOutputCutoffHz(false,n%2?20000.f:20.f)
                &&engine.setOutputResponse(true,static_cast<FilterResponse>(n%3))&&engine.setOutputResponse(false,static_cast<FilterResponse>((n+1)%3));
            if(!ok)valid.store(false);engine.setOutputStageBypass(true,n%3==0);engine.setOutputStageBypass(false,n%5==0);engine.setBypass(n%7==0);
        }
        done.store(true);
    });
    bool ok=true;start.store(true);
    do {for(int c=0;c<2;++c)for(int n=0;n<64;++n)audio.setSample(c,n,.5f);ok &= engine.process(juce::dsp::AudioBlock<float>(audio),false);}while(!done.load());
    producer.join();REQUIRE(ok);REQUIRE(valid.load());
}
