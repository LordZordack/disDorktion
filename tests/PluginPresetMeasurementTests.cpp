#include <catch2/catch_test_macros.hpp>
#include "plugin/PluginProcessor.h"
#include "plugin/FactoryPresets.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>

namespace
{
constexpr double measurementPi = 3.14159265358979323846;
enum class Fixture { sine, twoTone, transient };

const char* fixtureName(Fixture fixture)
{
    switch (fixture)
    {
        case Fixture::sine: return "sine997";
        case Fixture::twoTone: return "two-tone997-1499";
        case Fixture::transient: return "repeated-transient";
    }
    return "unknown";
}

float fixtureSample(Fixture fixture, std::uint64_t frame, double rate)
{
    const auto time = static_cast<double>(frame) / rate;
    const auto sine = std::sin(2.0 * measurementPi * 997.0 * time);
    if (fixture == Fixture::sine) return static_cast<float>(0.25 * sine);
    if (fixture == Fixture::twoTone)
        return static_cast<float>(0.125 * (sine + std::sin(2.0 * measurementPi * 1499.0 * time)));

    // Repeat a bounded 15 ms damped burst every 50 ms. Integer hashing gives
    // deterministic noise without sharing RNG or DSP state with the processor.
    const auto period = static_cast<std::uint64_t>(std::llround(rate * 0.05));
    const auto localFrame = frame % period;
    const auto localTime = static_cast<double>(localFrame) / rate;
    if (localFrame == 0) return 0.5f;
    if (localTime >= 0.015) return 0;
    auto hash = static_cast<std::uint32_t>(frame) + 0x9e3779b9u;
    hash ^= hash >> 16; hash *= 0x7feb352du;
    hash ^= hash >> 15; hash *= 0x846ca68bu; hash ^= hash >> 16;
    const auto noise = 2.0 * static_cast<double>(hash) / static_cast<double>(std::numeric_limits<std::uint32_t>::max()) - 1.0;
    const auto tone = std::sin(2.0 * measurementPi * 997.0 * localTime);
    return static_cast<float>(0.5 * std::exp(-localTime / 0.003) * (0.8 * tone + 0.2 * noise));
}

struct Measurement
{
    double peak = 0, squared = 0, maxDelta = 0;
    std::uint64_t samples = 0;
    bool finite = true;
    double rms() const { return samples == 0 ? 0 : std::sqrt(squared / static_cast<double>(samples)); }
};

Measurement renderMeasurement(disdorktion::PluginProcessor& plugin, Fixture fixture,
                              double rate, int channels, std::uint64_t& offset,
                              int frames, std::array<float, 2>& previous)
{
    Measurement result;
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> audio(channels, 127);
    while (frames > 0)
    {
        const auto count = std::min(frames, 127);
        audio.setSize(channels, count, false, false, true);
        for (int channel = 0; channel < channels; ++channel)
            for (int frame = 0; frame < count; ++frame)
                audio.setSample(channel, frame, fixtureSample(fixture, offset + static_cast<std::uint64_t>(frame), rate));
        plugin.processBlock(audio, midi);
        for (int channel = 0; channel < channels; ++channel)
            for (int frame = 0; frame < count; ++frame)
            {
                const auto value = audio.getSample(channel, frame);
                result.finite = result.finite && std::isfinite(value);
                result.peak = std::max(result.peak, std::abs(static_cast<double>(value)));
                result.squared += static_cast<double>(value) * value;
                result.maxDelta = std::max(result.maxDelta,
                    std::abs(static_cast<double>(value) - previous[static_cast<std::size_t>(channel)]));
                previous[static_cast<std::size_t>(channel)] = value;
                ++result.samples;
            }
        frames -= count;
        offset += static_cast<std::uint64_t>(count);
    }
    return result;
}

void prepareMeasurement(disdorktion::PluginProcessor& plugin, double rate, int channels, int preset)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
    layout.outputBuses.add(layout.inputBuses[0]);
    REQUIRE(plugin.setBusesLayout(layout));
    REQUIRE(plugin.loadFactoryPreset(preset));
    plugin.setNonRealtime(true);
    plugin.prepareToPlay(rate, 127);
}

void recordMeasurement(std::ofstream& csv, double rate, int channels, Fixture fixture,
                       int from, int to, const char* phase, const Measurement& measurement)
{
    CAPTURE(rate, channels, fixtureName(fixture), from, to, phase,
            measurement.peak, measurement.rms(), measurement.maxDelta);
    REQUIRE(measurement.finite);
    REQUIRE(std::isfinite(measurement.peak));
    REQUIRE(std::isfinite(measurement.rms()));
    REQUIRE(std::isfinite(measurement.maxDelta));
    REQUIRE(measurement.samples > 0);
    if (csv.is_open())
    {
        csv << rate << ',' << channels << ',' << fixtureName(fixture) << ','
            << disdorktion::factoryPresets[static_cast<std::size_t>(from)].key << ','
            << disdorktion::factoryPresets[static_cast<std::size_t>(to)].key << ','
            << phase << ',' << measurement.peak << ',' << measurement.rms() << ','
            << measurement.maxDelta << '\n';
        REQUIRE(csv.good());
    }
}
}

TEST_CASE("Factory preset settled and transition measurements are finite", "[adapter][preset][measurement]")
{
    std::ofstream csv;
    const auto reportPath = juce::SystemStats::getEnvironmentVariable("DISDORKTION_PRESET_REPORT", {});
    if (reportPath.isNotEmpty())
    {
        csv.open(reportPath.toStdString(), std::ios::out | std::ios::trunc);
        REQUIRE(csv.is_open());
        csv << std::setprecision(17);
        csv << "rate,channels,fixture,from,to,phase,peak,rms,max_delta\n";
    }
    for (const double rate : {48000.0, 96000.0})
        for (const int channels : {1, 2})
        {
            const auto warmFrames = static_cast<int>(std::llround(0.1 * rate));
            const auto measureFrames = static_cast<int>(std::llround(0.25 * rate));
            for (int preset = 0; preset < 5; ++preset)
                for (const auto fixture : {Fixture::sine, Fixture::twoTone, Fixture::transient})
                {
                    disdorktion::PluginProcessor plugin;
                    prepareMeasurement(plugin, rate, channels, preset);
                    std::uint64_t offset = 0;
                    std::array<float, 2> previous {};
                    const auto warm = renderMeasurement(plugin, fixture, rate, channels, offset, warmFrames, previous);
                    REQUIRE(warm.finite);
                    const auto settled = renderMeasurement(plugin, fixture, rate, channels, offset, measureFrames, previous);
                    recordMeasurement(csv, rate, channels, fixture, preset, preset, "settled", settled);
                }
            for (int from = 0; from < 5; ++from)
                for (int to = 0; to < 5; ++to)
                {
                    disdorktion::PluginProcessor plugin;
                    prepareMeasurement(plugin, rate, channels, from);
                    std::uint64_t offset = 0;
                    std::array<float, 2> previous {};
                    const auto warm = renderMeasurement(plugin, Fixture::sine, rate, channels, offset, warmFrames, previous);
                    REQUIRE(warm.finite);
                    REQUIRE(plugin.loadFactoryPreset(to));
                    const auto transition = renderMeasurement(plugin, Fixture::sine, rate, channels, offset, measureFrames, previous);
                    recordMeasurement(csv, rate, channels, Fixture::sine, from, to, "transition", transition);
                }
        }
}
