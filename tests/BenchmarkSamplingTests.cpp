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
