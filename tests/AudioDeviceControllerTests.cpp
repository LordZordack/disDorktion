#include "audition/AudioDeviceController.h"
#include "AllocationTracking.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <array>
#include <cmath>

namespace
{
using disdorktion::audition::AudioDeviceController;
using disdorktion::harness::ExperimentRecord;
using disdorktion::harness::SourceKind;

// Only metadata is used: this fake never opens hardware or starts a thread.
class CallbackDevice final : public juce::AudioIODevice
{
public:
    CallbackDevice(double rateValue = 48000.0, int capacityValue = 64,
                   std::initializer_list<int> active = {0, 1})
        : juce::AudioIODevice("Callback fixture", "Test"), rate(rateValue), capacity(capacityValue)
    { for (auto channel : active) outputs.setBit(channel); }
    juce::StringArray getOutputChannelNames() override { return {"0", "1", "2", "3"}; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override { return {rate}; }
    juce::Array<int> getAvailableBufferSizes() override { return {capacity}; }
    int getDefaultBufferSize() override { return capacity; }
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return false; }
    void start(juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return false; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return capacity; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return outputs; }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
private:
    double rate;
    int capacity;
    juce::BigInteger outputs;
};

ExperimentRecord callbackRecord(SourceKind kind = SourceKind::sine)
{
    ExperimentRecord record;
    record.source.kind = kind;
    record.source.amplitude = 0.25;
    record.source.frequency = 440.0;
    record.source.durationSamples = 10000;
    record.monitorDb = 0.0f;
    record.muted = false;
    return record;
}

void callback(AudioDeviceController& controller, float* const* outputs, int count, int frames)
{
    controller.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, count, frames, {});
}
void callback(AudioDeviceController& controller, juce::AudioBuffer<float>& audio)
{
    callback(controller, audio.getArrayOfWritePointers(), audio.getNumChannels(), audio.getNumSamples());
}
void fillCallback(juce::AudioBuffer<float>& audio, float value)
{
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int n = 0; n < audio.getNumSamples(); ++n) audio.setSample(c,n,value);
}
void primeMidi(AudioDeviceController& controller, CallbackDevice& device, juce::AudioBuffer<float>& audio)
{
    controller.configure(callbackRecord(SourceKind::midi));
    controller.audioDeviceAboutToStart(&device);
    controller.setPlaying(true);
    callback(controller,audio); // Consume initial prepare/restart panic before events.
}
void settleMidi(AudioDeviceController& controller, juce::AudioBuffer<float>& audio)
{
    // queueNote uses the current monotonic clock. Force this event into the
    // preceding interval so the callback clamps it to frame zero deterministically.
    juce::Thread::sleep(3);
    for (int i = 0; i < 8; ++i) callback(controller,audio);
}
}

TEST_CASE("Device callback generated samples and pre-monitor meters match a sine oracle", "[controller]")
{
    for (auto channels : {1,2})
    {
        AudioDeviceController controller;
        CallbackDevice device(48000.0, 16, channels == 1 ? std::initializer_list<int>{0} : std::initializer_list<int>{0,1});
        auto record = callbackRecord(); record.gain = 2.0f;
        controller.configure(record); controller.audioDeviceAboutToStart(&device);
        REQUIRE(controller.isReady()); REQUIRE(controller.channelCount() == static_cast<unsigned>(channels));
        REQUIRE(controller.sampleRate() == 48000.0); REQUIRE(controller.blockSize() == 16);
        controller.setPlaying(true);
        juce::AudioBuffer<float> audio(channels,257); // Larger than prepared capacity.
        callback(controller,audio);
        double squares = 0.0;
        float peak = 0.0f;
        for (int n = 0; n < 257; ++n)
        {
            const auto input = static_cast<float>(0.25 * std::sin(juce::MathConstants<double>::twoPi * 440.0 * n / 48000.0));
            for (int c = 0; c < channels; ++c) REQUIRE(audio.getSample(c,n) == Catch::Approx(2.0f*input).margin(0.000001f));
            peak = std::max(peak,std::abs(input)); squares += static_cast<double>(input)*input;
        }
        auto meter = controller.engine().meters();
        REQUIRE(meter.inputPeak == Catch::Approx(peak));
        REQUIRE(meter.inputRms == Catch::Approx(std::sqrt(squares/257.0)));
        REQUIRE(meter.outputPeak == Catch::Approx(2.0f*peak));
        REQUIRE(meter.outputRms == Catch::Approx(2.0*std::sqrt(squares/257.0)));
        controller.engine().setMuted(true); controller.restart(); callback(controller,audio);
        REQUIRE(audio.getMagnitude(0,0,257) == 0.0f);
        REQUIRE(controller.engine().meters().outputPeak == Catch::Approx(2.0f*peak));
        controller.setPlaying(false); callback(controller,audio);
        REQUIRE(audio.getMagnitude(0,0,257) == 0.0f);
    }
}

