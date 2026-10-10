#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cstdint>

namespace disdorktion::harness
{
enum class SourceKind { impulse, sine, twoTone, sweep, noise, file, midi };
struct SourceSettings
{
    SourceKind kind = SourceKind::sine;
    double frequency = 440.0, frequency2 = 880.0, amplitude = 0.25, phase = 0.0;
    std::uint64_t durationSamples = 48000;
    std::uint32_t seed = 1;
};

// One finite sequence, then silence. Samples depend on the absolute sample index;
// arbitrary block partitions and reset give the same sequence.
class SignalSource
{
public:
    bool prepare(const SourceSettings&, double sampleRate, unsigned channels);
    static bool validate(const SourceSettings&, double sampleRate, juce::String& error);
    void reset() noexcept;
    void process(juce::dsp::AudioBlock<float>) noexcept;
    bool isFinished() const noexcept { return position >= settings.durationSamples; }
private:
    SourceSettings settings;
    double rate = 48000.0, sweepLog = 0.0;
    std::uint64_t position = 0;
    std::uint32_t randomState = 1;
    unsigned channelCount = 0;
};
}
