#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "harness/OfflineRender.h"
#include "harness/LoopSource.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <thread>
#include <chrono>

using namespace disdorktion::harness;
namespace
{
struct RenderFixture
{
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("disdorktion-test-" + juce::Uuid().toString());
    RenderFixture() { REQUIRE(root.createDirectory().wasOk()); }
    ~RenderFixture() { root.deleteRecursively(); }
};
juce::AudioBuffer<float> readFloat(const juce::File& file, int samples, unsigned channels)
{
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(file.createInputStream().release(), true));
    REQUIRE(reader != nullptr); REQUIRE(reader->usesFloatingPointData); REQUIRE(reader->bitsPerSample == 32);
    REQUIRE(reader->lengthInSamples == samples); REQUIRE(reader->numChannels == channels);
    juce::AudioBuffer<float> audio(static_cast<int>(channels), samples);
    REQUIRE(reader->read(&audio, 0, samples, 0, true, true)); return audio;
}

TEST_CASE("Production offline render retains dB response before monitoring", "[render][gain]")
{
    RenderFixture fixture;
    ExperimentRecord record; record.version = 2; record.targetId = "gain";
    record.gainDb = -6.0f; record.renderDurationSamples = 31; record.source.durationSamples = 31;
    record.source.kind = SourceKind::impulse;
    const auto directory = fixture.root.getChildFile("production");
    const auto result = renderExperiment(record, directory);
    INFO(result.message); REQUIRE(result.status == RenderStatus::success);
    const auto rendered = readFloat(directory.getChildFile("rendered.wav"), 31, 2);
    REQUIRE(rendered.getSample(0, 0) == Catch::Approx(0.25 * std::pow(10.0, -6.0 / 20.0)).margin(1e-7));
    record.mute = true;
    REQUIRE(renderExperiment(record, fixture.root.getChildFile("module-muted")).status == RenderStatus::success);
    REQUIRE(readFloat(fixture.root.getChildFile("module-muted/rendered.wav"), 31, 2).getMagnitude(0, 31) == 0.0f);
}
}

TEST_CASE("Offline generated render is exact float audio independent of monitoring", "[render]")
{
    RenderFixture fixture;
    for (const auto kind : {SourceKind::impulse, SourceKind::sine, SourceKind::twoTone, SourceKind::sweep, SourceKind::noise})
    {
        ExperimentRecord record; record.source.kind = kind; record.source.durationSamples = 1031;
        record.renderDurationSamples = 1031; record.blockSize = 127; record.gain = 3.0f;
        record.source.amplitude = 0.75;
        const auto firstDir = fixture.root.getChildFile(juce::String(static_cast<int>(kind)) + "a");
        const auto secondDir = fixture.root.getChildFile(juce::String(static_cast<int>(kind)) + "b");
        const auto first = renderExperiment(record, firstDir);
        INFO(first.message); REQUIRE(first.status == RenderStatus::success);
        record.monitorDb = 0; record.muted = false;
        const auto second = renderExperiment(record, secondDir);
        REQUIRE(second.status == RenderStatus::success); REQUIRE(first.samples == 1031);
        REQUIRE(first.inputPeak == second.inputPeak); REQUIRE(first.outputRms == second.outputRms);
        const auto source = readFloat(firstDir.getChildFile("source.wav"), 1031, 2);
        const auto rendered = readFloat(firstDir.getChildFile("rendered.wav"), 1031, 2);
        const auto other = readFloat(secondDir.getChildFile("rendered.wav"), 1031, 2);
        double squares = 0, peak = 0;
        for (int c = 0; c < 2; ++c) for (int n = 0; n < 1031; ++n)
        {
            REQUIRE(rendered.getSample(c, n) == Catch::Approx(source.getSample(c, n) * 3).margin(1.0e-6));
            REQUIRE(rendered.getSample(c, n) == other.getSample(c, n));
            const double value = rendered.getSample(c, n); squares += value * value; peak = std::max(peak, std::abs(value));
        }
        REQUIRE(first.outputRms == Catch::Approx(std::sqrt(squares / 2062)).margin(1.0e-8));
        REQUIRE(first.outputPeak == peak); REQUIRE(first.overloaded == (peak > 1));
        REQUIRE(firstDir.getChildFile("result.json").existsAsFile());
    }
}

