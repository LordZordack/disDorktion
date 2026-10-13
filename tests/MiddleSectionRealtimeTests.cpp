#include "AllocationTracking.h"
#include "dsp/MiddleSection.h"
#include <catch2/catch_test_macros.hpp>

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
