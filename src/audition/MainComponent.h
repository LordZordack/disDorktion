#pragma once
#include "AudioDeviceController.h"
#include "harness/OfflineRender.h"
#include <functional>
#include <mutex>
#include <thread>

namespace disdorktion::audition
{
class MainComponent final : public juce::Component, private juce::Timer,
                            private juce::MidiKeyboardStateListener
{
public:
    MainComponent();
    ~MainComponent() override;
    void resized() override;
    void paint(juce::Graphics&) override;
    void focusLost(FocusChangeType) override;
    void focusOfChildComponentChanged(FocusChangeType) override;
private:
    void timerCallback() override;
    void handleNoteOn(juce::MidiKeyboardState*, int, int note, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int, int note, float) override;
    void configureRecord(const harness::ExperimentRecord&);
    bool readSourceControls(harness::ExperimentRecord&);
    void applySource();
    void loadAudio(const juce::File&, harness::ExperimentRecord, bool verifyHash, bool restoring = false);
    void chooseFile(bool save, const juce::String& pattern,
                    std::function<void(const juce::File&)>);
    void showDevices();
    void saveExperiment();
    void loadExperiment();
    void renderExperiment();
    harness::ExperimentRecord snapshot() const;
    void beginJob(std::function<void()>);
    void setWorkerControlsEnabled(bool);
    void status(const juce::String&);

    juce::AudioDeviceManager devices;
    AudioDeviceController audio;
    harness::ExperimentRecord record;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard{keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard};
    juce::ComboBox target, source;
    juce::Slider gain, monitor;
    juce::ToggleButton bypass{"Bypass"}, moduleMuted{"Mute module"}, muted{"Mute monitoring"};
    juce::TextButton apply{"Apply source"}, load{"Load WAV / AIFF"}, play{"Play / stop"}, restart{"Restart"};
    juce::TextButton save{"Save experiment"}, restore{"Load experiment"}, render{"Render offline"}, deviceButton{"Audio / MIDI devices"};
    juce::TextEditor frequency, frequency2, amplitude, phase, duration, renderDuration, seed, observations;
    juce::Label title, gainLabel, monitorLabel, sourceLabel, fieldsLabel, meterLabel, deviceLabel, statusLabel, observationsLabel;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Component::SafePointer<juce::DialogWindow> deviceDialog;
    std::thread worker;
    std::atomic<bool> cancel{false}, jobDone{true};
    std::mutex jobMutex;
    std::unique_ptr<harness::LoopSource> completedLoop;
    harness::ExperimentRecord completedRecord;
    juce::String jobMessage;
    bool loopJob = false, loopSucceeded = false, running = false;
    bool restoringLoopRecord = false;
    double completionRate = 0;
    unsigned completionChannels = 0;
    double overloadVisibleUntil = 0;
    bool applicationHadFocus = true;
    double lastConversionAttemptRate = 0;
    unsigned lastConversionAttemptChannels = 0;
};
}
