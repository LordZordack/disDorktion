#pragma once
#include <array>
#include <cmath>

namespace disdorktion
{
// Fixed offline RMS calibration; never analyze the live signal for gain control.
// Reproduce with disdorktion_driver_calibration (four factory Waveshapers).
inline constexpr std::array<float,5> driverKnots {1.f,1.5f,2.f,3.f,4.f};
inline constexpr std::array<float,5> driverCorrectionDb {
    0.f,-1.949236203330511f,-2.790267761527883f,-3.598405372768794f,-3.976905193158446f};

inline float driverCompensationDb(float driver) noexcept
{
    if (driver <= 1.f) return 0.f;
    if (driver >= 4.f) return driverCorrectionDb.back();
    for (std::size_t i=1;i<driverKnots.size();++i)
        if (driver <= driverKnots[i])
        {
            if (driver == driverKnots[i]) return driverCorrectionDb[i];
            const auto fraction=std::log(driver/driverKnots[i-1])/std::log(driverKnots[i]/driverKnots[i-1]);
            return driverCorrectionDb[i-1]+fraction*(driverCorrectionDb[i]-driverCorrectionDb[i-1]);
        }
    return driverCorrectionDb.back();
}
inline float driverCompensationGain(float driver) noexcept
{
    return driver == 1.f ? 1.f : std::pow(10.f,driverCompensationDb(driver)*.05f);
}
}
