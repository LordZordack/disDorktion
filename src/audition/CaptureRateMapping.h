#pragma once
#include <algorithm>
#include <cmath>

namespace disdorktion::audition
{
inline double captureRateMappingHost(double deviceRate) noexcept
{
    return std::isfinite(deviceRate) && deviceRate >= 8000.0 && deviceRate <= 384000.0
        ? deviceRate : 48000.0;
}

inline double captureRateFromPosition(double position, double deviceRate) noexcept
{
    const auto full = captureRateMappingHost(deviceRate);
    return 100.0 * std::pow(full / 100.0, std::clamp(position, 0.0, 1.0));
}

inline double captureRateToPosition(double requestedHz, double deviceRate) noexcept
{
    const auto full = captureRateMappingHost(deviceRate);
    return std::clamp(std::log(std::clamp(requestedHz, 100.0, full) / 100.0)
        / std::log(full / 100.0), 0.0, 1.0);
}
}
