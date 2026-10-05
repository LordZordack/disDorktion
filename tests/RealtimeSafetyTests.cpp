#include "AllocationTracking.h"
#include "dsp/ReferenceGain.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <new>

namespace
{
volatile std::uintptr_t allocationProbe = 0;

void exerciseReplacementOperators()
{
    // Each allocation and matching deallocation is an explicit call. The volatile
    // stores keep the returned pointers observable in optimized builds.
    auto* p0 = ::operator new(17); allocationProbe = reinterpret_cast<std::uintptr_t>(p0); ::operator delete(p0);
    auto* p1 = ::operator new[](19); allocationProbe = reinterpret_cast<std::uintptr_t>(p1); ::operator delete[](p1);
    auto* p2 = ::operator new(21); allocationProbe = reinterpret_cast<std::uintptr_t>(p2); ::operator delete(p2, 21);
    auto* p3 = ::operator new[](23); allocationProbe = reinterpret_cast<std::uintptr_t>(p3); ::operator delete[](p3, 23);

    auto* p4 = ::operator new(25, std::nothrow); allocationProbe = reinterpret_cast<std::uintptr_t>(p4); ::operator delete(p4, std::nothrow);
    auto* p5 = ::operator new[](27, std::nothrow); allocationProbe = reinterpret_cast<std::uintptr_t>(p5); ::operator delete[](p5, std::nothrow);

    constexpr std::align_val_t alignment {64};
    auto* p6 = ::operator new(29, alignment); allocationProbe = reinterpret_cast<std::uintptr_t>(p6); ::operator delete(p6, alignment);
    auto* p7 = ::operator new[](31, alignment); allocationProbe = reinterpret_cast<std::uintptr_t>(p7); ::operator delete[](p7, 31, alignment);
    auto* p8 = ::operator new(33, alignment); allocationProbe = reinterpret_cast<std::uintptr_t>(p8); ::operator delete(p8, 33, alignment);
    auto* p9 = ::operator new[](35, alignment); allocationProbe = reinterpret_cast<std::uintptr_t>(p9); ::operator delete[](p9, alignment);

    auto* p10 = ::operator new(37, alignment, std::nothrow);
    allocationProbe = reinterpret_cast<std::uintptr_t>(p10);
    ::operator delete(p10, alignment, std::nothrow);
    auto* p11 = ::operator new[](39, alignment, std::nothrow);
    allocationProbe = reinterpret_cast<std::uintptr_t>(p11);
    ::operator delete[](p11, alignment, std::nothrow);
}
}

TEST_CASE("Allocation tracker observes its global replacement overloads", "[realtime][allocation]")
{
    {
        disdorktion::test::ScopedAllocationTracking tracking;
        exerciseReplacementOperators();
    }

    const auto result = disdorktion::test::getAllocationCounts();
    REQUIRE(result.allocations == 12);
    REQUIRE(result.deallocations == 12);
}

TEST_CASE("Nested allocation tracking preserves the outer counts", "[realtime][allocation]")
{
    {
        disdorktion::test::ScopedAllocationTracking outer;
        auto* beforeNested = ::operator new(41);
        allocationProbe = reinterpret_cast<std::uintptr_t>(beforeNested);
        ::operator delete(beforeNested);

        {
            disdorktion::test::ScopedAllocationTracking inner;
            auto* insideNested = ::operator new(43);
            allocationProbe = reinterpret_cast<std::uintptr_t>(insideNested);
            ::operator delete(insideNested);
        }
    }

    const auto result = disdorktion::test::getAllocationCounts();
    REQUIRE(result.allocations == 2);
    REQUIRE(result.deallocations == 2);
}

TEST_CASE("ReferenceGain realtime calls do not allocate or deallocate", "[realtime]")
{
    using namespace disdorktion;

    ReferenceGain mono;
    ReferenceGain stereo;
    REQUIRE(mono.prepare({48000.0, 8, 1}));
    REQUIRE(stereo.prepare({48000.0, 8, 2}));

    juce::AudioBuffer<float> monoBuffer(1, 17);
    juce::AudioBuffer<float> stereoBuffer(2, 17);
    juce::AudioBuffer<float> wrongLayoutBuffer(2, 4);
    monoBuffer.clear();
    stereoBuffer.clear();
    wrongLayoutBuffer.clear();
    auto monoBlock = juce::dsp::AudioBlock<float>(monoBuffer);
    auto stereoBlock = juce::dsp::AudioBlock<float>(stereoBuffer);
    auto monoEmptyBlock = monoBlock.getSubBlock(0, 0);
    auto stereoEmptyBlock = stereoBlock.getSubBlock(0, 0);
    auto wrongLayoutBlock = juce::dsp::AudioBlock<float>(wrongLayoutBuffer);

    bool validMonoParameters = false;
    bool invalidMonoParameters = true;
    bool validStereoParameters = false;
    bool invalidStereoParameters = true;
    bool monoZeroResult = false;
    bool monoOversizedResult = false;
    bool monoWrongLayoutResult = true;
    bool stereoZeroResult = false;
    bool stereoOversizedResult = false;
    bool stereoWrongLayoutResult = true;

    {
        test::ScopedAllocationTracking tracking;
        validMonoParameters = mono.setParameters({0.5f, false});
        invalidMonoParameters = mono.setParameters({std::numeric_limits<float>::quiet_NaN(), true});
        mono.reset();
        monoZeroResult = mono.process(monoEmptyBlock);
        monoOversizedResult = mono.process(monoBlock);
        monoWrongLayoutResult = mono.process(wrongLayoutBlock);

        validStereoParameters = stereo.setParameters({2.0f, true});
        invalidStereoParameters = stereo.setParameters({-0.1f, false});
        stereo.reset();
        stereoZeroResult = stereo.process(stereoEmptyBlock);
        stereoOversizedResult = stereo.process(stereoBlock);
        stereoWrongLayoutResult = stereo.process(juce::dsp::AudioBlock<float>(monoBuffer));
    }

    const auto result = test::getAllocationCounts();
    REQUIRE(validMonoParameters);
    REQUIRE_FALSE(invalidMonoParameters);
    REQUIRE(validStereoParameters);
    REQUIRE_FALSE(invalidStereoParameters);
    REQUIRE(monoZeroResult);
    REQUIRE(monoOversizedResult);
    REQUIRE_FALSE(monoWrongLayoutResult);
    REQUIRE(stereoZeroResult);
    REQUIRE(stereoOversizedResult);
    REQUIRE_FALSE(stereoWrongLayoutResult);
    REQUIRE(result.allocations == 0);
    REQUIRE(result.deallocations == 0);
}
