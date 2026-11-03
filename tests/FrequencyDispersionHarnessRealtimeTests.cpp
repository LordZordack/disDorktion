#include "AllocationTracking.h"
#include "harness/AuditionEngine.h"
#include <catch2/catch_test_macros.hpp>
#include <thread>
using namespace disdorktion;
TEST_CASE("Dispersion harness callbacks and live controls allocate nothing", "[frequency-dispersion][harness-realtime]")
{
    harness::AuditionEngine engine;REQUIRE(engine.selectTarget("frequency-dispersion"));REQUIRE(engine.prepare({48000,7,2}));
    juce::AudioBuffer<float> audio(2,1031);audio.clear();bool ok=true;
    {
        test::ScopedAllocationTracking tracking;
        for(unsigned n=0;n<100;++n)
        {
            ok &= engine.setFrequencyDispersionControl("frequencyHz",n%2?20.f:20000.f);
            ok &= engine.setFrequencyDispersionControl("mix",n%2?0.f:1.f);
            engine.setBypass(n%7==0);ok &= engine.process(juce::dsp::AudioBlock<float>(audio),false);engine.reset();
        }
    }
    const auto counts=test::getAllocationCounts();REQUIRE(ok);REQUIRE(counts.allocations==0);REQUIRE(counts.deallocations==0);
}

TEST_CASE("Dispersion scalar controls publish concurrently with callbacks", "[frequency-dispersion][harness-realtime]")
{
    harness::AuditionEngine engine;REQUIRE(engine.selectTarget("frequency-dispersion"));REQUIRE(engine.prepare({48000,64,2}));
    std::atomic<bool> start{false},done{false},valid{true};juce::AudioBuffer<float> audio(2,64);
    std::thread producer([&] {
        while(!start.load())std::this_thread::yield();
        for(unsigned n=0;n<2000;++n)
        {
            if(!engine.setFrequencyDispersionControl("frequencyHz",n%2?20.f:20000.f)
                || !engine.setFrequencyDispersionControl("mix",n%2?1.f:0.f))valid.store(false);
            engine.setBypass(n%7==0);
        }
        done.store(true);
    });
    bool ok=true;start.store(true);
    do {audio.clear();audio.setSample(0,0,.5f);ok &= engine.process(juce::dsp::AudioBlock<float>(audio),false);}while(!done.load());
    producer.join();REQUIRE(ok);REQUIRE(valid.load());
}
