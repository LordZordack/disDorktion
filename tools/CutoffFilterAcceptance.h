#pragma once
#include <cmath>
namespace disdorktion::measurement
{
inline bool cutoffCpuWithinBudget(double high,double low,unsigned completedCases) noexcept
{
    return completedCases==288 && std::isfinite(high) && std::isfinite(low)
        && high>=0. && low>=0. && high<.05 && low<.05;
}
}
