#pragma once
#include "ReferenceGainTarget.h"
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
};

class AuditionEngine
{
public:
    // prepare/reset/selection/restoration require a detached callback. The scalar
    // live setters and meter polling may run concurrently with process().
    bool prepare(const juce::dsp::ProcessSpec& spec);
    void reset() noexcept;
    bool selectTarget(std::string_view id) noexcept;
    std::string_view selectedTargetId() const noexcept { return "reference-gain"; }
    bool setParameters(const GainParameters& parameters) noexcept;
    bool setGain(float gain) noexcept;
    void setBypass(bool bypass) noexcept;
    void setMonitorDb(float db) noexcept;
    void setMuted(bool muted) noexcept;
    bool setMonitoring(float db, bool muted) noexcept;
    // Input is already source-filled. Measurements precede monitoring. Offline
    // callers disable monitoring to retain exact module samples and state.
    bool process(juce::dsp::AudioBlock<float> block, bool applyMonitor = true) noexcept;
    MeterSnapshot meters() const noexcept;
    bool consumeOverload() noexcept;
    unsigned int getLatencySamples() const noexcept { return target.getLatencySamples(); }
private:
    static_assert(std::atomic<float>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    void clearMeters() noexcept;
    ReferenceGainTarget target;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> monitor {0.0f};
    std::atomic<float> gain {1.0f}, monitorDb {-18.0f};
    std::atomic<bool> bypass {false}, muted {true};
    std::atomic<float> inputPeak {0.0f}, inputRms {0.0f}, outputPeak {0.0f}, outputRms {0.0f};
    std::atomic<bool> overload {false};
    juce::uint32 channels = 0;
    bool prepared = false;
};
}
