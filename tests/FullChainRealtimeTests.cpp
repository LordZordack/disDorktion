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
        for(unsigned n=0;n<64;++n)
        {
            FullChainSettings s;s.mix=n/63.f;s.bypass=(n&1)!=0;s.input.gain.gainDb=12;
            s.middle.tanh.driveDb=24;s.output.highPass.cutoffHz=100.f+n*100.f;
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
