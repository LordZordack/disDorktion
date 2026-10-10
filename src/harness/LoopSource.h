#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <cstdint>

namespace disdorktion::harness
{
// load/destruction/swap require a worker or offline context with processing
// detached. Playback owns only resident PCM and wraps without a crossfade.
class LoopSource
{
public:
    bool load(const juce::File&, double targetRate, unsigned channels, juce::String& error,
              const std::atomic<bool>* cancel = nullptr, std::uint64_t externalResidentBytes = 0);
    void reset() noexcept { position = 0; }
    void process(juce::dsp::AudioBlock<float>) noexcept;
    std::uint64_t lengthSamples() const noexcept { return static_cast<std::uint64_t>(pcm.getNumSamples()); }
    std::uint64_t residentMemoryBytes() const noexcept
    {
        return sizeof(float) * lengthSamples() * static_cast<unsigned>(pcm.getNumChannels());
    }
    const juce::String& fileHash() const noexcept { return hash; }
    double originalSampleRate() const noexcept { return sourceRate; }
    unsigned originalChannels() const noexcept { return sourceChannels; }
    const juce::String& path() const noexcept { return filePath; }
    static constexpr std::uint64_t memoryLimitBytes = 256ULL * 1024ULL * 1024ULL;
    static constexpr const char* conversionPolicy = "linear;mono-duplicate;stereo-average";
private:
    juce::AudioBuffer<float> pcm;
    juce::String hash, filePath;
    double sourceRate = 0.0;
    unsigned sourceChannels = 0;
    std::size_t position = 0;
};
}
