#include "../benchmarks/WallTimeSampling.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
using disdorktion::benchmark::WallTimeSampler;

TEST_CASE("Wall-time sampling covers exact slots with bounded count and excluded endpoint", "[benchmark-sampling]")
{
    for (const std::int64_t window : {200000000LL, 10000000000LL, 30000000000LL, 120000000000LL})
    {
        WallTimeSampler sampler(window);
        REQUIRE_FALSE(sampler.shouldSample(-1));
        std::size_t count = 0;
        for (std::int64_t slot = 0; slot < 150000; ++slot)
        {
            const auto beginning = (slot * window + 149999) / 150000;
            REQUIRE(sampler.shouldSample(beginning)); ++count;
            REQUIRE_FALSE(sampler.shouldSample(beginning));
            const auto next = ((slot + 1) * window + 149999) / 150000;
            REQUIRE_FALSE(sampler.shouldSample(next - 1));
        }
        REQUIRE(count == WallTimeSampler::targetSlots);
        REQUIRE_FALSE(sampler.shouldSample(window));
        REQUIRE_FALSE(sampler.shouldSample(window + 1));
    }
}

TEST_CASE("Wall-time sampling skips missed slots without catchup and rejects backwards clock samples", "[benchmark-sampling]")
{
    constexpr std::int64_t slotWidth = 100000;
    WallTimeSampler sampler(slotWidth * 150000);
    REQUIRE(sampler.shouldSample(0));
    REQUIRE(sampler.shouldSample(100 * slotWidth));
    REQUIRE_FALSE(sampler.shouldSample(100 * slotWidth + 1));
    REQUIRE_FALSE(sampler.shouldSample(99 * slotWidth));
    REQUIRE_FALSE(sampler.shouldSample(-1));
    REQUIRE(sampler.shouldSample(101 * slotWidth));
    REQUIRE(sampler.shouldSample(149999 * slotWidth));
    REQUIRE_FALSE(sampler.shouldSample(150000 * slotWidth));
    WallTimeSampler slow(slotWidth * 150000);
    std::size_t count = 0;
    for (std::int64_t slot = 0; slot < 150000; slot += 3) count += slow.shouldSample(slot * slotWidth) ? 1 : 0;
    REQUIRE(count == 50000); // Slow callbacks cannot synthesize missing observations.
}

TEST_CASE("Wall-time sampler rejects invalid collection windows", "[benchmark-sampling]")
{
    for (std::int64_t window : {0LL, -1LL, 120000000001LL})
    {
        WallTimeSampler sampler(window);
        REQUIRE_FALSE(sampler.shouldSample(0));
        REQUIRE_FALSE(sampler.shouldSample(1));
    }
}

