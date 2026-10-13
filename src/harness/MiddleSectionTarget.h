#pragma once
#include "AuditionTarget.h"
#include "dsp/MiddleSection.h"
#include <array>

namespace disdorktion::harness
{
inline constexpr std::string_view middleSectionQuality = "4x-fir-adaa-v1";
inline constexpr std::array middleSectionControls {
    TargetControl {"tanhDriveDb", "Tanh drive (dB)", -24.f, 36.f, 0.f},
    TargetControl {"tanhOutputDb", "Tanh output (dB)", -60.f, 12.f, 0.f},
    TargetControl {"tanhBypass", "Bypass tanh", 0.f, 1.f, 0.f},
    TargetControl {"folderDriveDb", "Folder drive (dB)", -24.f, 36.f, 0.f},
    TargetControl {"folderOutputDb", "Folder output (dB)", -60.f, 12.f, 0.f},
    TargetControl {"folderThreshold", "Fold threshold", .05f, 1.f, 1.f},
    TargetControl {"folderBypass", "Bypass folder", 0.f, 1.f, 0.f}
};
inline constexpr TargetDescriptor middleSectionDescriptor {"middle-section", "Middle section (qualification candidate)", middleSectionControls};

class MiddleSectionTarget final : public AuditionTarget
{
public:
    const TargetDescriptor& descriptor() const noexcept override { return middleSectionDescriptor; }
    bool prepare(const juce::dsp::ProcessSpec& spec) override
    { return section.prepare(spec, 4, MiddleLatencyCorrection::fir, MiddleFoldAlgorithm::adaaTriangle); }
    void reset() noexcept override { section.reset(); }
    bool setParameters(const MiddleSettings& next) noexcept
    {
        if (!isValidTanhSaturationSettings(next.tanh) || !isValidWaveFolderSettings(next.folder)) return false;
        auto effective = next;
        effective.tanh.bypass |= commonBypass;
        effective.folder.bypass |= commonBypass;
        if (!section.setParameters(effective)) return false;
        retained = next;
        return true;
    }
    bool setControl(std::string_view id, float value) noexcept override
    {
        auto next = retained;
        if (id == "tanhDriveDb") next.tanh.driveDb = value;
        else if (id == "tanhOutputDb") next.tanh.outputDb = value;
        else if (id == "folderDriveDb") next.folder.driveDb = value;
        else if (id == "folderOutputDb") next.folder.outputDb = value;
        else if (id == "folderThreshold") next.folder.threshold = value;
        else if ((id == "tanhBypass" || id == "folderBypass") && (value == 0.f || value == 1.f))
        {
            if (id == "tanhBypass") next.tanh.bypass = value == 1.f;
            else next.folder.bypass = value == 1.f;
        }
        else return false;
        return setParameters(next);
    }
    void setBypass(bool value) noexcept override { commonBypass = value; (void) setParameters(retained); }
    bool process(juce::dsp::AudioBlock<float> block) noexcept override { return section.process(block); }
    unsigned getLatencySamples() const noexcept override { return section.getLatencySamples(); }
private:
    MiddleSection section;
    MiddleSettings retained;
    bool commonBypass = false;
};
}
