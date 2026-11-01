#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>
#include <vector>

namespace disdorktion
{
class PluginProcessor;

class PluginEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class ControlRow;
    class PeakMeter;
    void timerCallback() override;
    void refreshPresetAndMode();
    void addControl(int section, const char* id, const char* label);

    PluginProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;
    juce::Label title, subtitle, presetStatus, compatibilityStatus;
    juce::ComboBox presets;
    juce::TextButton initialize {"Initialize"}, clearOverloads {"Clear overloads"};
    std::array<juce::Rectangle<int>, 5> sections;
    std::array<std::vector<std::unique_ptr<ControlRow>>, 5> controls;
    std::unique_ptr<PeakMeter> inputMeter, outputMeter;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
}
