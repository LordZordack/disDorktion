#include "dsp/MiddleSection.h"
#include "plugin/FactoryPresets.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
constexpr std::array<double, 5> knots {1.0, 1.5, 2.0, 3.0, 4.0};
constexpr std::array<double, 3> levels {-24.0, -18.0, -12.0};
constexpr std::array<double, 2> rates {48000.0, 96000.0};
constexpr int blockSize = 256;
constexpr std::uint32_t noiseSeed = 0x51a7c0deu;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

juce::var object() { return juce::var(new juce::DynamicObject()); }
void set(juce::var& value, const char* name, const juce::var& field)
{
    value.getDynamicObject()->setProperty(name, field);
}
double median(std::vector<double> values)
{
    require(!values.empty(), "Empty median");
    std::sort(values.begin(), values.end());
    const auto centre = values.size() / 2;
    return values.size() % 2 ? values[centre] : (values[centre - 1] + values[centre]) * 0.5;
}

std::vector<float> fixture(int kind, double rate, double levelDb)
{
    std::vector<float> samples(static_cast<std::size_t>(rate * 2.0));
    std::uint32_t state = noiseSeed;
    double energy = 0.0;
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * static_cast<double>(i) / rate;
        double sample = 0;
        if (kind == 0) sample = std::sin(997.0 * phase);
        else if (kind == 1)
            sample = (std::sin(997.0 * phase) + std::sin(1499.0 * phase)) / std::sqrt(2.0);
        else
        {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            sample = 2.0 * static_cast<double>(state) / 4294967295.0 - 1.0;
        }
        samples[i] = static_cast<float>(sample);
        energy += static_cast<double>(samples[i]) * samples[i];
    }
    const auto gain = std::pow(10.0, levelDb / 20.0) / std::sqrt(energy / samples.size());
    for (auto& sample : samples) sample = static_cast<float>(sample * gain);
    return samples;
}

double measure(const std::vector<float>& input, double rate,
               const disdorktion::MiddleSettings& settings, double drive)
{
    disdorktion::MiddleSection section;
    require(section.setParameters(settings), "Invalid calibration settings");
    require(section.prepare({rate, blockSize, 1}, 4,
                            disdorktion::MiddleLatencyCorrection::fir,
                            disdorktion::MiddleFoldAlgorithm::adaaTriangle), "Calibration preparation failed");
    // The production Driver remains at unity: multiply the fixture before the
    // fully wet section to measure only uncompensated nonlinear predrive.
    const auto latency = static_cast<std::size_t>(section.getLatencySamples());
    const auto first = static_cast<std::size_t>(rate * 0.25) + latency;
    const auto end = input.size() + latency;
    juce::AudioBuffer<float> buffer(1, blockSize);
    double energy = 0.0;
    std::size_t measured = 0;
    for (std::size_t start = 0; start < end; start += blockSize)
    {
        const auto count = static_cast<int>(std::min<std::size_t>(blockSize, end - start));
        buffer.clear();
        for (int i = 0; i < count; ++i)
        {
            const auto position = start + static_cast<std::size_t>(i);
            if (position < input.size()) buffer.setSample(0, i, static_cast<float>(input[position] * drive));
        }
        auto block = juce::dsp::AudioBlock<float>(buffer).getSubBlock(0, static_cast<std::size_t>(count));
        require(section.process(block), "Calibration processing failed");
        for (int i = 0; i < count; ++i)
        {
            if (start + static_cast<std::size_t>(i) < first) continue;
            const double sample = buffer.getSample(0, i);
            require(std::isfinite(sample), "Nonfinite calibration output");
            energy += sample * sample;
            ++measured;
        }
    }
    require(measured == input.size() - static_cast<std::size_t>(rate * 0.25), "Misaligned measurement window");
    require(energy > 0.0, "Silent calibration output");
    return 10.0 * std::log10(energy / measured);
}

struct Row
{
    juce::String preset, signal;
    double rate = 0, level = 0;
    std::array<double, knots.size()> outputDb {}, deltaDb {};
};

