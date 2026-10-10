#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "harness/LoopSource.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <array>
#include <limits>

using namespace disdorktion::harness;
namespace
{
struct TempAudio
{
    juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("disdorktion-loop-test-" + juce::Uuid().toString() + ".wav");
    ~TempAudio() { file.deleteFile(); }
};
void writeAudio(const juce::File& file, const juce::AudioBuffer<float>& buffer, double rate, bool aiff = false)
{
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    REQUIRE(stream != nullptr);
    const auto options = juce::AudioFormatWriterOptions().withSampleRate(rate).withNumChannels(buffer.getNumChannels())
        .withBitsPerSample(aiff ? 24 : 32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    std::unique_ptr<juce::AudioFormatWriter> writer;
    if (aiff) { juce::AiffAudioFormat format; writer = format.createWriterFor(stream, options); }
    else { juce::WavAudioFormat format; writer = format.createWriterFor(stream, options); }
    REQUIRE(writer != nullptr); REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()));
}
}

TEST_CASE("Loop playback wraps exactly and caches original metadata", "[harness][loop]")
{
    TempAudio fixture;
    juce::AudioBuffer<float> input(1, 3);
    input.setSample(0, 0, 0.25f); input.setSample(0, 1, -0.5f); input.setSample(0, 2, 0.75f);
    writeAudio(fixture.file, input, 48000);
    LoopSource loop; juce::String error;
    REQUIRE(loop.load(fixture.file, 48000, 2, error));
    REQUIRE(loop.lengthSamples() == 3); REQUIRE(loop.originalSampleRate() == 48000);
    REQUIRE(loop.originalChannels() == 1); REQUIRE(loop.path() == fixture.file.getFullPathName());
    REQUIRE(loop.fileHash() == juce::SHA256(fixture.file).toHexString());
    REQUIRE(loop.fileHash().length() == 64);
    juce::AudioBuffer<float> first(2, 7), second(2, 5);
    loop.process(juce::dsp::AudioBlock<float>(first)); loop.process(juce::dsp::AudioBlock<float>(second));
    for (int c = 0; c < 2; ++c)
    {
        for (int n = 0; n < 7; ++n) REQUIRE(first.getSample(c, n) == input.getSample(0, n % 3));
        for (int n = 0; n < 5; ++n) REQUIRE(second.getSample(c, n) == input.getSample(0, (n + 7) % 3));
    }
    loop.reset(); loop.process(juce::dsp::AudioBlock<float>(second));
    REQUIRE(second.getSample(0, 0) == 0.25f);
}

TEST_CASE("Loop conversion averages stereo and shares deterministic resampling", "[harness][loop]")
{
    TempAudio fixture;
    juce::AudioBuffer<float> input(2, 3);
    for (int n = 0; n < 3; ++n)
    {
        input.setSample(0, n, static_cast<float>(n) * 0.25f);
        input.setSample(1, n, static_cast<float>(n) * 0.5f);
    }
    writeAudio(fixture.file, input, 24000);
    LoopSource first, second; juce::String error;
    REQUIRE(first.load(fixture.file, 48000, 1, error)); REQUIRE(second.load(fixture.file, 48000, 1, error));
    REQUIRE(first.lengthSamples() == 6);
    juce::AudioBuffer<float> whole(1, 17), partitioned(1, 17);
    first.process(juce::dsp::AudioBlock<float>(whole));
    auto block = juce::dsp::AudioBlock<float>(partitioned);
    second.process(block.getSubBlock(0, 1)); second.process(block.getSubBlock(1, 9)); second.process(block.getSubBlock(10, 7));
    const std::array<float, 6> expected {0.0f, 0.1875f, 0.375f, 0.5625f, 0.75f, 0.75f};
    for (int n = 0; n < 17; ++n)
    {
        REQUIRE(whole.getSample(0, n) == expected[static_cast<std::size_t>(n % 6)]);
        REQUIRE(partitioned.getSample(0, n) == whole.getSample(0, n));
    }
    REQUIRE(second.load(fixture.file, 12000, 2, error)); REQUIRE(second.lengthSamples() == 2);
}

