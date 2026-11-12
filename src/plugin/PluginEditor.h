#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>
#include <vector>
#include "PresetDialogs.h"

namespace disdorktion
{
class PluginProcessor;

class PluginEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PluginEditor(PluginProcessor&, std::shared_ptr<PresetDialogs> = {});
    ~PluginEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class ControlRow;
    class PeakMeter;
    void timerCallback() override;
    void refreshPresetAndMode();
    void addControl(int section, const char* id, const char* label);
    void showPresetMenu();
    void choosePresetFile(bool save, uint64_t generation);
    void reportPresetResult(const juce::Result&);

    PluginProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;
    juce::Label title, subtitle, presetStatus;
    juce::TextButton presets, clearOverloads {"Clear overloads"};
    std::shared_ptr<PresetDialogs> presetDialogs;
    bool presetDialogPending = false;
    std::array<juce::Rectangle<int>, 4> sections;
    std::array<std::vector<std::unique_ptr<ControlRow>>, 4> controls;
    std::unique_ptr<PeakMeter> inputMeter, outputMeter;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
}
