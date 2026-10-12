#include "AllocationTracking.h"
#include "dsp/WaveFolder.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>

TEST_CASE("Wave folder processing parameter rejection and reset allocate nothing", "[wave-folder][realtime]")
{
    using namespace disdorktion;
    WaveFolder mono, stereo, unprepared;
    REQUIRE(mono.prepare({48000, 1, 1})); REQUIRE(stereo.prepare({96000, 127, 2}));
    juce::AudioBuffer<float> m(1, 4099), s(2, 4099);
    for (int n = 0; n < 4099; ++n)
    {
        m.setSample(0, n, .25f); s.setSample(0, n, .5f); s.setSample(1, n, -.125f);
    }
    auto mb = juce::dsp::AudioBlock<float>(m), sb = juce::dsp::AudioBlock<float>(s);
    bool success = true;
    {
        test::ScopedAllocationTracking tracking;
        for (int i = 0; i < 64; ++i)
        {
            success &= mono.setParameters({i % 2 ? -24.f : 36.f, i % 2 ? 1.f : .05f, i % 2 ? -60.f : 12.f, i % 3 == 0});
            success &= stereo.setParameters({i % 2 ? 36.f : -24.f, i % 2 ? .05f : 1.f, i % 2 ? 12.f : -60.f, i % 5 == 0});
            success &= !mono.setParameters({37, 1, 0, true});
            success &= !stereo.setParameters({0, std::numeric_limits<float>::quiet_NaN(), 0, true});
            success &= mono.process(mb.getSubBlock(0, 0)); success &= stereo.process(sb.getSubBlock(0, 0));
            success &= mono.process(mb); success &= stereo.process(sb);
            success &= !mono.process(sb); success &= !stereo.process(mb); success &= !unprepared.process(mb);
            mono.reset(); stereo.reset();
        }
    }
    const auto counts = test::getAllocationCounts();
    REQUIRE(success); REQUIRE(counts.allocations == 0); REQUIRE(counts.deallocations == 0);
}
