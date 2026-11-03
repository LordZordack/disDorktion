#pragma once
#include "BlockDispatcher.h"
#include <array>
#include <vector>

namespace disdorktion
{
struct ResonantCombSettings
{
    float delayMs = 5.f;
    float feedforward = 0.f;
    float feedback = .7f;
    float dampingHz = 6000.f;
    float mix = .5f;
    bool dampingEnabled = true;
    bool bypass = false;
};

inline bool isValidResonantCombSettings(const ResonantCombSettings& s) noexcept
{
    return std::isfinite(s.delayMs) && s.delayMs >= .25f && s.delayMs <= 40.f
        && std::isfinite(s.feedforward) && s.feedforward >= -1.f && s.feedforward <= 1.f
        && std::isfinite(s.feedback) && s.feedback >= -.95f && s.feedback <= .95f
        && std::isfinite(s.dampingHz) && s.dampingHz >= 20.f && s.dampingHz <= 20000.f
        && std::isfinite(s.mix) && s.mix >= 0.f && s.mix <= 1.f;
}

// Exclusive processing ownership is required. Only preparation allocates.
// Histories remain warm during dry mix and bypass; reset snaps retained targets.
class ResonantComb
{
public:
    bool prepare(const juce::dsp::ProcessSpec& spec) noexcept
    {
        if (!isValidProcessSpec(spec)) return false;
        const auto newLength = static_cast<std::size_t>(std::ceil(spec.sampleRate * .040)) + 4;
        std::array<std::vector<double>, 2> replacement;
        try
        {
            for (unsigned c = 0; c < spec.numChannels; ++c) replacement[c].resize(newLength, 0.);
        }
        catch (...) { return false; }
        // All potentially failing operations precede mutation of live state.
        history.swap(replacement);
        dispatcher.prepare(spec);
        sampleRate = spec.sampleRate;
        length = newLength;
        prepared = true;
        delay.reset(sampleRate, .100);
        feedback.reset(sampleRate, .010);
        feedforward.reset(sampleRate, .010);
        damping.reset(sampleRate, .010);
        mix.reset(sampleRate, .010);
        bypass.reset(sampleRate, .010);
        reset();
        return true;
    }

    void reset() noexcept
    {
        for (auto& channel : history) std::fill(channel.begin(), channel.end(), 0.);
        lowPass.fill(0.);
        write = 0;
        delay.setCurrentAndTargetValue(retained.delayMs * sampleRate * .001);
        feedback.setCurrentAndTargetValue(retained.feedback);
        feedforward.setCurrentAndTargetValue(retained.feedforward);
        damping.setCurrentAndTargetValue(dampingCoefficient());
        mix.setCurrentAndTargetValue(retained.mix);
        bypass.setCurrentAndTargetValue(retained.bypass ? 1. : 0.);
    }

    bool setParameters(const ResonantCombSettings& settings) noexcept
    {
        if (!isValidResonantCombSettings(settings)) return false;
        retained = settings;
        delay.setTargetValue(retained.delayMs * sampleRate * .001);
        feedback.setTargetValue(retained.feedback);
        feedforward.setTargetValue(retained.feedforward);
        damping.setTargetValue(dampingCoefficient());
        mix.setTargetValue(retained.mix);
        bypass.setTargetValue(retained.bypass ? 1. : 0.);
        return true;
    }

    bool process(juce::dsp::AudioBlock<float> block) noexcept
    {
        const juce::ScopedNoDenormals noDenormals;
        return dispatcher.process(block, [this](juce::dsp::AudioBlock<float> chunk) noexcept
        {
            for (std::size_t n = 0; n < chunk.getNumSamples(); ++n)
            {
                const double d = delay.getNextValue();
                const auto integer = static_cast<std::size_t>(d);
                const double fraction = d - static_cast<double>(integer);
                const auto newer = (write + length - integer) % length;
                const auto older = newer == 0 ? length - 1 : newer - 1;
                const double g = feedback.getNextValue(), ff = feedforward.getNextValue();
                const double a = damping.getNextValue();
                const double wetWeight = mix.getNextValue() * (1. - bypass.getNextValue());
                for (std::size_t c = 0; c < chunk.getNumChannels(); ++c)
                {
                    auto* samples = chunk.getChannelPointer(c);
                    const float input = samples[n];
                    const double delayed = (1. - fraction) * history[c][newer] + fraction * history[c][older];
                    lowPass[c] = (1. - a) * delayed + a * lowPass[c];
                    const double v = static_cast<double>(input) + g * lowPass[c];
                    history[c][write] = v;
                    const double wet = v + ff * delayed;
                    samples[n] = wetWeight == 0. ? input
                        : static_cast<float>(wetWeight == 1. ? wet : input + wetWeight * (wet - input));
                }
                if (++write == length) write = 0;
            }
        });
    }

    unsigned int getLatencySamples() const noexcept { return 0; }
    double getEffectiveDampingHz() const noexcept { return std::min(static_cast<double>(retained.dampingHz), .45 * sampleRate); }

    // Static retained-control unit-impulse bound, not an arbitrary prior-input
    // tail guarantee. A larger initial state requires corresponding scaling.
    // The positive recurrence majorant is q(1-a)v[n-D]+a*v[n-1].
    // Its real pole rho bounds |v[n]| by rho^n. D=ceil(delay) also
    // majorizes linear interpolation. wet adds at most |ff|*rho^(n-D).
    double getTailSeconds() const noexcept
    {
        if (!prepared) return 0.;
        const double d = std::ceil(retained.delayMs * sampleRate * .001);
        const double q = std::abs(static_cast<double>(retained.feedback));
        if (q == 0.) return retained.feedforward == 0.f ? 0. : (d + 1.) / sampleRate;
        const double a = dampingCoefficient();
        double lower = a, upper = 1.;
        for (int iteration = 0; iteration < 96; ++iteration)
        {
            const double rho = (lower + upper) * .5;
            const double characteristic = q * (1. - a) * std::exp(-d * std::log(rho)) + a / rho;
            if (characteristic > 1.) lower = rho; else upper = rho;
        }
        // Round toward the slower-decaying side, including floating root error.
        const double rho = std::nextafter(upper, 1.);
        const double logRho = std::log(rho);
        const double peak = 1. + std::abs(static_cast<double>(retained.feedforward)) * std::exp(-d * logRho);
        return (std::ceil(std::log(1e-6 / peak) / logRho) + 1.) / sampleRate;
    }

private:
    double dampingCoefficient() const noexcept
    {
        constexpr double twoPi = 6.283185307179586476925286766559;
        return retained.dampingEnabled ? std::exp(-twoPi * getEffectiveDampingHz() / sampleRate) : 0.;
    }
    using Smoother = juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear>;
    BlockDispatcher dispatcher;
    ResonantCombSettings retained;
    std::array<std::vector<double>, 2> history;
    std::array<double, 2> lowPass {};
    Smoother delay {240.}, feedback {.7}, feedforward {0.}, damping {0.}, mix {.5}, bypass {0.};
    double sampleRate = 48000.;
    std::size_t length = 0, write = 0;
    bool prepared = false;
};
}
