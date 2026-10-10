#pragma once
#include "AuditionTarget.h"
#include "dsp/BitCrusher.h"
#include <array>
#include <cmath>

namespace disdorktion::harness
{
inline constexpr std::array bitCrusherControls {
    TargetControl {"bitDepth", "Bit depth", 1.0f, 64.0f, 64.0f},
    TargetControl {"captureRateHz", "Capture rate (Hz)", 100.0f, 384000.0f, 384000.0f},
    TargetControl {"mix", "Mix", 0.0f, 1.0f, 1.0f},
    TargetControl {"crush", "Crush", 0.0f, 1.0f, 0.0f},
    TargetControl {"shape", "Shape", -1.0f, 1.0f, 0.0f},
    TargetControl {"jitter", "Jitter", 0.0f, 1.0f, 0.0f}
};
inline constexpr TargetDescriptor bitCrusherDescriptor {"bit-crusher", "Bit crusher", bitCrusherControls};

class BitCrusherTarget final : public AuditionTarget
{
public:
    const TargetDescriptor& descriptor() const noexcept override { return bitCrusherDescriptor; }
    bool prepare(const juce::dsp::ProcessSpec& spec) override { return crusher.prepare(spec); }
    void reset() noexcept override { crusher.reset(); }
    bool setControl(std::string_view id, float value) noexcept override
    {
        auto next = settings;
        if (id == "bitDepth")
        {
            if (!std::isfinite(value) || value < 1.0f || value > 64.0f
                || value != std::floor(value)) return false;
            next.bitDepth = static_cast<unsigned>(value);
        }
        else if (id == "captureRateHz") next.captureRateHz = value;
        else if (id == "mix") next.mix = value;
        else if (id == "crush") next.crush = value;
        else if (id == "shape") next.shape = value;
        else if (id == "jitter") next.jitter = value;
        else return false;
        return setParameters(next);
    }
    void setBypass(bool value) noexcept override { auto next = settings; next.bypass = value; (void) setParameters(next); }
    bool setParameters(const BitCrusherSettings& value) noexcept
    {
        if (!crusher.setParameters(value)) return false;
        settings = value;
        return true;
    }
    bool process(juce::dsp::AudioBlock<float> block) noexcept override { return crusher.process(block); }
    unsigned getLatencySamples() const noexcept override { return crusher.getLatencySamples(); }
private:
    BitCrusher crusher;
    BitCrusherSettings settings;
};
}