#include "../benchmarks/AutomationCoverage.h"
TEST_CASE("Automation sampling coverage requires one hundred observations for every control setting", "[benchmark-sampling]")
{
    using disdorktion::benchmark::AutomationCoverage;
    AutomationCoverage coverage;
    REQUIRE_FALSE(coverage.qualifies()); REQUIRE(coverage.coveredCount() == 0);
    for (std::uint64_t n = 0; n < 101 * 99; ++n) coverage.record(n);
    REQUIRE(coverage.coveredCount() == 101); REQUIRE(coverage.minimum() == 99); REQUIRE(coverage.maximum() == 99);
    REQUIRE_FALSE(coverage.qualifies());
    for (std::uint64_t n = 0; n < 100; ++n) coverage.record(n);
    REQUIRE(coverage.minimum() == 99); REQUIRE(coverage.maximum() == 100); REQUIRE_FALSE(coverage.qualifies());
    coverage.record(100);
    REQUIRE(coverage.minimum() == 100); REQUIRE(coverage.maximum() == 100); REQUIRE(coverage.qualifies());
    AutomationCoverage aliased;
    for (std::uint64_t n = 0; n < 150000; ++n) aliased.record(n * 101 + 17);
    REQUIRE(aliased.coveredCount() == 1); REQUIRE(aliased.minimum() == 0); REQUIRE(aliased.maximum() == 150000);
    REQUIRE_FALSE(aliased.qualifies());
}
#include "../benchmarks/InputSectionBenchmarkPath.h"
TEST_CASE("Input automation samples every accepted depth and bypass pair", "[benchmark-sampling][input-section]")
{
    using namespace disdorktion::benchmark;
    InputInteractionCoverage coverage;
    REQUIRE_FALSE(coverage.qualifies());
    // One complete cycle visits each pair for 101 consecutive callbacks.
    for(std::uint64_t iteration=0;iteration<101*11*4;++iteration)
    {
        const auto settings=inputAutomationSettings(iteration);
        REQUIRE(disdorktion::isValidInputSettings(settings));
        coverage.record(iteration);
    }
    REQUIRE(coverage.coveredCount()==44);REQUIRE(coverage.minimum()==101);REQUIRE(coverage.qualifies());
    InputInteractionCoverage aliased;
    for(std::uint64_t iteration=0;iteration<150000;++iteration)aliased.record(iteration*(101*11*4));
    REQUIRE(aliased.coveredCount()==1);REQUIRE(aliased.minimum()==0);REQUIRE_FALSE(aliased.qualifies());
    // Continuous settings include endpoints; depth and bypass change independently.
    const auto low=inputAutomationSettings(0),high=inputAutomationSettings(100);
    REQUIRE(low.gain.gainDb==-60);REQUIRE(high.gain.gainDb==24);
    REQUIRE(low.crusher.captureRateHz==100);REQUIRE(high.crusher.captureRateHz==384000);
    REQUIRE(inputAutomationSettings(101*10).crusher.bitDepth==64);
    REQUIRE(inputAutomationSettings(101*11).gain.bypass);
    REQUIRE(inputAutomationSettings(101*11*2).crusher.bypass);
    REQUIRE(inputAutomationSettings(101*11*3).gain.bypass);
    REQUIRE(inputAutomationSettings(101*11*3).crusher.bypass);
}
TEST_CASE("Input expressive benchmark retains interior signal and near-native jitter capture", "[benchmark-sampling][input-section]")
{
    for(unsigned hostRate:{48000u,96000u})
    {
        const auto settings=disdorktion::benchmark::inputExpressiveSettings(hostRate);
        REQUIRE(disdorktion::isValidInputSettings(settings));
        REQUIRE(settings.gain.gainDb==24);
        REQUIRE(settings.crusher.captureRateHz==static_cast<float>(hostRate-1));
        REQUIRE(settings.crusher.jitter==1);
        REQUIRE(settings.crusher.crush==1);
        REQUIRE(settings.crusher.shape==.8f);
        REQUIRE((.5/16)*std::pow(10.0,settings.gain.gainDb/20.0)<1);
    }
}
TEST_CASE("Input dynamic bypass traverses all four states at eight-callback boundaries", "[benchmark-sampling][input-section]")
{
    using disdorktion::benchmark::inputBypassSettings;
    for(unsigned hostRate:{48000u,96000u})
    {
        // Explicit event endpoints ensure flags change at the intended callback,
        // both modules toggle, and the expressive workload survives each event.
        for(const auto& event:std::array<std::array<std::uint64_t,3>,10>{{
            {{0,0,0}},{{7,0,0}},{{8,1,0}},{{15,1,0}},{{16,0,1}},
            {{23,0,1}},{{24,1,1}},{{31,1,1}},{{32,0,0}},{{39,0,0}}}})
        {
            const auto settings=inputBypassSettings(event[0],hostRate);
            REQUIRE(settings.gain.bypass==static_cast<bool>(event[1]));
            REQUIRE(settings.crusher.bypass==static_cast<bool>(event[2]));
            REQUIRE(disdorktion::isValidInputSettings(settings));
            REQUIRE_FALSE(settings.gain.mute);
            REQUIRE(settings.gain.gainDb==24);
            REQUIRE(settings.crusher.captureRateHz==static_cast<float>(hostRate-1));
            REQUIRE(settings.crusher.crush==1);
            REQUIRE(settings.crusher.shape==.8f);
            REQUIRE(settings.crusher.jitter==1);
        }
    }
}
TEST_CASE("Input bypass qualification rejects missing or sparsely sampled states", "[benchmark-sampling][input-section]")
{
    using disdorktion::benchmark::InputBypassCoverage;
    InputBypassCoverage coverage;
    REQUIRE(coverage.coveredCount()==0);REQUIRE_FALSE(coverage.qualifies());
    for(unsigned observation=0;observation<99;++observation)
        for(const std::uint64_t event:{0u,8u,16u,24u})coverage.record(event);
    REQUIRE(coverage.coveredCount()==4);REQUIRE(coverage.minimum()==99);REQUIRE_FALSE(coverage.qualifies());
    for(const std::uint64_t event:{0u,8u,16u})coverage.record(event);
    REQUIRE(coverage.minimum()==99);REQUIRE_FALSE(coverage.qualifies());
    coverage.record(24);REQUIRE(coverage.minimum()==100);REQUIRE(coverage.qualifies());
    InputBypassCoverage aliased;
    for(unsigned observation=0;observation<150000;++observation)aliased.record(7);
    REQUIRE(aliased.coveredCount()==1);REQUIRE(aliased.minimum()==0);REQUIRE_FALSE(aliased.qualifies());
}
