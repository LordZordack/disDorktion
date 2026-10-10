#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "harness/AuditionEngine.h"
#include "harness/ExperimentRecord.h"
#include "harness/SignalSource.h"
#include "harness/LoopSource.h"
#include "harness/SineVoice.h"
#include "harness/MidiEventQueue.h"
#include <array>

namespace disdorktion::audition
{
// Configuration and loop replacement require removeAudioCallback first.
// Device lifecycle is serialized with audio IO by AudioDeviceManager.
class AudioDeviceController final : public juce::AudioIODeviceCallback,
                                    public juce::MidiInputCallback
{
public:
    void configure(const harness::ExperimentRecord&);
    void replaceLoop(std::unique_ptr<harness::LoopSource>, double rate, unsigned channels);
    void setPlaying(bool value) noexcept;
    bool isPlaying() const noexcept { return playing.load(); }
    void restart() noexcept { restartRequested.store(true); }
    void queueNote(bool on, int note, float velocity = 1.0f) noexcept;
    void allNotesOff() noexcept;
    bool consumeMidiOverflow() noexcept { return overflowNotice.exchange(false); }
    double sampleRate() const noexcept { return activeRate.load(); }
    unsigned channelCount() const noexcept { return activeChannels.load(); }
    unsigned blockSize() const noexcept { return activeBlockSize.load(); }
    bool fileNeedsConversion() const noexcept { return needsConversion.load(); }
    bool isReady() const noexcept { return ready.load(); }
    harness::AuditionEngine& engine() noexcept { return processor; }
    const harness::LoopSource* loadedLoop() const noexcept { return loop.get(); }

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const*,
        int, int, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;

private:
    harness::AuditionEngine processor;
    harness::SignalSource generated;
    harness::SineVoice voice;
    harness::MidiEventQueue midi;
    std::unique_ptr<harness::LoopSource> loop;
    harness::ExperimentRecord settings;
    double loopRate = 0;
    unsigned loopChannels = 0;
    std::atomic<double> activeRate{0};
    std::atomic<unsigned> activeChannels{0}, activeBlockSize{0};
    std::atomic<bool> playing{false}, restartRequested{false}, ready{false};
    std::atomic<bool> needsConversion{false}, overflowNotice{false}, panic{false};
    std::array<harness::MidiEvent, harness::MidiEventQueue::capacity> events{};
    std::size_t pendingEvents = 0;
};
}
