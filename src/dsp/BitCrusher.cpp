#include "BitCrusher.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace disdorktion
{
bool BitCrusher::prepare(const juce::dsp::ProcessSpec& spec) noexcept
{
    if (!dispatcher.prepare(spec)) return false;
    hostRate = spec.sampleRate;
    rate.reset(hostRate, 0.010);
    bypass.reset(hostRate, 0.010);
    mix.reset(hostRate, 0.010); crush.reset(hostRate, 0.010);
    shape.reset(hostRate, 0.010); jitter.reset(hostRate, 0.010);
    reset();
    return true;
}
void BitCrusher::updateQuantizer() noexcept
{
    activeDepth = retained.bitDepth;
    scale = std::ldexp(1.0, static_cast<int>(activeDepth) - 1);
    inverseScale = 1.0 / scale;
    if (activeDepth == 64)
    {
        minimumCode = std::numeric_limits<std::int64_t>::min();
        maximumCode = std::numeric_limits<std::int64_t>::max();
    }
    else
    {
        const auto half = std::int64_t {1} << (activeDepth - 1);
        minimumCode = -half;
        maximumCode = half - 1;
    }
}
std::int64_t BitCrusher::encode(float sample) const noexcept
{
    // Full-scale branches prevent the 64-bit +2^63 conversion overflow.
    if (sample <= -1.0f) return minimumCode;
    if (sample >= 1.0f) return maximumCode;
    const auto rounded = std::round(static_cast<double>(sample) * scale);
    // For depth 64, float input strictly inside (-1,1) is at least 2^39
    // integer codes from the overflow boundary. Small depths still need clamp.
    if (activeDepth != 64 && rounded > static_cast<double>(maximumCode)) return maximumCode;
    return static_cast<std::int64_t>(rounded);
}
void BitCrusher::reset() noexcept
{
    heldRaw.fill(0.0f); heldWet.fill(0.0f); heldCode.fill(0);
    phase = phaseError = 0.0;
    firstFrame = true;
    updateQuantizer();
    rate.setCurrentAndTargetValue(std::min(hostRate, static_cast<double>(retained.captureRateHz)));
    bypass.setCurrentAndTargetValue(retained.bypass ? 1.0 : 0.0);
    mix.setCurrentAndTargetValue(retained.mix); crush.setCurrentAndTargetValue(retained.crush);
    shape.setCurrentAndTargetValue(retained.shape); jitter.setCurrentAndTargetValue(retained.jitter);
    previousCrush = retained.crush; previousShape = retained.shape;
    enhancedStep = inverseScale + (1.0 - inverseScale) * previousCrush * previousCrush * previousCrush;
    exponent = std::exp2(2.0 * previousShape); inverseExponent = 1.0 / exponent;
    activeSeed = retained.jitterSeed; randomState = activeSeed ? activeSeed : 0x6d2b79f5u;
    clockRandom = 0.0;
}
double BitCrusher::nextJitter() noexcept
{
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return (static_cast<double>(randomState) + 0.5) / 2147483648.0 - 1.0;
}
bool BitCrusher::setParameters(const BitCrusherSettings& settings) noexcept
{
    if (!isValidBitCrusherSettings(settings)) return false;
    retained = settings;
    rate.setTargetValue(std::min(hostRate, static_cast<double>(retained.captureRateHz)));
    bypass.setTargetValue(retained.bypass ? 1.0 : 0.0);
    mix.setTargetValue(retained.mix); crush.setTargetValue(retained.crush);
    shape.setTargetValue(retained.shape); jitter.setTargetValue(retained.jitter);
    return true;
}
bool BitCrusher::process(juce::dsp::AudioBlock<float> block) noexcept
{
    return dispatcher.process(block, [this](auto chunk) noexcept
    {
        bool depthChanged = activeDepth != retained.bitDepth;
        if (depthChanged) updateQuantizer();
        for (std::size_t n = 0; n < chunk.getNumSamples(); ++n)
        {
            const auto captureRate = rate.getNextValue();
            const auto blend = bypass.getNextValue();
            const auto wetMix = mix.getNextValue();
            const auto crushAmount = crush.getNextValue();
            const auto shapeAmount = shape.getNextValue();
            const auto jitterAmount = jitter.getNextValue();
            if (activeSeed != retained.jitterSeed)
            {
                activeSeed = retained.jitterSeed;
                randomState = activeSeed ? activeSeed : 0x6d2b79f5u;
                clockRandom = jitterAmount > 0.0 && captureRate < hostRate ? nextJitter() : 0.0;
            }
            const bool quantizerChanged = depthChanged || crushAmount != previousCrush || shapeAmount != previousShape;
            if (quantizerChanged)
            {
                enhancedStep = inverseScale + (1.0 - inverseScale) * crushAmount * crushAmount * crushAmount;
                if (shapeAmount != previousShape)
                { exponent = std::exp2(2.0 * shapeAmount); inverseExponent = 1.0 / exponent; }
                previousCrush = crushAmount; previousShape = shapeAmount;
            }
            const auto threshold = hostRate + clockRandom * 0.9 * jitterAmount * (hostRate - captureRate);
            bool capture = firstFrame;
            if (!firstFrame)
            {
                // Bounded compensated sum in Hz avoids rounding the hold period
                // and keeps integer rates/divisors exact across block boundaries.
                const auto increment = captureRate - phaseError;
                const auto sum = phase + increment;
                phaseError = (sum - phase) - increment;
                phase = sum;
                const auto tolerance = 8.0 * std::numeric_limits<double>::epsilon() * hostRate;
                capture = phase >= threshold - tolerance;
                if (capture)
                {
                    phase -= threshold;
                    if (phase < 0.0) { phase = 0.0; phaseError = 0.0; }
                }
            }
            firstFrame = false;
            if (capture)
                clockRandom = jitterAmount > 0.0 && captureRate < hostRate ? nextJitter() : 0.0;
            const auto enhancement = std::max(crushAmount, std::abs(shapeAmount));
            for (std::size_t c = 0; c < chunk.getNumChannels(); ++c)
            {
                auto& sample = chunk.getChannelPointer(c)[n];
                const auto dry = sample;
                if (capture) heldRaw[c] = dry;
                if (capture || quantizerChanged)
                {
                    heldCode[c] = encode(heldRaw[c]);
                    heldWet[c] = static_cast<float>(static_cast<double>(heldCode[c]) * inverseScale);
                    if (enhancement > 0.0)
                    {
                        const juce::ScopedNoDenormals noDenormals;
                        const auto raw = std::clamp(static_cast<double>(heldRaw[c]), -1.0, 1.0);
                        const auto shaped = shapeAmount == 0.0 || raw == 0.0 ? raw
                            : std::copysign(std::pow(std::abs(raw), exponent), raw);
                        const auto quantized = std::clamp(std::round(shaped / enhancedStep) * enhancedStep, -1.0, 1.0);
                        const auto decoded = shapeAmount == 0.0 || quantized == 0.0 ? quantized
                            : std::copysign(std::pow(std::abs(quantized), inverseExponent), quantized);
                        heldWet[c] = static_cast<float>((1.0 - enhancement) * heldWet[c] + enhancement * decoded);
                    }
                }
                const auto wetWeight = (1.0 - blend) * wetMix;
                if (wetWeight == 0.0) sample = dry;
                else if (wetWeight == 1.0) sample = heldWet[c];
                else if (wetMix == 1.0)
                    sample = static_cast<float>((1.0 - blend) * heldWet[c] + blend * static_cast<double>(dry));
                else sample = static_cast<float>(wetWeight * heldWet[c] + (1.0 - wetWeight) * static_cast<double>(dry));
            }
            depthChanged = false;
        }
    });
}
}
