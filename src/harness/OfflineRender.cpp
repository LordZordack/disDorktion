#include "OfflineRender.h"
#include "AuditionEngine.h"
#include "LoopSource.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <memory>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#elif JUCE_LINUX
 #include <fcntl.h>
 #include <linux/fs.h>
 #include <sys/syscall.h>
 #include <unistd.h>
#elif JUCE_MAC
 #include <stdio.h>
#endif

namespace disdorktion::harness
{
namespace
{
struct Staging
{
    juce::File directory;
    bool owned = false;
    bool published = false;
    ~Staging() { if (owned && !published) directory.deleteRecursively(); }
};
bool publish(const juce::File& staging, const juce::File& destination)
{
    // Native atomic, no-replace directory rename. JUCE moveFileTo deletes its
    // destination first and would violate the fresh-output policy during a race.
#if JUCE_WINDOWS
    return MoveFileW(staging.getFullPathName().toWideCharPointer(), destination.getFullPathName().toWideCharPointer()) != 0;
#elif JUCE_LINUX
    return syscall(SYS_renameat2, AT_FDCWD, staging.getFullPathName().toRawUTF8(),
                   AT_FDCWD, destination.getFullPathName().toRawUTF8(), RENAME_NOREPLACE) == 0;
#elif JUCE_MAC
    return renamex_np(staging.getFullPathName().toRawUTF8(), destination.getFullPathName().toRawUTF8(), RENAME_EXCL) == 0;
#else
    juce::ignoreUnused(staging, destination);
    return false;
#endif
}
struct Accumulator
{
    double peak = 0, squares = 0;
    void add(const juce::AudioBuffer<float>& audio, int frames)
    {
        for (int channel = 0; channel < audio.getNumChannels(); ++channel)
            for (int frame = 0; frame < frames; ++frame)
            {
                const double sample = audio.getSample(channel, frame);
                peak = std::max(peak, std::abs(sample)); squares += sample * sample;
            }
    }
};
std::unique_ptr<juce::AudioFormatWriter> writer(const juce::File& file, const ExperimentRecord& record)
{
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (!stream) return {};
    juce::WavAudioFormat format;
    return format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(record.sampleRate).withNumChannels(static_cast<int>(record.channels))
        .withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
}
}

RenderResult renderExperiment(const ExperimentRecord& record, const juce::File& outputDirectory,
                              const std::atomic<bool>* cancel)
{
    RenderResult result;
    const auto cancelled = [&]
    {
        if (cancel == nullptr || !cancel->load(std::memory_order_relaxed)) return false;
        result.status = RenderStatus::ioFailure;
        result.message = "Offline render cancelled";
        return true;
    };
    if (cancelled()) return result;
    if (!record.validate(result.message)) return result;
    if (record.source.kind == SourceKind::midi) { result.message = "Live MIDI is not a deterministic offline source"; return result; }
    // Conservative RIFF budget includes headers; validated counts also fit exact JSON integers.
    if (record.renderDurationSamples > (0xffffffffULL - 4096) / (4 * record.channels))
    { result.message = "Render exceeds float WAV RIFF size limit"; return result; }
    if (outputDirectory.exists()) { result.message = "Output destination already exists"; return result; }
    SignalSource generated;
    LoopSource loop;
    const bool fileSource = record.source.kind == SourceKind::file;
    if (fileSource)
    {
        if (!loop.load(juce::File(record.filePath), record.sampleRate, record.channels, result.message, cancel))
        { (void) cancelled(); return result; }
        if (cancelled()) return result;
        if (loop.fileHash() != record.fileHash) { result.message = "Source file content hash changed"; return result; }
        if (loop.originalSampleRate() != record.originalSampleRate || loop.originalChannels() != record.originalChannels)
        { result.message = "Source file original format changed"; return result; }
        loop.reset();
    }
    else if (!generated.prepare(record.source, record.sampleRate, record.channels))
    { result.message = "Cannot prepare source"; return result; }
    AuditionEngine engine;
    if (!engine.selectTarget(record.targetId.toStdString())
        || !(record.version == 1 ? engine.setParameters({record.gain, record.bypass})
                                 : engine.setGainSettings({record.gainDb, record.mute, record.bypass}))
        || !engine.prepare({record.sampleRate, record.blockSize, record.channels}))
    { result.message = "Cannot prepare target"; return result; }
    engine.reset();
    result.latencySamples = engine.getLatencySamples();
    result.status = RenderStatus::ioFailure;
    if (cancelled()) return result;
    const auto parent = outputDirectory.getParentDirectory();
    if (!parent.isDirectory()) { result.message = "Output parent directory does not exist"; return result; }
    Staging staging { parent.getChildFile(".disdorktion-render-" + juce::Uuid().toString()) };
    if (staging.directory.exists()) { result.message = "Staging directory collision"; return result; }
    if (staging.directory.createDirectory().failed()) { result.message = "Cannot create staging directory"; return result; }
    staging.owned = true;
    auto sourceWriter = writer(staging.directory.getChildFile("source.wav"), record);
    auto renderedWriter = writer(staging.directory.getChildFile("rendered.wav"), record);
    if (!sourceWriter || !renderedWriter) { result.message = "Cannot create float WAV writers"; return result; }
    juce::AudioBuffer<float> audio(static_cast<int>(record.channels), static_cast<int>(record.blockSize));
    Accumulator input, output;
    const juce::ScopedNoDenormals noDenormals;
    for (std::uint64_t offset = 0; offset < record.renderDurationSamples;)
    {
        if (cancelled()) return result;
        const auto frames = static_cast<int>(std::min<std::uint64_t>(record.blockSize, record.renderDurationSamples - offset));
        auto block = juce::dsp::AudioBlock<float>(audio).getSubBlock(0, static_cast<std::size_t>(frames));
        if (fileSource) loop.process(block); else generated.process(block);
        input.add(audio, frames);
        if (!sourceWriter->writeFromAudioSampleBuffer(audio, 0, frames))
        { result.message = "Source WAV write failed"; return result; }
        if (!engine.process(block, false)) { result.message = "Target processing failed"; return result; }
        output.add(audio, frames);
        if (!renderedWriter->writeFromAudioSampleBuffer(audio, 0, frames))
        { result.message = "Rendered WAV write failed"; return result; }
        offset += static_cast<std::uint64_t>(frames);
    }
    if (cancelled()) return result;
    if (!sourceWriter->flush() || !renderedWriter->flush()) { result.message = "WAV finalization failed"; return result; }
    sourceWriter.reset(); renderedWriter.reset();
    result.samples = record.renderDurationSamples;
    result.inputPeak = input.peak; result.outputPeak = output.peak;
    const auto values = static_cast<double>(result.samples) * record.channels;
    result.inputRms = std::sqrt(input.squares / values); result.outputRms = std::sqrt(output.squares / values);
    result.overloaded = output.peak > 1.0;
    auto* object = new juce::DynamicObject;
    juce::var json(object);
    object->setProperty("version", 1); object->setProperty("samples", static_cast<juce::int64>(result.samples));
    object->setProperty("inputPeak", result.inputPeak); object->setProperty("inputRms", result.inputRms);
    object->setProperty("outputPeak", result.outputPeak); object->setProperty("outputRms", result.outputRms);
    object->setProperty("overloaded", result.overloaded); object->setProperty("latencySamples", static_cast<int>(result.latencySamples));
    object->setProperty("experiment", record.toJson());
    object->setProperty("rendererBuildIdentity", ExperimentRecord{}.buildIdentity);
    if (!staging.directory.getChildFile("result.json").replaceWithText(juce::JSON::toString(json)))
    { result.message = "Result JSON write failed"; return result; }
    if (cancelled()) return result;
    if (!publish(staging.directory, outputDirectory))
    { result.message = "Cannot publish complete result directory"; return result; }
    staging.published = true; result.status = RenderStatus::success; result.message.clear();
    return result;
}
}
