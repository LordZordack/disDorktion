#include "AudioDeviceController.h"
#include <algorithm>
#include <cmath>

namespace disdorktion::audition
{
using namespace harness;
static_assert(std::atomic<double>::is_always_lock_free);
static_assert(std::atomic<unsigned>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

void AudioDeviceController::configure(const ExperimentRecord& record)
{
    settings = record;
    processor.setParameters({settings.gain, settings.bypass});
    processor.setMonitorDb(settings.monitorDb);
    processor.setMuted(settings.muted);
    playing.store(false);
    restartRequested.store(true);
    allNotesOff();
}

void AudioDeviceController::replaceLoop(std::unique_ptr<LoopSource> replacement,
                                       double rate, unsigned channels)
{
    loop = std::move(replacement);
    loopRate = rate;
    loopChannels = channels;
    needsConversion.store(false);
}

void AudioDeviceController::setPlaying(bool value) noexcept
{
    playing.store(value);
    if (!value) allNotesOff();
}
void AudioDeviceController::queueNote(bool on, int note, float velocity) noexcept
{
    midi.push({on ? MidiEvent::Type::noteOn : MidiEvent::Type::noteOff,
               note, velocity, juce::Time::getMillisecondCounterHiRes() * 0.001});
}
void AudioDeviceController::allNotesOff() noexcept
{
    midi.invalidate(); // Include producer reservations not yet published.
    panic.store(true);
}

void AudioDeviceController::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& message)
{
    // Ignore sysex/non-note payloads; only copy compact values into the bounded queue.
    if (message.isNoteOn()) queueNote(true, message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff()) queueNote(false, message.getNoteNumber());
    else if (message.isAllNotesOff() || message.isAllSoundOff()) allNotesOff();
}

void AudioDeviceController::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    ready.store(false);
    const auto rate = device->getCurrentSampleRate();
    const auto channels = static_cast<unsigned>(device->getActiveOutputChannels().countNumberOfSetBits());
    const auto capacity = static_cast<unsigned>(device->getCurrentBufferSizeSamples());
    activeRate.store(rate);
    activeChannels.store(channels);
    activeBlockSize.store(capacity);
    bool sourceReady = true;
    if (settings.source.kind == SourceKind::file)
    {
        sourceReady = loop != nullptr && loopRate == rate && loopChannels == channels;
        needsConversion.store(!sourceReady);
        if (sourceReady) loop->reset();
    }
    else if (settings.source.kind != SourceKind::midi)
        sourceReady = generated.prepare(settings.source, rate, channels);
    const auto targetReady = processor.prepare({rate, capacity, channels});
    const auto voiceReady = voice.prepare(rate);
    pendingEvents = 0;
    allNotesOff();
    ready.store(sourceReady && targetReady && voiceReady);
}

void AudioDeviceController::audioDeviceStopped()
{
    ready.store(false);
    // Do not clear actual device metadata: a UI detach/reattach uses it for loading.
}

void AudioDeviceController::audioDeviceIOCallbackWithContext(const float* const*, int,
    float* const* outputs, int numOutputs, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int c = 0; c < numOutputs; ++c)
        if (outputs[c] != nullptr) juce::FloatVectorOperations::clear(outputs[c], numSamples);
    if (!ready.load() || numSamples <= 0) return;
    std::array<float*, 2> active{};
    unsigned count = 0;
    for (int c = 0; c < numOutputs; ++c)
        if (outputs[c] != nullptr && count < active.size()) active[count++] = outputs[c];
    if (count != activeChannels.load()) return;
    auto block = juce::dsp::AudioBlock<float>(active.data(), count, static_cast<std::size_t>(numSamples));
    if (restartRequested.exchange(false))
    {
        generated.reset();
        if (loop != nullptr) loop->reset();
        voice.reset();
        processor.reset();
        allNotesOff();
    }
    bool discard = panic.exchange(false);
    if (midi.consumeOverflow()) { discard = true; overflowNotice.store(true); }
    // Drain at most one fixed capacity per callback, including emergency recovery.
    MidiEvent event;
    for (std::size_t i = 0; i < MidiEventQueue::capacity && midi.pop(event); ++i)
    {
        if (pendingEvents < events.size() && !discard) events[pendingEvents++] = event;
        else if (!discard) { discard = true; overflowNotice.store(true); }
    }
    if (discard)
    {
        pendingEvents = 0;
        voice.allNotesOff();
    }
    if (playing.load())
    {
        if (settings.source.kind == SourceKind::file && loop != nullptr) loop->process(block);
        else if (settings.source.kind == SourceKind::midi)
        {
            // Stable insertion sort is bounded by the fixed queue capacity, and
            // unlike stable_sort cannot allocate temporary storage. Equal-time
            // note-on/off events retain their FIFO reservation order.
            for (std::size_t i = 1; i < pendingEvents; ++i)
            {
                const auto value = events[i];
                auto j = i;
                while (j > 0 && events[j - 1].timestampSeconds > value.timestampSeconds)
                { events[j] = events[j - 1]; --j; }
                events[j] = value;
            }
            const auto rate = activeRate.load();
            const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            // One block of delivery delay preserves timing inside the preceding interval.
            const auto start = now - static_cast<double>(numSamples) / rate;
            std::size_t consumed = 0;
            for (int n = 0; n < numSamples; ++n)
            {
                const auto timestamp = start + static_cast<double>(n) / rate;
                while (consumed < pendingEvents && events[consumed].timestampSeconds <= timestamp)
                {
                    const auto& e = events[consumed++];
                    if (e.type == MidiEvent::Type::noteOn) voice.noteOn(e.note, e.velocity);
                    else if (e.type == MidiEvent::Type::noteOff) voice.noteOff(e.note);
                    else voice.allNotesOff();
                }
                // SineVoice uses a 0.25 nominal level; expose source amplitude as
                // the actual peak at unit MIDI velocity, matching other sources.
                const auto sample = voice.nextSample() * (4.0f * static_cast<float>(settings.source.amplitude));
                for (unsigned c = 0; c < count; ++c) active[c][n] = sample;
            }
            std::move(events.begin() + static_cast<std::ptrdiff_t>(consumed),
                      events.begin() + static_cast<std::ptrdiff_t>(pendingEvents), events.begin());
            pendingEvents -= consumed;
        }
        else
        {
            generated.process(block);
            if (generated.isFinished())
            {
                // Rewind on the audio thread before the next playback. Publish
                // stopped only after the rewind request is visible to the UI.
                restartRequested.store(true);
                playing.store(false);
            }
        }
    }
    else { pendingEvents = 0; voice.allNotesOff(); }
    processor.process(block);
}
}