juce::var calibrate()
{
    const std::array<const char*, 3> names {"sine_997", "two_tone_997_1499_equal_power", "xorshift32_noise"};
    std::vector<Row> rows;
    for (std::size_t presetIndex = 1; presetIndex < disdorktion::factoryPresets.size(); ++presetIndex)
    {
        const auto& preset = disdorktion::factoryPresets[presetIndex];
        auto settings = preset.settings.middle;
        settings.mix = 1.0f;
        settings.trimDb = settings.tanh.outputDb = settings.folder.outputDb = 0.0f;
        require(!(settings.tanh.bypass && settings.folder.bypass), "Expected four active factory Waveshapers");
        for (const auto rate : rates) for (const auto level : levels) for (int kind = 0; kind < 3; ++kind)
        {
            const auto input = fixture(kind, rate, level);
            Row row;
            row.preset = preset.key; row.signal = names[static_cast<std::size_t>(kind)];
            row.rate = rate; row.level = level;
            for (std::size_t knot = 0; knot < knots.size(); ++knot)
            {
                row.outputDb[knot] = measure(input, rate, settings, knots[knot]);
                row.deltaDb[knot] = row.outputDb[knot] - row.outputDb[0];
            }
            rows.push_back(row);
        }
    }
    require(rows.size() == 72, "Expected 72 fixtures per knot");
    juce::var report = object();
    set(report, "schema", "disdorktion.driver-calibration.v1");
    set(report, "method", "Median settled output RMS delta relative to Driver=1; correction=-median clamped to +/-6 dB");
    set(report, "scope", "Four active canonical factory Waveshaper settings only; mix=1, shared and stage trims=0; external Driver scaling, internal Driver=1");
    set(report, "duration_seconds", 2.0); set(report, "discard_seconds", 0.25);
    set(report, "oversampling_factor", 4); set(report, "block_size", blockSize);
    set(report, "noise_seed", static_cast<juce::int64>(noiseSeed));
    set(report, "noise_algorithm", "xorshift32 shifts 13,17,5; unsigned /4294967295 mapped to [-1,1]");
    set(report, "alignment", "Output measurement [0.25*rate+latency,2*rate+latency), zero-padded input tail; identical generated input for every knot");
    set(report, "normalization", "Each mono fixture normalized to requested RMS across the full two-second input");
    juce::Array<juce::var> results;
    std::vector<double> allNewErrors, allOldErrors;
    bool allImproved = true;
    for (std::size_t knot = 0; knot < knots.size(); ++knot)
    {
        std::vector<double> deltas, newErrors, oldErrors;
        for (const auto& row : rows) deltas.push_back(row.deltaDb[knot]);
        const auto delta = median(deltas);
        const auto correction = knot == 0 ? 0.0 : std::clamp(-delta, -6.0, 6.0);
        const auto oldCorrection = -20.0 * std::log10(knots[knot]);
        juce::Array<juce::var> fixtures;
        for (const auto& row : rows)
        {
            auto result = object();
            const auto newError = row.deltaDb[knot] + correction;
            const auto oldError = row.deltaDb[knot] + oldCorrection;
            set(result, "preset", row.preset); set(result, "signal", row.signal);
            set(result, "sample_rate_hz", row.rate); set(result, "input_rms_dbfs", row.level);
            set(result, "reference_output_rms_dbfs", row.outputDb[0]);
            set(result, "output_rms_dbfs", row.outputDb[knot]); set(result, "delta_db", row.deltaDb[knot]);
            set(result, "new_residual_db", newError); set(result, "old_residual_db", oldError);
            fixtures.add(result);
            newErrors.push_back(std::abs(newError)); oldErrors.push_back(std::abs(oldError));
            if (knot != 0) { allNewErrors.push_back(std::abs(newError)); allOldErrors.push_back(std::abs(oldError)); }
        }
        auto result = object();
        set(result, "driver", knots[knot]); set(result, "fixture_count", static_cast<int>(rows.size()));
        set(result, "median_delta_db", delta); set(result, "correction_db", correction);
        set(result, "old_inverse_correction_db", oldCorrection);
        set(result, "new_median_absolute_error_db", median(newErrors));
        set(result, "old_median_absolute_error_db", median(oldErrors));
        const auto improvement = median(oldErrors) - median(newErrors);
        set(result, "median_absolute_error_improvement_db", improvement);
        if (knot != 0 && improvement <= 0.0) allImproved = false;
        set(result, "fixtures", fixtures); results.add(result);
    }
    set(report, "knots", results);
    auto summary = object();
    set(summary, "excludes_unity_knot", true);
    set(summary, "new_median_absolute_error_db", median(allNewErrors));
    set(summary, "old_median_absolute_error_db", median(allOldErrors));
    set(summary, "improvement_db", median(allOldErrors) - median(allNewErrors));
    set(summary, "every_nonunity_knot_improves_median", allImproved);
    set(summary, "interpretation", allImproved ? "Every nonunity knot improves the fixture median absolute error; individual fixtures may regress."
                                              : "Mixed per-knot results: inspect knot and fixture residuals before claiming improvement.");
    set(report, "summary", summary);
    return report;
}
}

int main(int argc, char** argv)
{
    try
    {
        require(argc <= 2, "Usage: disdorktion_driver_calibration [report.json]");
        const auto report = calibrate();
        const auto json = juce::JSON::toString(report, false);
        if (argc == 2)
            require(juce::File::getCurrentWorkingDirectory().getChildFile(juce::String::fromUTF8(argv[1])).replaceWithText(json),
                    "Could not write calibration report");
        else std::cout << json << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Driver calibration failed: " << error.what() << '\n';
        return 1;
    }
}
