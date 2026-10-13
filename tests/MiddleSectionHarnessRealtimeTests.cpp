#include "AllocationTracking.h"
#include "harness/AuditionEngine.h"
#include <catch2/catch_test_macros.hpp>
#include <thread>
using namespace disdorktion;

TEST_CASE("Middle harness scalar transitions processing and reset allocate nothing", "[middle-section][harness-realtime]")
{
    disdorktion::harness::AuditionEngine engine;
    REQUIRE(engine.selectTarget("middle-section")); REQUIRE(engine.prepare({48000,7,2}));
    juce::AudioBuffer<float> audio(2,1027);audio.clear();bool ok=true;
    {
        test::ScopedAllocationTracking tracking;
        for(unsigned n=0;n<100;++n)
        {
            ok &= engine.setMiddleTanhDriveDb(n%2?-24.f:36.f);ok &= engine.setMiddleTanhOutputDb(n%2?-60.f:12.f);
            ok &= engine.setMiddleFolderDriveDb(n%2?36.f:-24.f);ok &= engine.setMiddleFolderOutputDb(n%2?12.f:-60.f);
            ok &= engine.setMiddleThreshold(n%2?.05f:1.f);
            engine.setMiddleTanhBypass(n%3==0);engine.setMiddleFolderBypass(n%5==0);engine.setBypass(n%7==0);
            ok &= !engine.setMiddleTanhDriveDb(37.f);ok &= !engine.setMiddleThreshold(0.f);
            ok &= engine.process(juce::dsp::AudioBlock<float>(audio),false);(void)engine.meters();engine.reset();
        }
    }
    const auto counts=test::getAllocationCounts();REQUIRE(ok);REQUIRE(counts.allocations==0);REQUIRE(counts.deallocations==0);
}

TEST_CASE("Middle live controls may publish concurrently with processing", "[middle-section][harness-realtime]")
{
    disdorktion::harness::AuditionEngine engine;
    REQUIRE(engine.selectTarget("middle-section"));REQUIRE(engine.prepare({48000,64,2}));
    juce::AudioBuffer<float> audio(2,64);std::atomic<bool> start{false},done{false},valid{true};
    std::thread controls([&]{while(!start.load())std::this_thread::yield();
        for(unsigned n=0;n<2000;++n)
        {
            bool ok=engine.setMiddleTanhDriveDb(n%2?-24.f:36.f)&&engine.setMiddleTanhOutputDb(n%2?-60.f:12.f)
                &&engine.setMiddleFolderDriveDb(n%2?36.f:-24.f)&&engine.setMiddleFolderOutputDb(n%2?12.f:-60.f)&&engine.setMiddleThreshold(n%2?.05f:1.f);
            if(!ok)valid.store(false);engine.setMiddleTanhBypass(n%3==0);engine.setMiddleFolderBypass(n%5==0);engine.setBypass(n%7==0);
        }done.store(true);});
    bool ok=true;start.store(true);
    do {for(int c=0;c<2;++c)for(int n=0;n<64;++n)audio.setSample(c,n,.25f);ok &= engine.process(juce::dsp::AudioBlock<float>(audio),false);}while(!done.load());
    controls.join();REQUIRE(ok);REQUIRE(valid.load());
}