TEST_CASE("Offline loop wraps for exact requested length and hash failures publish nothing", "[render]")
{
    RenderFixture fixture;
    ExperimentRecord initial; initial.channels = 1; initial.source.kind = SourceKind::impulse;
    initial.source.durationSamples = 7; initial.renderDurationSamples = 7; initial.blockSize = 4;
    REQUIRE(renderExperiment(initial, fixture.root.getChildFile("seed")).status == RenderStatus::success);
    const auto sourceFile = fixture.root.getChildFile("seed/source.wav");
    LoopSource loop; juce::String error; REQUIRE(loop.load(sourceFile, 48000, 1, error));
    ExperimentRecord record; record.channels = 1; record.source.kind = SourceKind::file;
    record.filePath = sourceFile.getFullPathName(); record.fileHash = loop.fileHash();
    record.originalSampleRate = loop.originalSampleRate(); record.originalChannels = loop.originalChannels();
    record.renderDurationSamples = 31; record.blockSize = 8;
    const auto destination = fixture.root.getChildFile("loop");
    const auto result = renderExperiment(record, destination); INFO(result.message);
    REQUIRE(result.status == RenderStatus::success);
    const auto audio = readFloat(destination.getChildFile("rendered.wav"), 31, 1);
    for (int n = 0; n < 31; ++n) REQUIRE(audio.getSample(0, n) == (n % 7 == 0 ? 0.25f : 0.0f));
    REQUIRE(renderExperiment(record, destination).status == RenderStatus::invalidRequest);
    record.fileHash = juce::String::repeatedString("0", 64);
    const auto rejected = fixture.root.getChildFile("rejected");
    REQUIRE(renderExperiment(record, rejected).status == RenderStatus::invalidRequest); REQUIRE_FALSE(rejected.exists());
    record.source.kind = SourceKind::midi;
    REQUIRE(renderExperiment(record, rejected).status == RenderStatus::invalidRequest); REQUIRE_FALSE(rejected.exists());
    record.source.kind = SourceKind::sine;
    REQUIRE(renderExperiment(record, fixture.root.getChildFile("missing/result")).status == RenderStatus::ioFailure);
    REQUIRE_FALSE(fixture.root.getChildFile("missing/result").exists());
}

TEST_CASE("Offline cancellation never publishes a partial result", "[render]")
{
    RenderFixture fixture;
    ExperimentRecord record;
    std::atomic<bool> cancel {true};
    const auto destination = fixture.root.getChildFile("cancelled");
    auto result = renderExperiment(record, destination, &cancel);
    REQUIRE(result.status == RenderStatus::ioFailure);
    REQUIRE(result.message.contains("cancelled")); REQUIRE_FALSE(destination.exists());
    cancel.store(false);
    // Leave ample work so the main thread can observe staging and cancel an
    // active render. Every block checks the same flag; no device is involved.
    record.renderDurationSamples = 100000000;
    std::atomic<bool> finished {false};
    std::thread worker([&] { result = renderExperiment(record, destination, &cancel); finished.store(true); });
    bool staged = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!finished.load() && std::chrono::steady_clock::now() < deadline)
    {
        if (!fixture.root.findChildFiles(juce::File::findDirectories, false, ".disdorktion-render-*").isEmpty())
        { staged = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    cancel.store(true); worker.join();
    REQUIRE(staged); REQUIRE(result.status == RenderStatus::ioFailure);
    REQUIRE(result.message.contains("cancelled")); REQUIRE_FALSE(destination.exists());
    REQUIRE(fixture.root.findChildFiles(juce::File::findDirectories, false, ".disdorktion-render-*").isEmpty());
}
