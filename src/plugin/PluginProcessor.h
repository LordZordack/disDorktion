#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/ReferenceGain.h"
#include "dsp/Gain.h"
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
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "DisDorktion"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
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
    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* gainControl = nullptr;
    std::atomic<float>* bypassControl = nullptr;
    std::array<std::atomic<float>*, 6> productionControls {};
    ReferenceGain gain;
    Gain inputGain;
    Gain outputGain;
    // JUCE calls prepare before playback and release after playback stops.
    // Lifecycle/reset and processing must be serialized, as must the DSP module.
    bool hostReady = false;
};
}
