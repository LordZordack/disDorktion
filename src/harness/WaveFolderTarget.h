#pragma once
#include "AuditionTarget.h"
#include "dsp/WaveFolder.h"
#include <array>

namespace disdorktion::harness
{
inline constexpr std::array waveFolderControls {
    TargetControl {"driveDb", "Drive (dB)", -24.0f, 36.0f, 0.0f},
    TargetControl {"threshold", "Fold threshold", 0.05f, 1.0f, 1.0f},
    TargetControl {"outputDb", "Output (dB)", -60.0f, 12.0f, 0.0f}
};
inline constexpr TargetDescriptor waveFolderDescriptor {"wave-folder", "Wave folder", waveFolderControls};
class WaveFolderTarget final : public AuditionTarget
{
public:
    const TargetDescriptor& descriptor() const noexcept override { return waveFolderDescriptor; }
    bool prepare(const juce::dsp::ProcessSpec& spec) override { return folder.prepare(spec); }
    void reset() noexcept override { folder.reset(); }
    bool setControl(std::string_view id, float value) noexcept override
    {
        auto next = settings;
        if (id == "driveDb") next.driveDb = value;
        else if (id == "threshold") next.threshold = value;
        else if (id == "outputDb") next.outputDb = value;
        else return false;
        return setParameters(next);
    }
    void setBypass(bool value) noexcept override { auto next = settings; next.bypass = value; (void) setParameters(next); }
    bool setParameters(const WaveFolderSettings& value) noexcept
    {
        if (!folder.setParameters(value)) return false;
        settings = value; return true;
    }
    bool process(juce::dsp::AudioBlock<float> block) noexcept override { return folder.process(block); }
    unsigned getLatencySamples() const noexcept override { return folder.getLatencySamples(); }
private:
    WaveFolder folder;
    WaveFolderSettings settings;
};
}
