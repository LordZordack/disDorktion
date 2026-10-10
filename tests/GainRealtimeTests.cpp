#include "AllocationTracking.h"
#include "dsp/Gain.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

TEST_CASE("Production Gain controls reset and accepted or rejected processing allocate nothing", "[production-gain][realtime]")
{
    using namespace disdorktion;
    Gain mono, stereo, unprepared;
    REQUIRE(mono.prepare({48000.0, 8, 1}));
    REQUIRE(stereo.prepare({48000.0, 8, 2}));
    juce::AudioBuffer<float> m(1, 1027), s(2, 1027);
    m.clear(); s.clear();
    auto mb = juce::dsp::AudioBlock<float>(m);
    auto sb = juce::dsp::AudioBlock<float>(s);
    bool success = true;
    {
        test::ScopedAllocationTracking tracking;
        for (int i = 0; i < 64; ++i)
        {
            success &= mono.setParameters({i % 2 ? -60.0f : 24.0f, i % 3 == 0, i % 5 == 0});
            success &= stereo.setParameters({i % 2 ? 12.0f : -6.0f, i % 5 == 0, i % 3 == 0});
            success &= !mono.setParameters({std::numeric_limits<float>::quiet_NaN(), true, true});
            success &= !stereo.setParameters({std::numeric_limits<float>::infinity(), true, true});
            success &= !mono.setParameters({-61.0f, true, true});
            success &= !stereo.setParameters({25.0f, true, true});
            success &= mono.process(mb.getSubBlock(0, 0));
            success &= stereo.process(sb.getSubBlock(0, 0));
            success &= mono.process(mb);
            success &= stereo.process(sb);
            success &= !mono.process(sb);
            success &= !stereo.process(mb);
            success &= !unprepared.process(mb);
            mono.reset(); stereo.reset(); unprepared.reset();
            success &= mono.process(mb);
            success &= stereo.process(sb);
        }
    }
    const auto counts = test::getAllocationCounts();
    REQUIRE(success);
    REQUIRE(counts.allocations == 0);
    REQUIRE(counts.deallocations == 0);
}
