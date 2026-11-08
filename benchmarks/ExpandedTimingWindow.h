#pragma once
#include <cstdint>
#include <cstddef>

namespace disdorktion::benchmark
{
// Full-chain callbacks are substantially more expensive than isolated modules.
// Keep the established sample-count gate and extend the ceiling, rather than
// allowing a short distribution to qualify larger/oversized incoming blocks.
constexpr double expandedTimingWindowSeconds(double callbacksPerSecond) noexcept
{
    const double seconds = callbacksPerSecond > 0. ? 150000. / callbacksPerSecond : 1200.;
    return seconds < 10. ? 10. : seconds > 1200. ? 1200. : seconds;
}
static_assert(expandedTimingWindowSeconds(100000.) == 10.);
static_assert(expandedTimingWindowSeconds(300.) == 500.);
static_assert(expandedTimingWindowSeconds(1.) == 1200.);

class ExpandedWallTimeSampler
{
public:
    explicit ExpandedWallTimeSampler(std::int64_t duration) noexcept : window(duration) {}
    bool shouldSample(std::int64_t elapsed) noexcept
    {
        if(window <= 0 || window > 1200000000000LL || elapsed < 0 || elapsed >= window) return false;
        // <=1200s *150000 slots fits in signed64-bit arithmetic.
        const auto slot = std::size_t(elapsed * 150000LL / window);
        if(previous != 150000 && slot <= previous) return false;
        previous=slot;return true;
    }
private:
    std::int64_t window;
    std::size_t previous=150000;
};
}
