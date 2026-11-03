#pragma once
#include "BlockDispatcher.h"
#include <array>

namespace disdorktion
{
struct ToneSettings
{
    float toneDb = 0.f;
    bool bypass = false;
};
inline bool isValidToneSettings(const ToneSettings& settings) noexcept
{
    return std::isfinite(settings.toneDb) && settings.toneDb >= -6.f && settings.toneDb <= 6.f;
}

// Fixed 1 kHz reciprocal shelf. Exclusive processing ownership is required.
// The lowpass state update is convex throughout the host domain, even while
// controls move. For input bound X, |state|, |low|<=X and |wet|<3.49X.
class Tone
{
public:
    bool prepare(const juce::dsp::ProcessSpec& spec) noexcept
    {
        if (!isValidProcessSpec(spec)) return false;
        dispatcher.prepare(spec);
        k = std::tan(3.14159265358979323846 * 1000. / spec.sampleRate);
        tone.reset(spec.sampleRate, .010);
        bypass.reset(spec.sampleRate, .010);
        reset();
        return true;
    }
    void reset() noexcept
    {
        history.fill(0.);
        tone.setCurrentAndTargetValue(retained.toneDb);
        bypass.setCurrentAndTargetValue(retained.bypass ? 1. : 0.);
        update(retained.toneDb);
    }
    bool setParameters(const ToneSettings& settings) noexcept
    {
        if (!isValidToneSettings(settings)) return false;
        retained = settings;
        tone.setTargetValue(settings.toneDb);
        bypass.setTargetValue(settings.bypass ? 1. : 0.);
        return true;
    }
    bool process(juce::dsp::AudioBlock<float> block) noexcept
    {
        const juce::ScopedNoDenormals noDenormals;
        return dispatcher.process(block, [this](juce::dsp::AudioBlock<float> chunk) noexcept
        {
            for (std::size_t n = 0; n < chunk.getNumSamples(); ++n)
            {
                const auto db = tone.getNextValue();
                if (db != cachedDb) update(db);
                const auto blend = 1. - bypass.getNextValue();
                for (std::size_t c = 0; c < chunk.getNumChannels(); ++c)
                {
                    auto* samples = chunk.getChannelPointer(c);
                    const float dry = samples[n];
                    const auto v = q * (double(dry) - history[c]);
                    const auto low = v + history[c];
                    history[c] = low + v;
                    const auto wet = g * dry + difference * low;
                    samples[n] = db == 0. || blend == 0. ? dry
                        : static_cast<float>(blend == 1. ? wet : dry + blend * (wet - dry));
                }
            }
        });
    }
    unsigned getLatencySamples() const noexcept { return 0; }
private:
    void update(double db) noexcept
    {
        cachedDb = db;
        g = std::pow(10., db / 20.);
        q = g * k / (1. + g * k);
        difference = 1. / g - g;
    }
    using Smoother = juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear>;
    BlockDispatcher dispatcher;
    ToneSettings retained;
    std::array<double, 2> history{};
    Smoother tone{0.}, bypass{0.};
    double k = 0.06554346281523822, cachedDb = 0., g = 1., q = 0.06151176850362156, difference = 0.;
};
}
