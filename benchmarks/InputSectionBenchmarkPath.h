#pragma once
#include "dsp/InputSection.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace disdorktion::benchmark
{
inline InputSettings inputAutomationSettings(std::uint64_t iteration) noexcept
{
    const auto residue=iteration%101;
    const auto depth=(iteration/101)%bitCrusherDepths.size();
    const auto bypass=(iteration/(101*bitCrusherDepths.size()))%4;
    const float position=static_cast<float>(residue)/100;
    return {{-60+84*position, false, (bypass&1)!=0},
        {bitCrusherDepths[depth],100+383900*position,(bypass&2)!=0,
         position,1-position,2*position-1,position,123}};
}
inline InputSettings inputExpressiveSettings(unsigned hostRate) noexcept
{
    // Interior stimulus after +24 dB keeps nonlinear powers active; hostRate-1
    // runs the jitter generator on nearly every captured frame.
    return {{24,false,false},{64,static_cast<float>(hostRate-1),false,1,1,.8f,1,123}};
}
inline std::size_t inputBypassState(std::uint64_t iteration) noexcept
{
    return static_cast<std::size_t>((iteration/8)%4);
}
inline InputSettings inputBypassSettings(std::uint64_t iteration,unsigned hostRate) noexcept
{
    auto settings=inputExpressiveSettings(hostRate);
    const auto state=inputBypassState(iteration);
    settings.gain.bypass=(state&1)!=0;
    settings.crusher.bypass=(state&2)!=0;
    return settings;
}
class InputBypassCoverage
{
public:
    void record(std::uint64_t iteration) noexcept {++counts[inputBypassState(iteration)];}
    std::size_t coveredCount() const noexcept
    {return static_cast<std::size_t>(std::count_if(counts.begin(),counts.end(),[](auto n){return n!=0;}));}
    std::size_t minimum() const noexcept {return *std::min_element(counts.begin(),counts.end());}
    bool qualifies() const noexcept {return minimum()>=100;}
private:
    std::array<std::size_t,4> counts {};
};
// Counts timed observations, including the joint depth/bypass combinations;
// the separate 101-residue collector covers continuously automated controls.
class InputInteractionCoverage
{
public:
    void record(std::uint64_t iteration) noexcept
    {
        const auto depth=(iteration/101)%bitCrusherDepths.size();
        const auto bypass=(iteration/(101*bitCrusherDepths.size()))%4;
        ++counts[depth*4+bypass];
    }
    std::size_t coveredCount() const noexcept
    {return static_cast<std::size_t>(std::count_if(counts.begin(),counts.end(),[](auto n){return n!=0;}));}
    std::size_t minimum() const noexcept {return *std::min_element(counts.begin(),counts.end());}
    bool qualifies() const noexcept {return minimum()>=100;}
private:
    std::array<std::size_t,44> counts {};
};
}
