#pragma once
#include "BlockDispatcher.h"
#include <array>
#include <numbers>
#include <string_view>

namespace disdorktion
{
enum class FilterResponse : unsigned { butterworth, chebyshev1, chebyshev2 };
inline constexpr std::array<std::string_view,3> filterResponseIds {"butterworth", "chebyshev-1", "chebyshev-2"};
struct CutoffSettings
{
    float cutoffHz = 1000.0f;
    bool bypass = false;
    FilterResponse response = FilterResponse::butterworth;
};
inline bool isValidCutoffSettings(const CutoffSettings& value) noexcept
{
    return std::isfinite(value.cutoffHz) && value.cutoffHz >= 20.0f && value.cutoffHz <= 20000.0f
        && static_cast<unsigned>(value.response) < filterResponseIds.size();
}

// Separate template instantiations give HP and LP independent storage and defaults.
// All three banks stay warm, including during full bypass and mode transitions.
template<bool High>
class CutoffFilter
{
public:
    CutoffFilter() noexcept
    {
        retained.cutoffHz = High ? 20.0f : 20000.0f;
        banks[0].damping = std::sqrt(2.0);
        for (unsigned i = 1; i < banks.size(); ++i)
        {
            const double epsilon = i == 1 ? std::sqrt(std::pow(10.0, 0.1) - 1.0) : 1.0 / std::sqrt(9999.0);
            const double mu = std::asinh(1.0 / epsilon) / 2.0;
            const double a = std::sinh(mu) / std::sqrt(2.0);
            const double b = std::cosh(mu) / std::sqrt(2.0);
            const double r = std::hypot(a, b);
            const double w3 = i == 1 ? std::sqrt((1.0 + 1.0 / epsilon) / 2.0)
                                     : std::sqrt(2.0 / (1.0 + 1.0 / epsilon));
            const double lowMultiplier = i == 1 ? r / w3 : 1.0 / (r * w3);
            banks[i].multiplier = High ? 1.0 / lowMultiplier : lowMultiplier;
            banks[i].damping = 2.0 * a / r;
        }
        updateTargets();
        reset();
    }
    bool prepare(const juce::dsp::ProcessSpec& spec) noexcept
    {
        if (!dispatcher.prepare(spec)) return false;
        sampleRate = spec.sampleRate;
        rampFrames = static_cast<unsigned>(sampleRate * 0.010);
        for (auto& bank : banks) bank.g.reset(sampleRate, 0.010);
        bypass.reset(sampleRate, 0.010);
        updateTargets();
        reset();
        return true;
    }
    void reset() noexcept
    {
        for (auto& bank : banks)
        {
            bank.s1.fill(0.0); bank.s2.fill(0.0);
            bank.g.setCurrentAndTargetValue(bank.target);
        }
        bypass.setCurrentAndTargetValue(retained.bypass ? 1.0 : 0.0);
        weights.fill(0.0); weights[static_cast<unsigned>(retained.response)] = 1.0;
        startWeights = weights; modeRemaining = 0;
    }
    bool setParameters(const CutoffSettings& value) noexcept
    {
        if (!isValidCutoffSettings(value)) return false;
        const bool cutoffChanged = value.cutoffHz != retained.cutoffHz;
        const bool modeChanged = value.response != retained.response;
        retained = value;
        if (cutoffChanged) updateTargets();
        if (modeChanged) { startWeights = weights; modeRemaining = rampFrames; }
        bypass.setTargetValue(retained.bypass ? 1.0 : 0.0);
        return true;
    }
    double getEffectiveCutoffHz() const noexcept { return std::min(static_cast<double>(retained.cutoffHz), sampleRate * 0.45); }
    unsigned getLatencySamples() const noexcept { return 0; }
    bool process(juce::dsp::AudioBlock<float> block) noexcept
    {
        const juce::ScopedNoDenormals noDenormals;
        return dispatcher.process(block, [this](auto chunk) noexcept
        {
            for (std::size_t frame = 0; frame < chunk.getNumSamples(); ++frame)
            {
                if (modeRemaining != 0)
                {
                    --modeRemaining;
                    const double progress = 1.0 - static_cast<double>(modeRemaining) / rampFrames;
                    for (unsigned i = 0; i < weights.size(); ++i)
                        weights[i] = modeRemaining == 0 ? (i == static_cast<unsigned>(retained.response) ? 1.0 : 0.0)
                            : startWeights[i] + progress * ((i == static_cast<unsigned>(retained.response) ? 1.0 : 0.0) - startWeights[i]);
                }
                std::array<double,3> gs, hs;
                for (unsigned i = 0; i < banks.size(); ++i)
                {
                    gs[i] = banks[i].g.getNextValue();
                    hs[i] = 1.0 / (1.0 + gs[i] * (gs[i] + banks[i].damping));
                }
                const double blend = bypass.getNextValue();
                for (std::size_t channel = 0; channel < chunk.getNumChannels(); ++channel)
                {
                    auto& sample = chunk.getChannelPointer(channel)[frame];
                    const double dry = sample;
                    double wet = 0.0;
                    for (unsigned i = 0; i < banks.size(); ++i)
                    {
                        auto& bank = banks[i];
                        const double v1 = hs[i] * (bank.s1[channel] + gs[i] * (dry - bank.s2[channel]));
                        const double v2 = bank.s2[channel] + gs[i] * v1;
                        bank.s1[channel] = 2.0 * v1 - bank.s1[channel];
                        bank.s2[channel] = 2.0 * v2 - bank.s2[channel];
                        const double high = dry - bank.damping * v1 - v2;
                        double output = High ? high : v2;
                        if (i == 1) output *= 0.8912509381337456; // 1 dB even-order passband endpoint.
                        if (i == 2) output += 0.01 * (High ? v2 : high); // Finite inverse-Chebyshev zero.
                        wet += weights[i] * output;
                    }
                    if (blend != 1.0) sample = static_cast<float>(blend == 0.0 ? wet : blend * dry + (1.0 - blend) * wet);
                }
            }
        });
    }
private:
    struct Bank
    {
        double multiplier = 1.0, damping = 0.0, target = 1.0;
        juce::SmoothedValue<double,juce::ValueSmoothingTypes::Multiplicative> g {1.0};
        std::array<double,2> s1 {}, s2 {};
    };
    void updateTargets() noexcept
    {
        const double base = std::tan(std::numbers::pi * getEffectiveCutoffHz() / sampleRate);
        for (auto& bank : banks) { bank.target = base * bank.multiplier; bank.g.setTargetValue(bank.target); }
    }
    BlockDispatcher dispatcher;
    CutoffSettings retained;
    std::array<Bank,3> banks;
    std::array<double,3> weights {}, startWeights {};
    juce::SmoothedValue<double,juce::ValueSmoothingTypes::Linear> bypass {0.0};
    double sampleRate = 48000.0;
    unsigned rampFrames = 480, modeRemaining = 0;
};
}
