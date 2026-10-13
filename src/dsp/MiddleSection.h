#pragma once
#include "TanhSaturation.h"
#include "WaveFolder.h"
#include "AdaaWaveFolder.h"
#include <memory>
#include <array>

namespace disdorktion
{
struct MiddleSettings
{
    TanhSaturationSettings tanh;
    WaveFolderSettings folder;
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
    bool process(juce::dsp::AudioBlock<float>) noexcept;
    unsigned int getLatencySamples() const noexcept { return latency; }
    unsigned getFactor() const noexcept { return factor; }
private:
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
