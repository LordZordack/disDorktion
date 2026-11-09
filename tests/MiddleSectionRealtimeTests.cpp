#include "AllocationTracking.h"
#include "dsp/MiddleSection.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Driver smoothing compensation and inactive transitions allocate nothing", "[middle][driver-adjustment][realtime]")
{
    disdorktion::MiddleSection section;
    REQUIRE(section.prepare({48000,7,2},4,disdorktion::MiddleLatencyCorrection::fir,disdorktion::MiddleFoldAlgorithm::adaaTriangle));
    juce::AudioBuffer<float> audio(2,1031); bool ok=true;
    {
        disdorktion::test::ScopedAllocationTracking tracking;
        for(int epoch=0;epoch<24;++epoch)
        {
            ok &= section.setDriver(1.f+epoch%4);
            ok &= !section.setDriver(0.f);
            const bool off=epoch%3==0;
            ok &= section.setParameters({{12.f,0.f,off},{9.f,.4f,0.f,off},epoch%4 ? .5f : 0.f,0.f});
            for(int c=0;c<2;++c) for(int n=0;n<1031;++n) audio.setSample(c,n,.1f*(c+1));
            ok &= section.process(juce::dsp::AudioBlock<float>(audio));
            ok &= section.process(juce::dsp::AudioBlock<float>(audio).getSubBlock(0,0));
        }
        section.reset();
    }
    const auto counts=disdorktion::test::getAllocationCounts();
    REQUIRE(ok); REQUIRE(counts.allocations==0); REQUIRE(counts.deallocations==0);
}

TEST_CASE("Middle FIR processing updates and reset allocate nothing", "[middle][realtime]")
{
    disdorktion::MiddleSection m;
    REQUIRE(m.prepare({96000,127,2},4));
    juce::AudioBuffer<float> b(2,4099); b.clear();
    bool ok=true;
    {
        disdorktion::test::ScopedAllocationTracking tracking;
        for (int i=0;i<20;++i)
        {
            ok &= m.setParameters({{i%2 ? 36.f : -24.f,-12,i%3==0},{18,.25f,-12,i%4==0}});
            ok &= !m.setParameters({{37,0,false},{0,1,0,false}});
            ok &= m.process(juce::dsp::AudioBlock<float>(b).getSubBlock(0,0));
            for(int n=0;n<4099;++n) { b.setSample(0,n,.25f); b.setSample(1,n,-.125f); }
            ok &= m.process(juce::dsp::AudioBlock<float>(b));
            for(int c=0;c<2;++c) for(int n=0;n<4099;++n) ok &= std::isfinite(b.getSample(c,n));
            m.reset();
        }
    }
    const auto counts=disdorktion::test::getAllocationCounts();
    REQUIRE(ok); REQUIRE(counts.allocations==0); REQUIRE(counts.deallocations==0);
}

TEST_CASE("ADAA middle processing control transitions and reset allocate nothing", "[middle][adaa][realtime]")
{
    using namespace disdorktion;
    MiddleSection m; REQUIRE(m.prepare({96000,127,2},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
    juce::AudioBuffer<float> b(2,4099); b.clear(); bool ok=true;
    {
        test::ScopedAllocationTracking tracking;
        for(int i=0;i<20;++i)
        {
            ok &= m.setParameters({{i%2 ? 36.f : -24.f,-12,i%3==0},{i%2 ? 36.f : -24.f,i%2 ? .05f : 1.f,-12,i%4==0}});
            ok &= !m.setParameters({{0,0,false},{37,1,0,false}});
            ok &= m.process(juce::dsp::AudioBlock<float>(b).getSubBlock(0,0));
            for(int n=0;n<4099;++n) { b.setSample(0,n,.25f); b.setSample(1,n,-.125f); }
            ok &= m.process(juce::dsp::AudioBlock<float>(b));
            for(int c=0;c<2;++c) for(int n=0;n<4099;++n) ok &= std::isfinite(b.getSample(c,n));
            m.reset();
        }
    }
    const auto counts=test::getAllocationCounts(); REQUIRE(ok); REQUIRE(counts.allocations==0); REQUIRE(counts.deallocations==0);
}

TEST_CASE("Waveshaper section suspension warming and trim allocate nothing", "[middle][section-mix][realtime]")
{
    using namespace disdorktion;
    MiddleSection section;
    REQUIRE(section.prepare({48000,127,2},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
    juce::AudioBuffer<float> audio(2,4099); audio.clear(); bool ok=true;
    {
        test::ScopedAllocationTracking tracking;
        for(int epoch=0;epoch<16;++epoch)
        {
            const bool off=epoch%3==0;
            ok &= section.setParameters({{18,0,off},{24,.25f,0,off},epoch%4==0 ? 0.f : .5f,epoch%2==0 ? 6.f : -6.f});
            ok &= section.process(juce::dsp::AudioBlock<float>(audio).getSubBlock(0,epoch%2==0 ? 67 : 4099));
        }
        section.reset();
    }
    const auto counts=test::getAllocationCounts();
    REQUIRE(ok); REQUIRE(counts.allocations==0); REQUIRE(counts.deallocations==0);
}
