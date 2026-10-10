#include "AllocationTracking.h"
#include "harness/AuditionEngine.h"
#include "harness/SignalSource.h"
#include "harness/SineVoice.h"
#include "harness/MidiEventQueue.h"
#include "harness/LoopSource.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <thread>

TEST_CASE("Crusher scalar automation remains valid during processing", "[harness][realtime][crusher]")
{
    using namespace disdorktion;
    harness::AuditionEngine engine;
    REQUIRE(engine.selectTarget("bit-crusher"));
    REQUIRE(engine.prepare({48000.0, 32, 2}));
    juce::AudioBuffer<float> audio(2, 257);
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 257; ++n) audio.setSample(c, n, static_cast<float>(n % 17) / 32.0f);
    std::atomic<bool> settersValid {true};
    std::thread worker([&] {
        for (int i = 0; i < 2000; ++i)
        {
            bool valid = engine.setBitDepth(i % 2 == 0 ? 4u : 64u);
            valid &= engine.setCaptureRateHz(i % 2 == 0 ? 100.0f : 384000.0f);
            valid &= engine.setCrusherMix(i % 2 == 0 ? 0.0f : 1.0f);
            valid &= engine.setCrush(i % 2 == 0 ? 0.75f : 0.0f);
            valid &= engine.setQuantizerShape(i % 2 == 0 ? -1.0f : 1.0f);
            valid &= engine.setJitter(i % 2 == 0 ? 1.0f : 0.0f);
            engine.setJitterSeed(static_cast<std::uint32_t>(i));
            engine.setBypass(i % 3 == 0);
            if (!valid) settersValid.store(false);
        }
    });
    bool processed = true;
    for (int i = 0; i < 256; ++i)
        processed &= engine.process(juce::dsp::AudioBlock<float>(audio), false);
    worker.join();
    REQUIRE(settersValid.load());
    REQUIRE(processed);
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 257; ++n) REQUIRE(std::isfinite(audio.getSample(c, n)));
}

TEST_CASE("Crusher harness live setters and callback processing allocate nothing", "[harness][realtime][crusher]")
{
    using namespace disdorktion;
    harness::AuditionEngine engine;
    REQUIRE(engine.selectTarget("bit-crusher"));
    REQUIRE(engine.setBitCrusherSettings({8, 12000.0f, false}));
    REQUIRE(engine.prepare({48000.0, 32, 2}));
    juce::AudioBuffer<float> audio(2, 257);
    audio.clear();
    bool success = true;
    {
        test::ScopedAllocationTracking tracking;
        for (int i = 0; i < 32; ++i)
        {
            success &= engine.setBitDepth(i % 2 == 0 ? 4u : 16u);
            success &= engine.setCaptureRateHz(i % 2 == 0 ? 6000.0f : 384000.0f);
            engine.setBypass(i % 3 == 0);
            success &= engine.process(juce::dsp::AudioBlock<float>(audio), false);
        }
        engine.reset();
    }
    const auto counts = test::getAllocationCounts();
    REQUIRE(success);
    REQUIRE(counts.allocations == 0);
    REQUIRE(counts.deallocations == 0);
}

TEST_CASE("Production harness scalar controls and processing allocate nothing", "[harness][realtime][gain]")
{
    using namespace disdorktion;
    harness::AuditionEngine engine;
    REQUIRE(engine.selectTarget("gain")); REQUIRE(engine.prepare({48000.0, 8, 2}));
    juce::AudioBuffer<float> audio(2, 257); audio.clear();
    bool success = true;
    {
        test::ScopedAllocationTracking tracking;
        for (int i = 0; i < 32; ++i)
        {
            success &= engine.setGainDb(i % 2 == 0 ? -60.0f : 24.0f);
            engine.setModuleMuted(i % 3 == 0); engine.setBypass(i % 4 == 0);
            success &= !engine.setGainDb(std::numeric_limits<float>::quiet_NaN());
            success &= engine.process(juce::dsp::AudioBlock<float>(audio), false);
        }
        engine.reset();
    }
    const auto counts = test::getAllocationCounts();
    REQUIRE(success); REQUIRE(counts.allocations == 0); REQUIRE(counts.deallocations == 0);
}

