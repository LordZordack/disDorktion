#include "FullChainQualification.h"
#include "dsp/FullChainControls.h"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <string>
#include <type_traits>

using namespace disdorktion;
using namespace disdorktion::qualification;

namespace
{
// These captures are behavioral regression evidence, never CPU qualification.
constexpr std::array<unsigned, 20> eventMilliseconds {
    0, 40, 44, 48, 53, 80, 105, 140, 145, 190,
    240, 245, 300, 410, 600, 900, 1100, 1103, 1400, 1404
};

juce::var completeSettings(const FullChainSettings& s)
{
    auto result = settings(s);
    for (const auto& control : fullChainControls)
        put(result, control.id, getFullChainControl(s, control.id));
    put(result, "output.comb.delayMs", s.output.comb.delayMs);
    return result;
}

FullChainSettings initialSettings()
{
    auto s = expressive();
    s.output.comb = {5.f, .4f, .7f, 6000.f, .5f, true, false};
    s.output.dispersion = {500.f, .7f, false};
    s.output.tone = {3.f, false};
    return s;
}

// Short intervals deliberately interrupt both 10ms and 100ms ramps. Other
// intervals let them finish, including the last sample of multiplicative ramps.
void changeSettings(FullChainSettings& s, unsigned event)
{
    if (event == 1 || event == 3 || event == 8 || event == 17 || event == 19)
    {
        const bool upper = event == 1 || event == 8 || event == 19;
        s.output.highPass = {upper ? 16000.f : 20.f, false,
            upper ? FilterResponse::chebyshev1 : FilterResponse::chebyshev2};
        s.output.lowPass = {upper ? 20000.f : 30.f, false,
            upper ? FilterResponse::chebyshev2 : FilterResponse::butterworth};
        s.output.comb.delayMs = upper ? 40.f : .25f;
        s.output.comb.feedback = upper ? .95f : -.95f;
        s.output.comb.feedforward = upper ? 1.f : -1.f;
        s.output.comb.dampingHz = upper ? 20000.f : 20.f;
        s.output.comb.dampingEnabled = upper;
        s.output.dispersion.frequencyHz = upper ? 20000.f : 20.f;
        s.output.tone.toneDb = upper ? 6.f : -6.f;
        s.middle.tanh.driveDb = upper ? 24.f : 0.f;
        s.middle.folder.driveDb = upper ? 18.f : 0.f;
        s.middle.folder.threshold = upper ? .2f : .9f;
        s.mix = upper ? .73f : .21f;
    }
    if (event == 4 || event == 6)
    {
        const bool bypass = event == 4;
        s.output.comb.bypass = s.output.dispersion.bypass = s.output.tone.bypass = bypass;
        s.output.highPass.bypass = s.output.lowPass.bypass = bypass;
        s.middle.tanh.bypass = s.middle.folder.bypass = bypass;
    }
    if (event == 9 || event == 10) s.bypass = event == 9;
    if (event == 12 || event == 13)
    {
        s.output.comb.mix = s.output.dispersion.mix = event == 12 ? 0.f : 1.f;
        s.output.comb.bypass = s.output.dispersion.bypass = s.output.tone.bypass = event == 12;
    }
}

template<class Processor>
bool apply(Processor& processor, const FullChainSettings& s)
{
    if constexpr (std::is_same_v<Processor, FullChain>) return processor.setParameters(s);
    else return processor.setParameters(s.output);
}

template<class Processor>
void checkRejection(Processor& processor, const FullChainSettings& retained,
                    const juce::dsp::ProcessSpec& spec)
{
    auto invalid = retained;
    invalid.output.lowPass.cutoffHz = std::numeric_limits<float>::quiet_NaN();
    require(!apply(processor, invalid));
    invalid = retained; invalid.output.tone.toneDb = 7.f;
    require(!apply(processor, invalid));
    invalid = retained; invalid.output.comb.delayMs = 0.f;
    require(!apply(processor, invalid));
    invalid = retained; invalid.output.highPass.response = static_cast<FilterResponse>(99);
    require(!apply(processor, invalid));
    auto invalidSpec = spec; invalidSpec.maximumBlockSize = 0;
    require(!processor.prepare(invalidSpec));
    invalidSpec = spec; invalidSpec.sampleRate = std::numeric_limits<double>::quiet_NaN();
    require(!processor.prepare(invalidSpec));
}

float excitation(unsigned frame, double rate, unsigned channel)
{
    const double t = frame / rate;
    // Silence retains decaying histories before reset and after re-preparation.
    if ((t >= .65 && t < 1.1) || t >= 1.65) return 0.f;
    const double pi = juce::MathConstants<double>::pi;
    double value = .22 * std::sin(2. * pi * 113. * t)
                 + .13 * std::sin(2. * pi * 997. * t)
                 + .07 * std::sin(2. * pi * (.173 * rate) * t);
    if (frame % 4093 == 0) value += .3;
    return static_cast<float>(value * (channel == 0 ? 1. : -.73));
}

template<class Processor>
juce::var capture(const std::filesystem::path& root, double rate, unsigned channels, bool variable)
{
    constexpr bool full = std::is_same_v<Processor, FullChain>;
    const std::string target = full ? "full-chain" : "output-section";
    const std::string partition = variable ? "irregular" : "fixed127";
    const std::string filename = target + "-" + std::to_string(static_cast<unsigned>(rate))
        + "-ch" + std::to_string(channels) + "-" + partition + ".f32";
    const juce::dsp::ProcessSpec spec {rate, 127, channels};
    Processor processor;
    auto s = initialSettings();
    require(apply(processor, s)); require(processor.prepare(spec));
    const auto latency = processor.getLatencySamples();
    require(latency == (full ? 92u : 0u));
    juce::AudioBuffer<float> buffer(static_cast<int>(channels), 1025);
    std::array<float, 2050> interleaved {};
    std::ofstream stream(root / filename, std::ios::binary);
    require(bool(stream));
    juce::Array<juce::var> events;
    unsigned offset = 0, iteration = 0, event = 0;
    const unsigned total = static_cast<unsigned>(2. * rate);
    const auto eventFrame = [rate](unsigned index)
    { return static_cast<unsigned>(std::llround(eventMilliseconds[index] * rate / 1000.)); };
    const juce::ScopedNoDenormals noDenormals;
    while (offset < total)
    {
        if (event < eventMilliseconds.size() && offset == eventFrame(event))
        {
            changeSettings(s, event);
            require(apply(processor, s));
            const char* action = "settings";
            if (event == 2 || event == 5 || event == 7 || event == 11 || event == 15)
            { checkRejection(processor, s, spec); action = "invalid-settings-and-spec-rejected"; }
            if (event == 16) { processor.reset(); action = "reset"; }
            if (event == 18) { require(processor.prepare(spec)); action = "reprepare-same-spec"; }
            require(processor.getLatencySamples() == latency);
            auto entry = object(); put(entry, "frame", int(offset)); put(entry, "action", action);
            put(entry, "settings", completeSettings(s)); events.add(entry);
            ++event;
        }
        unsigned count = variable ? std::array<unsigned, 6>{1, 32, 257, 13, 1025, 64}[iteration % 6] : 127;
        count = std::min(count, total - offset);
        if (event < eventMilliseconds.size()) count = std::min(count, eventFrame(event) - offset);
        require(count != 0);
        for (unsigned c = 0; c < channels; ++c)
            for (unsigned n = 0; n < count; ++n)
                buffer.setSample(int(c), int(n), excitation(offset + n, rate, c));
        // Reapply identical snapshots even during unfinished ramps: a cache must
        // never snap the live controls to their retained target.
        require(apply(processor, s));
        auto block = juce::dsp::AudioBlock<float>(buffer).getSubBlock(0, count);
        require(processor.process(block));
        for (unsigned n = 0; n < count; ++n)
            for (unsigned c = 0; c < channels; ++c)
            {
                const float value = buffer.getSample(int(c), int(n));
                require(std::isfinite(value));
                interleaved[n * channels + c] = value;
            }
        stream.write(reinterpret_cast<const char*>(interleaved.data()), std::streamsize(count * channels * sizeof(float)));
        require(bool(stream)); offset += count; ++iteration;
    }
    stream.close(); require(!stream.fail());
    auto row = object(); put(row, "scenario", "interrupted-controls-bypass-lifecycle-v1");
    put(row, "target", target); put(row, "partition", partition); put(row, "sample_rate", rate);
    put(row, "channels", int(channels)); put(row, "frames", int(total)); put(row, "capacity", 127);
    put(row, "latency_samples", int(latency)); put(row, "file", filename);
    put(row, "sha256", hash(root / filename));
    put(row, "bytes", static_cast<juce::int64>(std::size_t(total) * channels * sizeof(float)));
    put(row, "events", juce::var(events));
    std::cout << filename << '\n';
    return row;
}
}

