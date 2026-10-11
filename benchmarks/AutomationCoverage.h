#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace disdorktion::benchmark
{
// Coverage of the 101-state tanh automation cycle, counted only for timed callbacks.
class AutomationCoverage
{
public:
    static constexpr std::size_t settingsCount = 101, requiredPerSetting = 100;
    void record(std::uint64_t controlIteration) noexcept { ++counts[controlIteration % settingsCount]; }
    std::size_t coveredCount() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(counts.begin(), counts.end(), [](auto count) { return count != 0; }));
    }
    std::size_t minimum() const noexcept { return *std::min_element(counts.begin(), counts.end()); }
    std::size_t maximum() const noexcept { return *std::max_element(counts.begin(), counts.end()); }
    bool qualifies() const noexcept { return minimum() >= requiredPerSetting; }
private:
    std::array<std::size_t, settingsCount> counts {};
};
}