TEST_CASE("Harness processing controls monitoring and failure paths allocate nothing", "[harness][realtime]")
{
    using namespace disdorktion;
    harness::AuditionEngine mono, stereo, unprepared;
    REQUIRE(mono.prepare({48000.0,8,1})); REQUIRE(stereo.prepare({48000.0,8,2}));
    juce::AudioBuffer<float> one(1,17),two(2,17);
    one.clear(); two.clear();
    auto a = juce::dsp::AudioBlock<float>(one), b = juce::dsp::AudioBlock<float>(two);
    bool success = true;
    {
        test::ScopedAllocationTracking tracking;
        for (int i = 0; i < 64; ++i)
        {
            success &= mono.setGain(i % 2 == 0 ? 0.0f : 4.0f);
            mono.setBypass(i % 3 == 0); mono.setMonitorDb(-static_cast<float>(i%61)); mono.setMuted(i%2==0);
            success &= !mono.setGain(std::numeric_limits<float>::quiet_NaN());
            success &= mono.process(a.getSubBlock(0,0));
            success &= mono.process(a);
            success &= stereo.process(b);
            success &= !mono.process(b);
            success &= !unprepared.process(a);
            (void) mono.meters(); (void) mono.consumeOverload();
        }
        mono.reset(); stereo.reset();
    }
    const auto counts = test::getAllocationCounts();
    REQUIRE(success); REQUIRE(counts.allocations == 0); REQUIRE(counts.deallocations == 0);
}

TEST_CASE("Harness generated sources voice and bounded MIDI queue allocate nothing", "[harness][realtime]")
{
    using namespace disdorktion;
    using namespace harness;
    for (const auto kind : {SourceKind::impulse, SourceKind::sine, SourceKind::twoTone, SourceKind::sweep, SourceKind::noise})
    {
        SignalSource source;
        SourceSettings settings;
        settings.kind = kind; settings.durationSamples = 2048;
        REQUIRE(source.prepare(settings,48000.0,2));
        SineVoice voice;
        REQUIRE(voice.prepare(48000.0));
        MidiEventQueue queue;
        juce::AudioBuffer<float> audio(2,257); audio.clear();
        auto block = juce::dsp::AudioBlock<float>(audio);
        bool success = true, overflow = false;
        float accumulated = 0.0f;
        {
            test::ScopedAllocationTracking tracking;
            source.reset(); source.process(block.getSubBlock(0,0));
            for (int i = 0; i < 12; ++i) source.process(block);
            for (std::size_t i = 0; i < MidiEventQueue::capacity; ++i)
                success &= queue.push({MidiEvent::Type::noteOn,60+static_cast<int>(i%12),0.5f,0.0});
            success &= !queue.push({});
            overflow = queue.consumeOverflow();
            MidiEvent event;
            // Overflow invalidates all previously queued reservations.
            success &= !queue.pop(event);
            for (std::size_t i = 0; i < MidiEventQueue::capacity; ++i)
                success &= queue.push({MidiEvent::Type::noteOn,60+static_cast<int>(i%12),0.5f,0.0});
            for (std::size_t i = 0; i < MidiEventQueue::capacity; ++i)
            {
                success &= queue.pop(event);
                voice.noteOn(event.note,event.velocity);
                for (int n = 0; n < 3; ++n) accumulated += voice.nextSample();
                voice.noteOff(event.note);
            }
            success &= !queue.pop(event);
            voice.allNotesOff();
            for (int n = 0; n < 512; ++n) accumulated += voice.nextSample();
            voice.reset();
        }
        const auto counts = test::getAllocationCounts();
        REQUIRE(success); REQUIRE(overflow); REQUIRE(std::isfinite(accumulated));
        REQUIRE(counts.allocations == 0); REQUIRE(counts.deallocations == 0);
    }
}

TEST_CASE("Harness resident loop wrapping allocates nothing", "[harness][realtime]")
{
    using namespace disdorktion;
    juce::TemporaryFile file(".wav");
    juce::AudioBuffer<float> fixture(1,3);
    fixture.setSample(0,0,0.25f); fixture.setSample(0,1,-0.5f); fixture.setSample(0,2,0.125f);
    {
        std::unique_ptr<juce::OutputStream> stream = file.getFile().createOutputStream();
        juce::WavAudioFormat format;
        auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
            .withSampleRate(48000.0).withNumChannels(1).withBitsPerSample(32)
            .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        REQUIRE(writer != nullptr);
        REQUIRE(writer->writeFromAudioSampleBuffer(fixture,0,3));
    }
    harness::LoopSource loop;
    juce::String error;
    REQUIRE(loop.load(file.getFile(),48000.0,2,error));
    juce::AudioBuffer<float> audio(2,257);
    auto block = juce::dsp::AudioBlock<float>(audio);
    {
        test::ScopedAllocationTracking tracking;
        loop.reset(); loop.process(block.getSubBlock(0,0));
        for (int i = 0; i < 32; ++i) loop.process(block);
    }
    const auto counts = test::getAllocationCounts();
    REQUIRE(counts.allocations == 0); REQUIRE(counts.deallocations == 0);
    REQUIRE(audio.getSample(0,0) == audio.getSample(1,0));
    REQUIRE(audio.getMagnitude(0,257) == 0.5f);
}
