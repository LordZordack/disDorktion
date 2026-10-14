#pragma once
#include "dsp/OutputSection.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace disdorktion::benchmark
{
struct OutputBenchmarkControl
{
    OutputSettings retained {};
    bool commonBypass = false;
    OutputSettings effectiveSettings() const noexcept
    {
        auto effective = retained;
        effective.highPass.bypass |= commonBypass;
        effective.lowPass.bypass |= commonBypass;
        return effective;
    }
};

inline OutputSettings outputFixedSettings(float high, float low, unsigned pair = 0,
                                         unsigned bypass = 0) noexcept
{
    return {{high, (bypass & 1) != 0, static_cast<FilterResponse>(pair / 3)},
            {low, (bypass & 2) != 0, static_cast<FilterResponse>(pair % 3)}};
}

// Every adjacent event changes both responses, including the cycle boundary.
// The 9-, 36- and 101-event cycles are independent; their joint period is 3636.
inline constexpr std::array<unsigned, 9> outputResponsePairSequence {0, 4, 8, 1, 5, 6, 2, 3, 7};
inline unsigned outputAutomationPair(std::uint64_t iteration) noexcept
{ return outputResponsePairSequence[iteration % outputResponsePairSequence.size()]; }
inline unsigned outputAutomationBypass(std::uint64_t iteration) noexcept
{ return static_cast<unsigned>((iteration / 9) % 4); }
inline OutputBenchmarkControl outputAutomationControl(std::uint64_t iteration) noexcept
{
    const auto residue = iteration % 101;
    const float high = 20.0f * std::pow(1000.0f, static_cast<float>(residue) / 100.0f);
    const float low = 20.0f * std::pow(1000.0f, static_cast<float>((residue * 37) % 101) / 100.0f);
    return {outputFixedSettings(high, low, outputAutomationPair(iteration),
                               outputAutomationBypass(iteration)), false};
}
inline unsigned outputBypassState(std::uint64_t iteration) noexcept
{ return static_cast<unsigned>((iteration / 8) % 4); }
inline OutputBenchmarkControl outputBypassControl(std::uint64_t iteration) noexcept
{ return {outputFixedSettings(200.0f, 8000.0f, 8, outputBypassState(iteration)), false}; }
inline OutputBenchmarkControl outputCommonBypassControl(std::uint64_t iteration) noexcept
{
    return {outputFixedSettings(200.0f, 8000.0f, 8, static_cast<unsigned>((iteration / 16) % 4)),
            (iteration / 8) % 2 != 0};
}

template<std::size_t Count>
class OutputControlCoverage
{
public:
    void recordState(std::size_t state) noexcept { if (state < Count) ++counts[state]; }
    std::size_t coveredCount() const noexcept
    { return static_cast<std::size_t>(std::count_if(counts.begin(), counts.end(), [](auto n) { return n != 0; })); }
    std::size_t minimum() const noexcept { return *std::min_element(counts.begin(), counts.end()); }
    bool qualifies() const noexcept { return minimum() >= 100; }
private:
    std::array<std::size_t, Count> counts {};
};
class OutputInteractionCoverage : public OutputControlCoverage<36>
{
public:
    void record(std::uint64_t iteration) noexcept
    { recordState(outputAutomationPair(iteration) * 4 + outputAutomationBypass(iteration)); }
};
class OutputBypassCoverage : public OutputControlCoverage<4>
{
public:
    void record(std::uint64_t iteration) noexcept { recordState(outputBypassState(iteration)); }
};
class OutputCommonBypassCoverage : public OutputControlCoverage<8>
{
public:
    void record(std::uint64_t iteration) noexcept { recordState(static_cast<std::size_t>((iteration / 8) % 8)); }
};
}