int main(int argc, char** argv)
{
    try
    {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        static_assert(std::endian::native == std::endian::little);
        require(argc == 3 && std::string(argv[1]) == "--output");
        const std::filesystem::path root(argv[2]);
        require(!root.empty() && !std::filesystem::exists(root));
        require(std::filesystem::create_directories(root));
        juce::Array<juce::var> rows;
        for (double rate : {8000., 44100., 48000., 96000., 192000., 384000.})
            for (unsigned channels : {1u, 2u}) for (bool variable : {false, true})
            {
                rows.add(capture<FullChain>(root, rate, channels, variable));
                rows.add(capture<OutputSection>(root, rate, channels, variable));
            }
        auto manifest = object(); put(manifest, "kind", "expanded-chain-golden-v1");
        put(manifest, "layout", "interleaved-float32-little-endian");
        put(manifest, "source_recipe", "ExpandedChainGolden.cpp excitation(), absolute sample frame; no random input");
        put(manifest, "comparison", "Compare each case file byte-for-byte against the same case from the baseline; provenance may differ");
        put(manifest, "cpu_qualification", false); put(manifest, "cases", juce::var(rows));
        put(manifest, "provenance", provenance(argv[0]));
        json(root / "manifest.json", manifest);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Golden capture failed: " << error.what()
                  << "\nUsage: --output NEW_DIRECTORY\n";
        return 2;
    }
}
