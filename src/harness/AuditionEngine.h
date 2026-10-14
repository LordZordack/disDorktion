#pragma once
#include "ReferenceGainTarget.h"
#include "TanhSaturationTarget.h"
#include "WaveFolderTarget.h"
#include "CutoffFilterTarget.h"
#include "MiddleSectionTarget.h"
#include "OutputSectionTarget.h"
#include <atomic>

namespace disdorktion::harness
{
struct MeterSnapshot
{
    float inputPeak = 0.0f;
    float inputRms = 0.0f;
    float outputPeak = 0.0f;
    float outputRms = 0.0f;
    bool overloaded = false;
    float postGainPeak = 0.0f;
};

class AuditionEngine
{
public:
    // prepare/reset/selectTarget and bulk settings setters require a detached
    // callback. Scalar live setters and meter polling may run concurrently with
    // process(); independent scalar loads do not promise a whole-state snapshot.
    bool prepare(const juce::dsp::ProcessSpec& spec);
    void reset() noexcept;
    bool selectTarget(std::string_view id) noexcept;
    std::string_view selectedTargetId() const noexcept { return selected->descriptor().id; }
    bool setParameters(const GainParameters& parameters) noexcept;
    bool setGain(float gain) noexcept;
    bool setTanhSaturationSettings(const TanhSaturationSettings&) noexcept;
    bool setTanhDriveDb(float) noexcept;
    bool setTanhOutputDb(float) noexcept;
    bool setWaveFolderSettings(const WaveFolderSettings&) noexcept;
    bool setWaveDriveDb(float) noexcept;
    bool setWaveThreshold(float) noexcept;
    bool setWaveOutputDb(float) noexcept;
    bool setFilterSettings(const CutoffSettings&) noexcept;
    bool setFilterCutoffHz(float) noexcept;
    bool setFilterResponse(FilterResponse) noexcept;
    bool setBitCrusherSettings(const BitCrusherSettings&) noexcept;
    bool setBitDepth(unsigned) noexcept;
    bool setCaptureRateHz(float) noexcept;
    bool setCrusherMix(float) noexcept;
    bool setCrush(float) noexcept;
    bool setQuantizerShape(float) noexcept;
    bool setJitter(float) noexcept;
    void setJitterSeed(std::uint32_t) noexcept;
    bool setGainSettings(const GainSettings&) noexcept;
    bool setGainDb(float db) noexcept;
    bool setInputSettings(const InputSettings&, bool commonBypass = false) noexcept;
    bool setMiddleSettings(const MiddleSettings&, bool commonBypass = false) noexcept;
    bool setOutputSettings(const OutputSettings&, bool commonBypass = false) noexcept;
    bool setOutputCutoffHz(bool highPass, float value) noexcept;
    bool setOutputResponse(bool highPass, FilterResponse value) noexcept;
    void setOutputStageBypass(bool highPass, bool value) noexcept;
    bool setMiddleTanhDriveDb(float) noexcept;
    bool setMiddleTanhOutputDb(float) noexcept;
    bool setMiddleFolderDriveDb(float) noexcept;
    bool setMiddleFolderOutputDb(float) noexcept;
    bool setMiddleThreshold(float) noexcept;
    void setMiddleTanhBypass(bool) noexcept;
    void setMiddleFolderBypass(bool) noexcept;
    void setInputGainBypass(bool value) noexcept;
    void setInputCrusherBypass(bool value) noexcept;
    void setModuleMuted(bool value) noexcept;
    void setBypass(bool bypass) noexcept;
    void setMonitorDb(float db) noexcept;
    void setMuted(bool muted) noexcept;
    bool setMonitoring(float db, bool muted) noexcept;
    // Input is already source-filled. Measurements precede monitoring. Offline
    // callers disable monitoring to retain exact module samples and state.
    bool process(juce::dsp::AudioBlock<float> block, bool applyMonitor = true) noexcept;
    MeterSnapshot meters() const noexcept;
    bool consumeOverload() noexcept;
    unsigned int getLatencySamples() const noexcept { return selected->getLatencySamples(); }
private:
    static_assert(std::atomic<float>::is_always_lock_free);
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    void clearMeters() noexcept;
    bool applyParameters() noexcept;
    ReferenceGainTarget target;
    GainTarget productionTarget;
    BitCrusherTarget crusherTarget;
    InputSectionTarget inputTarget;
    MiddleSectionTarget middleTarget;
    OutputSectionTarget outputTarget;
    TanhSaturationTarget saturationTarget;
    WaveFolderTarget folderTarget;
    CutoffFilterTarget<true> highPassTarget;
    CutoffFilterTarget<false> lowPassTarget;
    AuditionTarget* selected = &target;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> monitor {0.0f};
    std::atomic<float> gain {1.0f}, monitorDb {-18.0f};
    std::atomic<float> gainDb {0.0f};
    std::atomic<float> tanhDriveDb {0.0f}, tanhOutputDb {0.0f};
    std::atomic<bool> tanhBypass {false};
    std::atomic<float> waveDriveDb {0.0f}, waveThreshold {1.0f}, waveOutputDb {0.0f};
    std::atomic<bool> waveBypass {false};
    std::atomic<float> highCutoff {20.f}, lowCutoff {20000.f};
    std::atomic<unsigned> highResponse {0}, lowResponse {0};
    std::atomic<bool> highBypass {false}, lowBypass {false};
    std::atomic<unsigned> bitDepth {64};
    std::atomic<float> captureRateHz {384000.0f};
    std::atomic<float> crusherMix {1.0f}, crush {0.0f}, quantizerShape {0.0f}, jitter {0.0f};
    std::atomic<std::uint32_t> jitterSeed {1};
    std::atomic<bool> crusherBypass {false};
    std::atomic<bool> moduleMuted {false}, productionBypass {false};
    std::atomic<bool> inputBypass {false};
    std::atomic<float> middleTanhDrive {0.f}, middleTanhOutput {0.f};
    std::atomic<float> middleFolderDrive {0.f}, middleFolderOutput {0.f}, middleThreshold {1.f};
    std::atomic<bool> middleTanhBypass {false}, middleFolderBypass {false}, middleBypass {false};
    std::atomic<float> outputHighCutoff {20.f}, outputLowCutoff {20000.f};
    std::atomic<unsigned> outputHighResponse {0}, outputLowResponse {0};
    std::atomic<bool> outputHighBypass {false}, outputLowBypass {false}, outputBypass {false};
    std::atomic<float> postGainPeak {0.0f};
    std::atomic<bool> bypass {false}, muted {true};
    std::atomic<float> inputPeak {0.0f}, inputRms {0.0f}, outputPeak {0.0f}, outputRms {0.0f};
    std::atomic<bool> overload {false};
    juce::uint32 channels = 0;
    bool prepared = false;
};
}