TEST_CASE("Device restoration selects production gain and keeps monitor mute distinct", "[controller][gain]")
{
    AudioDeviceController controller; CallbackDevice device;
    auto record = callbackRecord(SourceKind::impulse);
    record.version = 2; record.targetId = "gain"; record.gainDb = 6.0f;
    controller.configure(record); controller.audioDeviceAboutToStart(&device); controller.setPlaying(true);
    juce::AudioBuffer<float> audio(2, 64); callback(controller, audio);
    REQUIRE(controller.engine().selectedTargetId() == "gain");
    REQUIRE(audio.getSample(0, 0) == Catch::Approx(0.25 * std::pow(10.0, 6.0 / 20.0)));
    record.muted = true;
    controller.configure(record); controller.audioDeviceAboutToStart(&device); controller.setPlaying(true); callback(controller, audio);
    REQUIRE(audio.getMagnitude(0, 64) == 0.0f);
    REQUIRE(controller.engine().meters().outputPeak > 0.0f);
    record.mute = true; record.muted = false;
    controller.configure(record); controller.audioDeviceAboutToStart(&device); controller.setPlaying(true); callback(controller, audio);
    REQUIRE(controller.engine().meters().outputPeak == 0.0f);
    record = callbackRecord(SourceKind::impulse); record.gain = 0.00001f;
    controller.configure(record); controller.audioDeviceAboutToStart(&device); controller.setPlaying(true); callback(controller, audio);
    REQUIRE(controller.engine().selectedTargetId() == "reference-gain");
    REQUIRE(audio.getSample(0, 0) == 0.25f * record.gain);
}

TEST_CASE("Generated playback stops at completion and Play repeats without Restart", "[controller]")
{
    for (auto kind : {SourceKind::impulse, SourceKind::sine, SourceKind::twoTone, SourceKind::sweep, SourceKind::noise})
    for (auto frames : {64, 65})
    for (auto idleBeforeReplay : {false, true})
    {
        AudioDeviceController controller;
        CallbackDevice device;
        auto record = callbackRecord(kind);
        record.source.durationSamples = static_cast<std::uint64_t>(frames);
        record.source.phase = 0.7;
        controller.configure(record);
        controller.audioDeviceAboutToStart(&device);
        controller.setPlaying(true);
        juce::AudioBuffer<float> audio(2, 64);
        callback(controller, audio);
        std::array<float, 64> first{};
        std::copy_n(audio.getReadPointer(0), first.size(), first.begin());
        if (frames > 64)
        {
            REQUIRE(controller.isPlaying());
            callback(controller, audio);
            for (int n = 1; n < 64; ++n) REQUIRE(audio.getSample(0, n) == 0.0f);
        }
        REQUIRE_FALSE(controller.isPlaying());
        if (idleBeforeReplay)
        {
            callback(controller, audio);
            REQUIRE_FALSE(controller.isPlaying());
            REQUIRE(audio.getMagnitude(0, 0, 64) == 0.0f);
        }
        controller.setPlaying(true);
        callback(controller, audio);
        for (int n = 0; n < 64; ++n)
            REQUIRE(audio.getSample(0, n) == first[static_cast<std::size_t>(n)]);
        REQUIRE(controller.isPlaying() == (frames > 64));
    }
}

TEST_CASE("Device callback compacts sparse stereo outputs and silences mismatched layouts", "[controller]")
{
    AudioDeviceController controller;
    CallbackDevice device(48000.0,64,{0,3});
    controller.configure(callbackRecord()); controller.audioDeviceAboutToStart(&device); controller.setPlaying(true);
    juce::AudioBuffer<float> audio(2,64); fillCallback(audio,9.0f);
    std::array<float*,4> sparse {audio.getWritePointer(0),nullptr,nullptr,audio.getWritePointer(1)};
    callback(controller,sparse.data(),4,64);
    REQUIRE(audio.getMagnitude(0,0,64) > 0.0f);
    for (int n = 0; n < 64; ++n) REQUIRE(audio.getSample(0,n) == audio.getSample(1,n));
    fillCallback(audio,9.0f);
    std::array<float*,4> missing {audio.getWritePointer(0),nullptr,nullptr,nullptr};
    callback(controller,missing.data(),4,64);
    REQUIRE(audio.getMagnitude(0,0,64) == 0.0f);
    REQUIRE(audio.getSample(1,0) == 9.0f); // Storage not passed to callback is untouched.
    controller.audioDeviceStopped(); fillCallback(audio,9.0f); callback(controller,audio);
    REQUIRE_FALSE(controller.isReady()); REQUIRE(audio.getMagnitude(0,0,64) == 0.0f); REQUIRE(audio.getMagnitude(1,0,64) == 0.0f);
    CallbackDevice invalid(48000.0,64,{0,1,2});
    controller.audioDeviceAboutToStart(&invalid); fillCallback(audio,9.0f); callback(controller,audio);
    REQUIRE_FALSE(controller.isReady()); REQUIRE(audio.getMagnitude(0,0,64) == 0.0f);
}

