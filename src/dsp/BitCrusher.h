#pragma once
#include "BlockDispatcher.h"
#include <array>
#include <cstdint>
#include <cmath>

namespace disdorktion
{
inline constexpr std::array<unsigned, 11> bitCrusherDepths {1,2,3,4,5,6,7,8,16,32,64};
inline constexpr bool isValidBitDepth(unsigned value) noexcept
{
    for (const auto depth : bitCrusherDepths) if (value == depth) return true;
    return false;
}
struct BitCrusherSettings
{
    unsigned bitDepth = 64;
    float captureRateHz = 384000.0f;
    bool bypass = false;
    float mix = 1.0f;
    float crush = 0.0f;
    float shape = 0.0f;
    float jitter = 0.0f;
    std::uint32_t jitterSeed = 1;
};
inline bool isValidBitCrusherSettings(const BitCrusherSettings& s) noexcept
{
    return isValidBitDepth(s.bitDepth) && std::isfinite(s.captureRateHz)
        && s.captureRateHz >= 100.0f && s.captureRateHz <= 384000.0f
        && std::isfinite(s.mix) && s.mix >= 0.0f && s.mix <= 1.0f
        && std::isfinite(s.crush) && s.crush >= 0.0f && s.crush <= 1.0f
        && std::isfinite(s.shape) && s.shape >= -1.0f && s.shape <= 1.0f
        && std::isfinite(s.jitter) && s.jitter >= 0.0f && s.jitter <= 1.0f;
}
class BitCrusher
{
public:
    bool prepare(const juce::dsp::ProcessSpec&) noexcept;
    void reset() noexcept;
    bool setParameters(const BitCrusherSettings&) noexcept;
    bool process(juce::dsp::AudioBlock<float>) noexcept;
    unsigned getLatencySamples() const noexcept { return 0; }
private:
    void updateQuantizer() noexcept;
    std::int64_t encode(float sample) const noexcept;
    double nextJitter() noexcept;
    BlockDispatcher dispatcher;
    BitCrusherSettings retained;
    double hostRate = 48000.0, phase = 0.0, phaseError = 0.0;
    double scale = 9223372036854775808.0, inverseScale = 1.0 / 9223372036854775808.0;
    std::int64_t minimumCode = INT64_MIN, maximumCode = INT64_MAX;
    std::array<float, 2> heldRaw {}, heldWet {};
    std::array<std::int64_t, 2> heldCode {};
    unsigned activeDepth = 64;
    bool firstFrame = true;
    std::uint32_t randomState = 1, activeSeed = 1;
    double clockRandom = 0.0, previousCrush = 0.0, previousShape = 0.0;
    double enhancedStep = 1.0 / 9223372036854775808.0, exponent = 1.0, inverseExponent = 1.0;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> rate {384000.0}, bypass {0.0};
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> mix {1.0}, crush {0.0}, shape {0.0}, jitter {0.0};
};
}
