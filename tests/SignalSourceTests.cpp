#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "harness/SignalSource.h"
#include <cmath>
#include <limits>
#include <vector>

using namespace disdorktion::harness;
namespace
{
std::vector<float> generate(SourceSettings settings, const std::vector<int>& partitions)
{
    SignalSource source;
    REQUIRE(source.prepare(settings, 48000.0, 2));
    std::vector<float> result;
    for (auto count : partitions)
    {
        juce::AudioBuffer<float> samples(2, count);
        source.process(juce::dsp::AudioBlock<float>(samples));
        for (int n = 0; n < count; ++n)
        {
            REQUIRE(samples.getSample(0, n) == samples.getSample(1, n));
            result.push_back(samples.getSample(0, n));
        }
    }
    return result;
}
}

TEST_CASE("Generated sources match mathematical sample oracles", "[harness][source]")
{
    SourceSettings settings;
    settings.durationSamples = 8;
    settings.frequency = 3000.0; settings.frequency2 = 6000.0;
    settings.amplitude = 0.8; settings.phase = 0.2;
    for (const auto kind : {SourceKind::impulse, SourceKind::sine, SourceKind::twoTone, SourceKind::sweep, SourceKind::noise})
    {
        settings.kind = kind;
        const auto values = generate(settings, {10});
        std::uint32_t state = settings.seed;
        for (std::size_t n = 0; n < 8; ++n)
        {
            const auto tau = juce::MathConstants<double>::twoPi;
            double expected = 0;
            switch (kind)
            {
                case SourceKind::impulse: expected = n == 0 ? settings.amplitude : 0.0; break;
                case SourceKind::sine: expected = settings.amplitude * std::sin(settings.phase + tau * settings.frequency * n / 48000.0); break;
                case SourceKind::twoTone:
                    expected = settings.amplitude * 0.5 * (std::sin(settings.phase + tau * settings.frequency * n / 48000.0)
                             + std::sin(settings.phase + tau * settings.frequency2 * n / 48000.0)); break;
                case SourceKind::sweep:
                {
                    const auto k = std::log(2.0);
                    expected = settings.amplitude * std::sin(settings.phase + tau * settings.frequency * 7.0 / 48000.0
                               * (std::pow(2.0, static_cast<double>(n) / 7.0) - 1.0) / k); break;
                }
                case SourceKind::noise:
                    state = state * 1664525u + 1013904223u;
                    expected = settings.amplitude * (static_cast<double>(state >> 8) / 8388608.0 - 1.0); break;
                default: break;
            }
            REQUIRE(values[n] == Catch::Approx(expected).margin(1.0e-6));
            REQUIRE(std::abs(values[n]) <= settings.amplitude + 1.0e-6);
        }
        REQUIRE(values[8] == 0.0f); REQUIRE(values[9] == 0.0f);
    }
}

TEST_CASE("Sources preserve arbitrary partitions and reset", "[harness][source]")
{
    SourceSettings settings; settings.durationSamples = 211; settings.seed = 0;
    for (const auto kind : {SourceKind::impulse, SourceKind::sine, SourceKind::twoTone, SourceKind::sweep, SourceKind::noise})
    {
        settings.kind = kind;
        REQUIRE(generate(settings, {263}) == generate(settings, {0, 1, 7, 31, 0, 127, 97}));
        SignalSource source; REQUIRE(source.prepare(settings, 48000.0, 1));
        juce::AudioBuffer<float> first(1, 100), repeated(1, 100);
        source.process(juce::dsp::AudioBlock<float>(first));
        source.reset(); source.process(juce::dsp::AudioBlock<float>(repeated));
        for (int n = 0; n < 100; ++n) REQUIRE(first.getSample(0, n) == repeated.getSample(0, n));
    }
}

TEST_CASE("Generated source validation rejects malformed values without replacing state", "[harness][source]")
{
    SourceSettings settings; juce::String error;
    REQUIRE(SignalSource::validate(settings, 48000, error));
    for (const auto rate : {0.0, 7999.0, 384001.0, std::numeric_limits<double>::infinity()})
        REQUIRE_FALSE(SignalSource::validate(settings, rate, error));
    auto invalid = settings; invalid.durationSamples = 0;
    REQUIRE_FALSE(SignalSource::validate(invalid, 48000, error));
    invalid = settings; invalid.durationSamples = 9007199254740992ULL;
    REQUIRE_FALSE(SignalSource::validate(invalid, 48000, error));
    for (const auto frequency : {0.0, -1.0, 24000.0, std::numeric_limits<double>::quiet_NaN()})
    {
        invalid = settings; invalid.frequency = frequency;
        REQUIRE_FALSE(SignalSource::validate(invalid, 48000, error));
    }
    invalid = settings; invalid.kind = SourceKind::sweep; invalid.frequency2 = 220.0;
    REQUIRE_FALSE(SignalSource::validate(invalid, 48000, error));
    invalid = settings; invalid.kind = SourceKind::file;
    REQUIRE_FALSE(SignalSource::validate(invalid, 48000, error));
    SignalSource source; settings.kind = SourceKind::impulse;
    REQUIRE(source.prepare(settings, 48000, 1));
    REQUIRE_FALSE(source.prepare(invalid, 48000, 1));
    juce::AudioBuffer<float> one(1, 1); source.process(juce::dsp::AudioBlock<float>(one));
    REQUIRE(one.getSample(0, 0) == 0.25f);
    settings.kind = SourceKind::sweep; settings.frequency = std::numeric_limits<double>::denorm_min();
    REQUIRE(source.prepare(settings, 48000, 1));
    juce::AudioBuffer<float> extreme(1, 10); source.process(juce::dsp::AudioBlock<float>(extreme));
    for (int n = 0; n < 10; ++n) REQUIRE(std::isfinite(extreme.getSample(0, n)));
}
