#pragma once

namespace disdorktion::benchmark
{
// Projected callbacks in ten seconds. Target 150k observations even when
// sampling every callback cannot supply 100k in the minimum ten-second window.
constexpr double timingWindowSeconds(double projectedTenSeconds) noexcept
{
    const auto seconds = projectedTenSeconds > 0.0 ? 1500000.0 / projectedTenSeconds : 120.0;
    return seconds < 10.0 ? 10.0 : seconds > 120.0 ? 120.0 : seconds;
}
static_assert(timingWindowSeconds(50000.0) == 30.0);
}