TEST_CASE("Loop cancellation and malformed files retain last valid source", "[harness][loop]")
{
    TempAudio fixture, corrupt;
    REQUIRE(fixture.file != corrupt.file);
    juce::AudioBuffer<float> input(1, 1); input.setSample(0, 0, 0.25f);
    writeAudio(fixture.file, input, 48000);
    REQUIRE(corrupt.file.replaceWithText("not audio"));
    LoopSource loop; juce::String error;
    REQUIRE(loop.load(fixture.file, 48000, 1, error));
    const auto hash = loop.fileHash();
    REQUIRE_FALSE(loop.load(corrupt.file, 48000, 1, error)); REQUIRE_FALSE(error.isEmpty());
    std::atomic<bool> cancel { true };
    REQUIRE_FALSE(loop.load(fixture.file, 48000, 1, error, &cancel));
    REQUIRE_FALSE(loop.load(fixture.file, 0, 1, error));
    REQUIRE_FALSE(loop.load(fixture.file, 48000, 3, error));
    REQUIRE(loop.fileHash() == hash); REQUIRE(loop.lengthSamples() == 1);
    juce::AudioBuffer<float> output(1, 3); loop.process(juce::dsp::AudioBlock<float>(output));
    for (int n = 0; n < 3; ++n) REQUIRE(output.getSample(0, n) == 0.25f);
    TempAudio nonfinite;
    input.setSample(0, 0, std::numeric_limits<float>::quiet_NaN());
    writeAudio(nonfinite.file, input, 48000);
    REQUIRE_FALSE(loop.load(nonfinite.file, 48000, 1, error));
    REQUIRE(loop.fileHash() == hash);
}

TEST_CASE("AIFF loops decode and preserve channel metadata", "[harness][loop]")
{
    TempAudio fixture; fixture.file = fixture.file.withFileExtension("aiff");
    juce::AudioBuffer<float> input(2, 2); input.clear();
    input.setSample(0, 0, 0.25f); input.setSample(1, 1, -0.5f);
    writeAudio(fixture.file, input, 44100, true);
    LoopSource loop; juce::String error;
    REQUIRE(loop.load(fixture.file, 44100, 2, error)); REQUIRE(loop.originalChannels() == 2);
    juce::AudioBuffer<float> output(2, 2); loop.process(juce::dsp::AudioBlock<float>(output));
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 2; ++n) REQUIRE(output.getSample(c, n) == Catch::Approx(input.getSample(c, n)).margin(1e-6));
}

TEST_CASE("Loop memory budget includes external and prior resident PCM without overflow", "[harness][loop]")
{
    TempAudio fixture;
    juce::AudioBuffer<float> input(1, 1); input.setSample(0, 0, 0.25f);
    writeAudio(fixture.file, input, 48000);
    LoopSource loop; juce::String error;
    REQUIRE(loop.residentMemoryBytes() == 0);
    REQUIRE(loop.load(fixture.file, 48000, 1, error));
    REQUIRE(loop.residentMemoryBytes() == 4);
    const auto hash = loop.fileHash();
    // Four bytes each: previous loop, decoded source, converted replacement.
    REQUIRE_FALSE(loop.load(fixture.file, 48000, 1, error, nullptr, LoopSource::memoryLimitBytes - 11));
    REQUIRE(error.contains("memory limit"));
    REQUIRE(loop.fileHash() == hash); REQUIRE(loop.residentMemoryBytes() == 4);
    REQUIRE_FALSE(loop.load(fixture.file, 48000, 1, error, nullptr, std::numeric_limits<std::uint64_t>::max()));
    REQUIRE(loop.fileHash() == hash);
    REQUIRE(loop.load(fixture.file, 48000, 1, error, nullptr, LoopSource::memoryLimitBytes - 12));
    LoopSource replacement;
    REQUIRE_FALSE(replacement.load(fixture.file, 48000, 1, error, nullptr, LoopSource::memoryLimitBytes - 7));
    REQUIRE(replacement.residentMemoryBytes() == 0);
    REQUIRE(replacement.load(fixture.file, 48000, 1, error, nullptr, LoopSource::memoryLimitBytes - 8));
    juce::AudioBuffer<float> output(1, 3); loop.process(juce::dsp::AudioBlock<float>(output));
    for (int n = 0; n < 3; ++n) REQUIRE(output.getSample(0, n) == 0.25f);
}
