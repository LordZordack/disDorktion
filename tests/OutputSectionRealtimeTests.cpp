#include "AllocationTracking.h"
#include "dsp/OutputSection.h"
#include <catch2/catch_test_macros.hpp>
using namespace disdorktion;
TEST_CASE("Output section oversized transitions reset and rejections allocate nothing", "[output-section][realtime]")
{
    OutputSection section;
    REQUIRE(section.prepare({96000,7,2}));
    juce::AudioBuffer<float> audio(2,1031), wrong(1,32); audio.clear(); wrong.clear();
    bool ok=true;
    {
        test::ScopedAllocationTracking tracking;
        for(unsigned n=0;n<100;++n)
        {
            OutputSettings settings {{n%2?20.f:20000.f,n%3==0,static_cast<FilterResponse>(n%3)},
                {n%2?20000.f:20.f,n%5==0,static_cast<FilterResponse>((n+1)%3)}};
            settings.comb={n%2?.25f:40.f,n%2?-1.f:1.f,n%2?-.95f:.95f,20.f,1.f,n%2==0,n%7==0};
            settings.dispersion={n%2?20.f:20000.f,1.f,n%3==0};
            settings.tone={n%2?-6.f:6.f,n%5==0};
            ok &= section.setParameters(settings);
            ok &= section.process(juce::dsp::AudioBlock<float>(audio));
            auto invalid=settings; invalid.lowPass.cutoffHz=19.f;
            ok &= !section.setParameters(invalid);
            ok &= !section.prepare({0,7,2});
            ok &= !section.process(juce::dsp::AudioBlock<float>(wrong));
            section.reset();
        }
    }
    const auto counts=test::getAllocationCounts(); REQUIRE(ok);
    REQUIRE(counts.allocations==0); REQUIRE(counts.deallocations==0);
}
