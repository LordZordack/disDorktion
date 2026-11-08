#include <catch2/catch_test_macros.hpp>
#include "dsp/FullChain.h"
#include "AllocationTracking.h"

using namespace disdorktion;
TEST_CASE("Full chain controls process reset and rejected calls allocate nothing", "[full-chain][rt]")
{
    FullChain chain; REQUIRE(chain.prepare({96000,7,2}));
    juce::AudioBuffer<float> audio(2,1031),wrong(1,31);audio.clear();wrong.clear();
    auto block=juce::dsp::AudioBlock<float>(audio);
    REQUIRE(chain.process(block));
    const auto before=test::getAllocationCounts();bool valid=true;
    {
        test::ScopedAllocationTracking track;
        for(bool expanded:{false,true})for(unsigned n=0;n<64;++n)
        {
            FullChainSettings s;s.mix=n/63.f;s.bypass=(n&1)!=0;s.input.gain.gainDb=12;
            s.middle.tanh.driveDb=24;s.output.highPass.cutoffHz=100.f+n*100.f;
            if(expanded)
            {
                s.output.comb={n%2?.25f:40.f,n%2?-1.f:1.f,n%2?-.95f:.95f,20.f,1.f,n%2==0,n%7==0};
                s.output.dispersion={n%2?20.f:20000.f,1.f,n%3==0};
                s.output.tone={n%2?-6.f:6.f,n%5==0};
            }
            valid &= chain.setParameters(s);valid &= chain.process(block);
            valid &= chain.process(block.getSubBlock(0,0));
            s.mix=2;valid &= !chain.setParameters(s);
            valid &= !chain.prepare({96000,0,2});
            valid &= !chain.process(juce::dsp::AudioBlock<float>(wrong));chain.reset();
        }
    }
    const auto after=test::getAllocationCounts();REQUIRE(valid);
    REQUIRE(after.allocations==before.allocations);REQUIRE(after.deallocations==before.deallocations);
}
