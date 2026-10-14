#include "../benchmarks/OutputSectionBenchmarkPath.h"
#include "../benchmarks/AutomationCoverage.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cmath>
#include <limits>

using namespace disdorktion;
using namespace disdorktion::benchmark;

TEST_CASE("Output automation changes both cutoffs and both responses at each callback", "[benchmark-sampling][output-section]")
{
    std::array<unsigned, 36> actualPairs {};
    std::array<bool, 101> highCutoffs {}, lowCutoffs {};
    bool sawHighMinimum = false, sawHighMaximum = false, sawLowMinimum = false, sawLowMaximum = false;
    auto previous = outputAutomationControl(3635).retained;
    for (std::uint64_t iteration = 0; iteration < 3636; ++iteration)
    {
        const auto control = outputAutomationControl(iteration);
        const auto& settings = control.retained;
        REQUIRE(isValidOutputSettings(settings));
        REQUIRE_FALSE(control.commonBypass);
        REQUIRE(settings.highPass.cutoffHz != previous.highPass.cutoffHz);
        REQUIRE(settings.lowPass.cutoffHz != previous.lowPass.cutoffHz);
        REQUIRE(settings.highPass.response != previous.highPass.response);
        REQUIRE(settings.lowPass.response != previous.lowPass.response);
        const auto pair = 3 * static_cast<unsigned>(settings.highPass.response)
            + static_cast<unsigned>(settings.lowPass.response);
        const auto bypass = static_cast<unsigned>(settings.highPass.bypass)
            + 2 * static_cast<unsigned>(settings.lowPass.bypass);
        ++actualPairs[pair * 4 + bypass];
        // Check settings-derived positions, independently of the schedule helper.
        const auto highPosition = static_cast<unsigned>(std::lround(100 * std::log(settings.highPass.cutoffHz / 20) / std::log(1000.0)));
        const auto lowPosition = static_cast<unsigned>(std::lround(100 * std::log(settings.lowPass.cutoffHz / 20) / std::log(1000.0)));
        REQUIRE(highPosition < highCutoffs.size()); REQUIRE(lowPosition < lowCutoffs.size());
        highCutoffs[highPosition] = true; lowCutoffs[lowPosition] = true;
        sawHighMinimum |= settings.highPass.cutoffHz == 20;
        sawHighMaximum |= settings.highPass.cutoffHz == 20000;
        sawLowMinimum |= settings.lowPass.cutoffHz == 20;
        sawLowMaximum |= settings.lowPass.cutoffHz == 20000;
        previous = settings;
    }
    for (const auto count : actualPairs) REQUIRE(count == 101);
    for (const auto seen : highCutoffs) REQUIRE(seen);
    for (const auto seen : lowCutoffs) REQUIRE(seen);
    REQUIRE(sawHighMinimum); REQUIRE(sawHighMaximum); REQUIRE(sawLowMinimum); REQUIRE(sawLowMaximum);
}

TEST_CASE("Output sampled automation rejects aliasing and requires all response and bypass pairs", "[benchmark-sampling][output-section]")
{
    OutputInteractionCoverage coverage;
    REQUIRE(coverage.coveredCount() == 0); REQUIRE_FALSE(coverage.qualifies());
    for (unsigned count = 0; count < 99; ++count)
        for (std::uint64_t event = 0; event < 36; ++event) coverage.record(event);
    REQUIRE(coverage.coveredCount() == 36); REQUIRE(coverage.minimum() == 99); REQUIRE_FALSE(coverage.qualifies());
    for (std::uint64_t event = 0; event < 35; ++event) coverage.record(event);
    REQUIRE(coverage.minimum() == 99); REQUIRE_FALSE(coverage.qualifies());
    coverage.record(35);
    REQUIRE(coverage.minimum() == 100); REQUIRE(coverage.qualifies());
    OutputInteractionCoverage aliased;
    AutomationCoverage cutoffAliased;
    for (std::uint64_t observation = 0; observation < 150000; ++observation)
    {
        aliased.record(observation * 3636 + 17);
        cutoffAliased.record(observation * 3636 + 17);
    }
    REQUIRE(aliased.coveredCount() == 1); REQUIRE(aliased.minimum() == 0); REQUIRE_FALSE(aliased.qualifies());
    REQUIRE(cutoffAliased.coveredCount() == 1); REQUIRE_FALSE(cutoffAliased.qualifies());
    OutputInteractionCoverage complete;
    AutomationCoverage cutoffs;
    for (std::uint64_t event = 0; event < 3 * 3636; ++event) { complete.record(event); cutoffs.record(event); }
    REQUIRE(complete.coveredCount() == 36); REQUIRE(complete.minimum() == 303); REQUIRE(complete.qualifies());
    REQUIRE(cutoffs.coveredCount() == 101); REQUIRE(cutoffs.minimum() == 108); REQUIRE(cutoffs.qualifies());
}

