#pragma once
#include "TanhSaturation.h"
#include "WaveFolder.h"
#include "AdaaWaveFolder.h"
#include <memory>
#include <array>
#include <vector>
#include <cstdint>

namespace disdorktion
{
struct MiddleSettings
{
    TanhSaturationSettings tanh;
    WaveFolderSettings folder;
    float mix = 1.0f;
    float trimDb = 0.0f;
};
enum class MiddleLatencyCorrection { fir, juceThiran };
enum class MiddleFoldAlgorithm { triangle, adaaTriangle };

// Q1 evaluation boundary. Rate selection is stopped-processing configuration.
class MiddleSection
{
public:
    bool prepare(const juce::dsp::ProcessSpec&, unsigned factor = 4,
                 MiddleLatencyCorrection = MiddleLatencyCorrection::fir,
                 MiddleFoldAlgorithm = MiddleFoldAlgorithm::triangle);
    void reset() noexcept;
    bool setParameters(const MiddleSettings&) noexcept;
    bool setDriver(float value) noexcept;
    bool process(juce::dsp::AudioBlock<float>) noexcept;
    unsigned int getLatencySamples() const noexcept { return latency; }
    unsigned getFactor() const noexcept { return factor; }
    std::uint64_t getWetFramesProcessed() const noexcept { return wetFramesProcessed; }
private:
    // Expose the existing smoother's countdown to split at exact state boundaries.
    struct Fade : juce::SmoothedValue<float>
    {
        std::size_t remaining() const noexcept { return static_cast<std::size_t>(countdown); }
    };
    void resetWet() noexcept;
    void updateWetState() noexcept;
    bool processWet(juce::dsp::AudioBlock<float>) noexcept;
    MiddleSettings retained;
    float retainedDriver = 1.f, cachedDriver = 1.f, cachedCompensation = 1.f;
    juce::SmoothedValue<float> driver {1.f};
    std::vector<float> driverHistory, compensationScratch;
    std::size_t driverPosition = 0;
    Fade sectionMix, nonlinearMix, trimGain;
    juce::AudioBuffer<float> dryScratch;
    std::array<std::vector<float>, 2> dryHistory;
    std::size_t dryPosition = 0, warmupFrames = 0, warmupRemaining = 0;
    std::uint64_t wetFramesProcessed = 0;
    bool wetRunning = false, holdMixDuringWarmup = false;
    BlockDispatcher dispatcher;
    TanhSaturation saturation;
    WaveFolder folder;
    AdaaWaveFolder adaaFolder;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    std::array<double, 65> correctionTaps {};
    // Mirrored ring exposes all 65 newest-to-oldest samples contiguously.
    std::array<std::array<float, 130>, 2> correctionHistory {};
    std::size_t correctionPosition = 0;
    bool needsFirCorrection = false;
    bool usesAdaa = false;
    unsigned factor = 1, latency = 0;
};
}
