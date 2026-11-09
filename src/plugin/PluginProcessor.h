#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/FullChainControls.h"
#include <atomic>
#include <array>

namespace disdorktion
{
struct PluginMeters
{
    float inputPeak = 0, outputPeak = 0;
    bool inputOverload = false, outputOverload = false;
};
class PluginProcessor final : public juce::AudioProcessor
{
public:
    PluginProcessor();
    void prepareToPlay(double sampleRate, int maximumBlockSize) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout& layout) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "DisDorktion"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return tailSeconds.load(std::memory_order_relaxed); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    juce::AudioProcessorValueTreeState& parameterState() noexcept { return parameters; }
    // Presets/Initialize are message-thread operations.
    bool loadFactoryPreset(int index);
    int matchingFactoryPreset() const;
    PluginMeters readMeters() noexcept;
    void clearMeterOverloads() noexcept { clearOverloads.store(true, std::memory_order_relaxed); }
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    bool applyControls() noexcept;
    void render(juce::AudioBuffer<float>&, bool hostBypassed) noexcept;
    void publishControl(const char*, float);
    void resetMeters() noexcept;
    void accumulateMeters(float input, float output) noexcept;
    juce::AudioProcessorValueTreeState parameters;
    std::array<std::atomic<float>*, fullChainControls.size()> chainControls {};
    FullChain chain;
    juce::AudioBuffer<float> bypassDelay, bypassScratch;
    std::size_t bypassPosition = 0;
    std::size_t chunkCapacity = 0;
    std::atomic<double> fullTailSeconds {20.0};
    std::atomic<float> inputMeter {0}, outputMeter {0};
    std::atomic<bool> inputClip {false}, outputClip {false}, clearOverloads {false};
    // Publish host metadata independently: hosts may query tails concurrently.
    std::atomic<double> tailSeconds {20.0};
    // JUCE calls prepare before playback and release after playback stops.
    // Lifecycle/reset/restore and processing must be serialized. JUCE wrappers
    // hold getCallbackLock() around callbacks; restore takes that same lock.
    // Direct callers must provide equivalent exclusive ownership.
    bool hostReady = false;
};
}
