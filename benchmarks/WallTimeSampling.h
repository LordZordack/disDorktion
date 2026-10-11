#pragma once
#include <cstddef>
#include <cstdint>

namespace disdorktion::benchmark
{
// One observation per equal wall-time slot over the whole collection window.
// Missed slots stay missed: a delayed callback cannot create catch-up samples.
class WallTimeSampler
{
public:
    static constexpr std::size_t targetSlots = 150000;
    explicit WallTimeSampler(std::int64_t windowNanoseconds) noexcept : window(windowNanoseconds) {}
    bool shouldSample(std::int64_t elapsedNanoseconds) noexcept
    {
        if (window <= 0 || window > 120000000000LL || elapsedNanoseconds < 0 || elapsedNanoseconds >= window)
            return false;
        // The supported <=120 s window bounds this multiplication below 1.8e16.
        const auto slot = static_cast<std::size_t>(elapsedNanoseconds * static_cast<std::int64_t>(targetSlots) / window);
        if (previousSlot != targetSlots && slot <= previousSlot) return false;
        previousSlot = slot;
        return true;
    }
private:
    std::int64_t window;
    std::size_t previousSlot = targetSlots;
};
}
