#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "harness/SineVoice.h"
#include "harness/MidiEventQueue.h"
#include <juce_core/juce_core.h>
#include <cmath>
#include <thread>

using namespace disdorktion::harness;

TEST_CASE("Sine voice scales velocity ramps and releases", "[harness][midi]")
{
    SineVoice voice; REQUIRE(voice.prepare(48000));
    voice.noteOn(69, 0.5f);
    for (int n = 0; n < 500; ++n)
    {
        const auto gain = 0.125 * std::min(1.0, (n + 1) / 240.0);
        REQUIRE(voice.nextSample() == Catch::Approx(gain * std::sin(juce::MathConstants<double>::twoPi * 440.0 * n / 48000.0)).margin(1e-6));
    }
    voice.noteOff(69);
    for (int n = 0; n < 240; ++n) voice.nextSample();
    for (int n = 0; n < 500; ++n) REQUIRE(voice.nextSample() == 0.0f);
    voice.noteOn(69, 1.0f); voice.allNotesOff();
    for (int n = 0; n < 240; ++n) voice.nextSample();
    REQUIRE(voice.nextSample() == 0.0f);
}

TEST_CASE("Sine voice restores last held note and deterministic reset", "[harness][midi]")
{
    SineVoice voice; REQUIRE(voice.prepare(48000));
    voice.noteOn(69, 1); voice.noteOn(81, 1); voice.noteOff(81);
    for (int n = 0; n < 1000; ++n)
    {
        const auto gain = 0.25 * std::min(1.0, (n + 1) / 240.0);
        REQUIRE(voice.nextSample() == Catch::Approx(gain * std::sin(juce::MathConstants<double>::twoPi * 440.0 * n / 48000.0)).margin(1e-6));
    }
    voice.reset(); voice.noteOn(69, 1);
    REQUIRE(voice.nextSample() == 0.0f);
    voice.noteOn(-1, 1); voice.noteOn(128, 1); voice.noteOff(128);
    REQUIRE(std::isfinite(voice.nextSample()));
    voice.noteOn(69, 0);
    for (int n = 0; n < 240; ++n) voice.nextSample();
    REQUIRE(voice.nextSample() == 0.0f);
}

TEST_CASE("MIDI queue preserves bounded FIFO and isolates overflow", "[harness][midi]")
{
    MidiEventQueue queue;
    MidiEvent event;
    for (std::size_t n = 0; n < MidiEventQueue::capacity; ++n)
        REQUIRE(queue.push({MidiEvent::Type::noteOn, static_cast<int>(n), 0.5f, static_cast<double>(n)}));
    for (std::size_t n = 0; n < MidiEventQueue::capacity; ++n)
    {
        REQUIRE(queue.pop(event)); REQUIRE(event.note == static_cast<int>(n));
    }
    REQUIRE_FALSE(queue.pop(event)); REQUIRE_FALSE(queue.consumeOverflow());
    for (std::size_t n = 0; n < MidiEventQueue::capacity; ++n) REQUIRE(queue.push({}));
    REQUIRE_FALSE(queue.push({})); REQUIRE(queue.consumeOverflow()); REQUIRE_FALSE(queue.consumeOverflow());
    REQUIRE_FALSE(queue.pop(event)); // stale events invalidated by overflow epoch
    REQUIRE(queue.push({MidiEvent::Type::noteOff, 60, 0, 1}));
    REQUIRE(queue.pop(event)); REQUIRE(event.note == 60);
}

TEST_CASE("MIDI queue accepts concurrent producers without torn payloads", "[harness][midi]")
{
    MidiEventQueue queue;
    constexpr int count = 80;
    bool acceptedA = true, acceptedB = true;
    std::thread a([&] { for (int n = 0; n < count; ++n) acceptedA &= queue.push({MidiEvent::Type::noteOn, n, 0.25f, static_cast<double>(n)}); });
    std::thread b([&] { for (int n = 0; n < count; ++n) acceptedB &= queue.push({MidiEvent::Type::noteOff, n, 0.75f, static_cast<double>(n)}); });
    a.join(); b.join();
    const auto overflowed = queue.consumeOverflow();
    REQUIRE(overflowed == (!acceptedA || !acceptedB));
    int seenA = 0, seenB = 0;
    int previousA = -1, previousB = -1;
    MidiEvent event;
    while (queue.pop(event))
    {
        if (event.type == MidiEvent::Type::noteOn)
        {
            REQUIRE(event.note > previousA); previousA = event.note; ++seenA; REQUIRE(event.velocity == 0.25f);
        }
        else
        {
            REQUIRE(event.type == MidiEvent::Type::noteOff);
            REQUIRE(event.note > previousB); previousB = event.note; ++seenB; REQUIRE(event.velocity == 0.75f);
        }
        REQUIRE(event.timestampSeconds == event.note);
    }
    if (!overflowed) { REQUIRE(seenA == count); REQUIRE(seenB == count); }
}
