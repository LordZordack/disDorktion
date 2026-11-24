#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>
#include <vector>
#include "PresetDialogs.h"
#include "FaceplateControls.h"

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
    class WaveformDisplay;
    void timerCallback() override;
    void refreshPresetAndMode();
    void addControl(const FaceplateLayout::Control&, const juce::String& label);
    void showPresetMenu();
    void choosePresetFile(bool save, uint64_t generation);
    void reportPresetResult(const juce::Result&);

    PluginProcessor& processor;
    const FaceplateAssets& artwork;
    faceplate::LookAndFeel lookAndFeel;
    // Keep the logical canvas alive until all its child controls are destroyed.
    juce::Component content;
    faceplate::PresetButton presets;
    std::shared_ptr<PresetDialogs> presetDialogs;
    bool presetDialogPending = false;
    std::vector<std::unique_ptr<ControlRow>> controls;
    std::unique_ptr<PeakMeter> inputMeter, outputMeter;
    std::unique_ptr<WaveformDisplay> inputWaveform, outputWaveform;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
}