TEST_CASE("Device callback keyboard hardware MIDI note-off and panic release the voice", "[controller][midi]")
{
    AudioDeviceController controller;
    CallbackDevice device;
    juce::AudioBuffer<float> audio(2,64);
    primeMidi(controller,device,audio);
    controller.queueNote(true,69,1.0f); settleMidi(controller,audio);
    REQUIRE(audio.getMagnitude(0,0,64) > 0.01f);
    controller.queueNote(false,69); settleMidi(controller,audio);
    REQUIRE(audio.getMagnitude(0,0,64) == 0.0f);
    const auto noteOn = juce::MidiMessage::noteOn(1,72,1.0f);
    const auto noteOff = juce::MidiMessage::noteOff(1,72);
    controller.handleIncomingMidiMessage(nullptr,noteOn); settleMidi(controller,audio);
    REQUIRE(audio.getMagnitude(0,0,64) > 0.01f);
    controller.handleIncomingMidiMessage(nullptr,noteOff); settleMidi(controller,audio);
    REQUIRE(audio.getMagnitude(0,0,64) == 0.0f);
    controller.queueNote(true,60); settleMidi(controller,audio);
    REQUIRE(audio.getMagnitude(0,0,64) > 0.01f);
    controller.queueNote(true,84); // A queued note before focus panic must never restart it.
    controller.allNotesOff(); settleMidi(controller,audio);
    REQUIRE(audio.getMagnitude(0,0,64) == 0.0f);
    controller.queueNote(true,67); settleMidi(controller,audio);
    REQUIRE(audio.getMagnitude(0,0,64) > 0.01f);
    controller.handleIncomingMidiMessage(nullptr,juce::MidiMessage::allNotesOff(1)); settleMidi(controller,audio);
    REQUIRE(audio.getMagnitude(0,0,64) == 0.0f);
}

TEST_CASE("Device callback queue overflow clears the voice and publishes an indicator", "[controller][midi]")
{
    AudioDeviceController controller;
    CallbackDevice device;
    juce::AudioBuffer<float> audio(2,64); primeMidi(controller,device,audio);
    controller.queueNote(true,69); settleMidi(controller,audio);
    REQUIRE(audio.getMagnitude(0,0,64) > 0.01f);
    for (std::size_t i = 0; i <= disdorktion::harness::MidiEventQueue::capacity; ++i) controller.queueNote(true,60);
    settleMidi(controller,audio);
    REQUIRE(audio.getMagnitude(0,0,64) == 0.0f);
    REQUIRE(controller.consumeMidiOverflow()); REQUIRE_FALSE(controller.consumeMidiOverflow());
}

TEST_CASE("Prepared app callbacks including MIDI queues and malformed outputs allocate nothing", "[controller][realtime]")
{
    using namespace disdorktion;
    AudioDeviceController generated,midi;
    CallbackDevice device;
    juce::AudioBuffer<float> audio(2,257),voiceAudio(2,64);
    generated.configure(callbackRecord()); generated.audioDeviceAboutToStart(&device); generated.setPlaying(true);
    callback(generated,audio);
    primeMidi(midi,device,voiceAudio);
    // Compact MIDI messages and all owning storage exist before tracking.
    const auto noteOn = juce::MidiMessage::noteOn(1,69,1.0f);
    const auto noteOff = juce::MidiMessage::noteOff(1,69);
    std::array<float*,4> sparse {audio.getWritePointer(0),nullptr,nullptr,audio.getWritePointer(1)};
    std::array<float*,1> malformed {audio.getWritePointer(0)};
    {
        test::ScopedAllocationTracking tracking;
        generated.restart(); callback(generated,sparse.data(),4,257);
        callback(generated,malformed.data(),1,257);
        callback(generated,sparse.data(),4,0);
        generated.engine().setGain(2.0f); generated.engine().setBypass(true); generated.engine().setMonitorDb(-6.0f);
        callback(generated,sparse.data(),4,257);
        midi.handleIncomingMidiMessage(nullptr,noteOn);
        for (int i = 0; i < 16; ++i) callback(midi,voiceAudio);
        midi.handleIncomingMidiMessage(nullptr,noteOff);
        midi.allNotesOff(); callback(midi,voiceAudio);
        for (std::size_t i = 0; i <= harness::MidiEventQueue::capacity; ++i) midi.queueNote(true,60);
        callback(midi,voiceAudio);
        generated.setPlaying(false); callback(generated,audio);
        generated.audioDeviceStopped(); callback(generated,audio);
    }
    const auto counts = test::getAllocationCounts();
    REQUIRE(counts.allocations == 0); REQUIRE(counts.deallocations == 0);
}
