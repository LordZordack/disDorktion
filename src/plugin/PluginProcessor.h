#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/ReferenceGain.h"
#include "dsp/Gain.h"
#include "dsp/FullChainControls.h"
#include <atomic>
#include <array>

namespace disdorktion
{
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
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    bool applyControls() noexcept;
    void render(juce::AudioBuffer<float>&, bool hostBypassed) noexcept;
    void publishControl(const char*, float);
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* gainControl = nullptr;
    std::atomic<float>* bypassControl = nullptr;
    std::array<std::atomic<float>*, fullChainControls.size()> chainControls {};
    ReferenceGain gain;
    Gain inputGain;
    Gain outputGain;
    FullChain chain;
    juce::AudioBuffer<float> bypassDelay, bypassScratch;
    std::size_t bypassPosition = 0;
    std::size_t chunkCapacity = 0;
    // ReferenceGain is a compatibility trim BEFORE the full-chain dry capture.
    // Restored v1/v2 states explicitly select the original gain-only path.
    bool legacyMode = false;
    // Publish host metadata independently: hosts may query tails concurrently.
    std::atomic<double> tailSeconds {3.0};
    double preparedSampleRate = 0.0;
    // JUCE calls prepare before playback and release after playback stops.
    // Lifecycle/reset/restore and processing must be serialized. JUCE wrappers
    // hold getCallbackLock() around callbacks; restore takes that same lock.
    // Direct callers must provide equivalent exclusive ownership.
    bool hostReady = false;
};
}