TEST_CASE("Output bypass applies independent flags at exact eight callback boundaries", "[benchmark-sampling][output-section]")
{
    for (const auto& event : std::array<std::array<std::uint64_t, 3>, 10> {{
        {{0, 0, 0}}, {{7, 0, 0}}, {{8, 1, 0}}, {{15, 1, 0}}, {{16, 0, 1}},
        {{23, 0, 1}}, {{24, 1, 1}}, {{31, 1, 1}}, {{32, 0, 0}}, {{39, 0, 0}} }})
    {
        const auto control = outputBypassControl(event[0]);
        REQUIRE(isValidOutputSettings(control.retained));
        REQUIRE_FALSE(control.commonBypass);
        REQUIRE(control.retained.highPass.bypass == static_cast<bool>(event[1]));
        REQUIRE(control.retained.lowPass.bypass == static_cast<bool>(event[2]));
        REQUIRE(control.retained.highPass.cutoffHz == 200); REQUIRE(control.retained.lowPass.cutoffHz == 8000);
        REQUIRE(control.retained.highPass.response == FilterResponse::chebyshev2);
        REQUIRE(control.retained.lowPass.response == FilterResponse::chebyshev2);
    }
    OutputBypassCoverage coverage;
    for (unsigned observation = 0; observation < 99; ++observation)
        for (const std::uint64_t event : {0u, 8u, 16u, 24u}) coverage.record(event);
    REQUIRE(coverage.coveredCount() == 4); REQUIRE(coverage.minimum() == 99); REQUIRE_FALSE(coverage.qualifies());
    for (const std::uint64_t event : {0u, 8u, 16u}) coverage.record(event);
    REQUIRE_FALSE(coverage.qualifies()); coverage.record(24); REQUIRE(coverage.qualifies());
}

TEST_CASE("Output common bypass retains every stage selection and restores it on release", "[benchmark-sampling][output-section]")
{
    // Explicit end points prove common events occur every eight callbacks, while
    // the retained selection changes every sixteen, including wraparound.
    for (const auto& event : std::array<std::array<std::uint64_t, 4>, 18> {{
        {{0,0,0,0}}, {{7,0,0,0}}, {{8,0,0,1}}, {{15,0,0,1}},
        {{16,1,0,0}}, {{23,1,0,0}}, {{24,1,0,1}}, {{31,1,0,1}},
        {{32,0,1,0}}, {{39,0,1,0}}, {{40,0,1,1}}, {{47,0,1,1}},
        {{48,1,1,0}}, {{55,1,1,0}}, {{56,1,1,1}}, {{63,1,1,1}},
        {{64,0,0,0}}, {{72,0,0,1}} }})
    {
        const auto control = outputCommonBypassControl(event[0]);
        const auto effective = control.effectiveSettings();
        REQUIRE(isValidOutputSettings(effective));
        REQUIRE(control.retained.highPass.bypass == static_cast<bool>(event[1]));
        REQUIRE(control.retained.lowPass.bypass == static_cast<bool>(event[2]));
        REQUIRE(control.commonBypass == static_cast<bool>(event[3]));
        REQUIRE(effective.highPass.bypass == static_cast<bool>(event[1] || event[3]));
        REQUIRE(effective.lowPass.bypass == static_cast<bool>(event[2] || event[3]));
        REQUIRE(control.retained.highPass.bypass == static_cast<bool>(event[1]));
        REQUIRE(control.retained.lowPass.bypass == static_cast<bool>(event[2]));
        REQUIRE(effective.highPass.cutoffHz == 200); REQUIRE(effective.lowPass.cutoffHz == 8000);
        REQUIRE(effective.highPass.response == FilterResponse::chebyshev2);
        REQUIRE(effective.lowPass.response == FilterResponse::chebyshev2);
    }
    OutputCommonBypassCoverage coverage;
    for (unsigned observation = 0; observation < 99; ++observation)
        for (std::uint64_t event = 0; event < 64; event += 8) coverage.record(event);
    REQUIRE(coverage.coveredCount() == 8); REQUIRE(coverage.minimum() == 99); REQUIRE_FALSE(coverage.qualifies());
    for (std::uint64_t event = 0; event < 56; event += 8) coverage.record(event);
    REQUIRE_FALSE(coverage.qualifies()); coverage.record(56); REQUIRE(coverage.qualifies());
    OutputCommonBypassCoverage aliased;
    for (unsigned observation = 0; observation < 150000; ++observation) aliased.record(8);
    REQUIRE(aliased.coveredCount() == 1); REQUIRE(aliased.minimum() == 0); REQUIRE_FALSE(aliased.qualifies());
}
