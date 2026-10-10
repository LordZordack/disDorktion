#include "dsp/Gain.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
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

namespace
{
constexpr int sampleRate = 48000;
constexpr int channels = 2;
constexpr int frames = sampleRate * 4;
constexpr int blockCapacity = 127;
constexpr double amplitude = 0.025;
struct Event
{
    int frame;
    disdorktion::GainSettings settings;
};
constexpr std::array<Event, 8> events {{
    {0, {0.0f, false, false}},
    {24000, {24.0f, false, false}},
    {48000, {-60.0f, false, false}},
    {72000, {-60.0f, true, false}},
    {96000, {24.0f, false, false}},
    {120000, {24.0f, false, true}},
    {144000, {24.0f, false, false}},
    {168000, {-12.0f, false, false}}
}};

enum class Source { sine, transients };
struct Render
{
    juce::AudioBuffer<float> audio {channels, frames};
    double sourcePeak = 0.0;
    double outputPeak = 0.0;
    unsigned int latency = 0;
};

bool render(Source source, Render& result, juce::String& error)
{
    disdorktion::Gain gain;
    if (!gain.prepare({static_cast<double>(sampleRate), blockCapacity, channels})
        || !gain.setParameters(events.front().settings))
    { error = "Cannot prepare production Gain"; return false; }
    gain.reset(); // Reset only at the beginning; event ramps retain their history.
    result.latency = gain.getLatencySamples();
    for (int frame = 0; frame < frames; ++frame)
    {
        const double phase = 2.0 * juce::MathConstants<double>::pi;
        const int pulseFrame = frame % 6000;
        const double sample = source == Source::sine
            ? amplitude * std::sin(phase * 440.0 * frame / sampleRate)
            : amplitude * std::exp(-static_cast<double>(pulseFrame) / 360.0)
                * std::cos(phase * 180.0 * pulseFrame / sampleRate);
        const auto value = static_cast<float>(sample);
        result.sourcePeak = std::max(result.sourcePeak, std::abs(static_cast<double>(value)));
        for (int channel = 0; channel < channels; ++channel)
            result.audio.setSample(channel, frame, value);
    }
    const juce::ScopedNoDenormals noDenormals;
    auto audio = juce::dsp::AudioBlock<float>(result.audio);
    std::size_t nextEvent = 1;
    for (int blockStart = 0; blockStart < frames; blockStart += blockCapacity)
    {
        const int blockEnd = std::min(blockStart + blockCapacity, frames);
        for (int offset = blockStart; offset < blockEnd;)
        {
            if (nextEvent < events.size() && events[nextEvent].frame == offset)
            {
                if (!gain.setParameters(events[nextEvent].settings))
                { error = "Gain rejected a scheduled control event"; return false; }
                ++nextEvent;
            }
            const int end = nextEvent < events.size()
                ? std::min(blockEnd, events[nextEvent].frame) : blockEnd;
            if (!gain.process(audio.getSubBlock(static_cast<std::size_t>(offset),
                                               static_cast<std::size_t>(end - offset))))
            { error = "Gain processing failed"; return false; }
            offset = end;
        }
    }
    for (int channel = 0; channel < channels; ++channel)
        for (int frame = 0; frame < frames; ++frame)
        {
            const double value = result.audio.getSample(channel, frame);
            if (!std::isfinite(value))
            { error = "Non-finite fixture output"; return false; }
            result.outputPeak = std::max(result.outputPeak, std::abs(value));
        }
    if (result.outputPeak > 1.0)
    { error = "Fixture exceeds unity peak; refusing to write audio"; return false; }
    return true;
}

bool deterministic(const Render& first, const Render& second)
{
    if (first.latency != second.latency || first.outputPeak != second.outputPeak
        || first.sourcePeak != second.sourcePeak) return false;
    for (int channel = 0; channel < channels; ++channel)
        for (int frame = 0; frame < frames; ++frame)
            if (first.audio.getSample(channel, frame) != second.audio.getSample(channel, frame))
                return false;
    return true;
}

bool writeWav(const juce::File& file, const Render& rendered)
{
    if (file.exists()) return false;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (!stream) return false;
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(sampleRate).withNumChannels(channels).withBitsPerSample(32)
        .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    return writer && writer->writeFromAudioSampleBuffer(rendered.audio, 0, frames) && writer->flush();
}

juce::var settingsJson(const Render& sine, const Render& transients)
{
    auto* object = new juce::DynamicObject;
    juce::var result(object);
    object->setProperty("version", 1);
    object->setProperty("target", "gain");
    object->setProperty("sampleRate", sampleRate);
    object->setProperty("channels", channels);
    object->setProperty("durationSamples", frames);
    object->setProperty("durationSeconds", 4.0);
    object->setProperty("blockCapacity", blockCapacity);
    object->setProperty("blockSchedule", "127-frame blocks, split at exact event frames; final partial block");
    object->setProperty("sampleFormat", "WAV 32-bit IEEE float");
    object->setProperty("initialResetOnly", true);
    object->setProperty("deterministicRerenderVerified", true);
    object->setProperty("finiteOutputVerified", true);
    object->setProperty("listeningStatus", "Not listened to; these files are fixtures for a human listening check");
    juce::Array<juce::var> controls;
    for (const auto& event : events)
    {
        auto* control = new juce::DynamicObject;
        juce::var value(control);
        control->setProperty("frame", event.frame);
        control->setProperty("seconds", static_cast<double>(event.frame) / sampleRate);
        control->setProperty("gainDb", event.settings.gainDb);
        control->setProperty("mute", event.settings.mute);
        control->setProperty("bypass", event.settings.bypass);
        controls.add(value);
    }
    object->setProperty("events", juce::var(controls));
    juce::Array<juce::var> sources;
    const auto addSource = [&](const char* file, const char* formula, const Render& render)
    {
        auto* entry = new juce::DynamicObject;
        juce::var value(entry);
        entry->setProperty("file", file);
        entry->setProperty("provenance", "Original deterministic synthesis in tools/GainTransitionMain.cpp; no external recording");
        entry->setProperty("formula", formula);
        entry->setProperty("sourceAmplitude", amplitude);
        entry->setProperty("stereo", "Identical source samples in both channels");
        entry->setProperty("inputPeak", render.sourcePeak);
        entry->setProperty("outputPeak", render.outputPeak);
        entry->setProperty("latencySamples", static_cast<int>(render.latency));
        sources.add(value);
    };
    addSource("gain-sine.wav", "0.025 * sin(2*pi*440*n/48000)", sine);
    addSource("gain-transients.wav", "p=n%6000; 0.025 * exp(-p/360) * cos(2*pi*180*p/48000)", transients);
    object->setProperty("sources", juce::var(sources));
    return result;
}

struct Staging
{
    juce::File directory;
    bool owned = false;
    bool published = false;
    ~Staging()
    {
        if (!owned || published) return;
        // Only delete the three owned files and then the empty directory. Never
        // recursively delete a supplied path or unexpected directory contents.
        for (const auto* name : {"gain-sine.wav", "gain-transients.wav", "settings.json"})
            directory.getChildFile(name).deleteFile();
        directory.deleteFile();
    }
};

bool publish(const juce::File& staging, const juce::File& destination)
{
    // Native no-replace rename also rejects an output created during rendering.
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

bool createStaging(const juce::File& directory)
{
    std::error_code error;
#if JUCE_WINDOWS
    const std::filesystem::path path(directory.getFullPathName().toWideCharPointer());
#else
    const std::filesystem::path path(directory.getFullPathName().toStdString());
#endif
    // Unlike createDirectory, this returns false if another actor already owns it.
    return std::filesystem::create_directory(path, error) && !error;
}
}

int main(int argc, char* argv[])
{
    const auto usage = []
    {
        std::cout << "Usage: disdorktion_gain_transitions --output-dir <fresh-directory>\n"
                     "Writes two four-second stereo gain transition fixtures and settings.json.\n";
    };
    if (argc == 2 && juce::String(argv[1]) == "--help") { usage(); return 0; }
    if (argc != 3 || juce::String(argv[1]) != "--output-dir"
        || juce::String(argv[2]).isEmpty() || juce::String(argv[2]).startsWith("--"))
    { usage(); return 2; }
    const juce::String path(argv[2]);
    const auto destination = juce::File::isAbsolutePath(path) ? juce::File(path)
        : juce::File::getCurrentWorkingDirectory().getChildFile(path);
    const auto parent = destination.getParentDirectory();
    if (destination.exists() || !parent.isDirectory())
    { std::cerr << "Output must be a fresh directory with an existing parent\n"; return 2; }
    Render sine, transients;
    juce::String error;
    for (const auto source : {Source::sine, Source::transients})
    {
        auto& output = source == Source::sine ? sine : transients;
        Render repeated;
        if (!render(source, output, error) || !render(source, repeated, error))
        { std::cerr << error << '\n'; return 1; }
        if (!deterministic(output, repeated))
        { std::cerr << "Fixture render is not deterministic\n"; return 1; }
    }
    Staging staging {parent.getChildFile(".disdorktion-gain-transitions-" + juce::Uuid().toString())};
    if (!createStaging(staging.directory))
    { std::cerr << "Cannot create a fresh staging directory\n"; return 1; }
    staging.owned = true;
    if (!writeWav(staging.directory.getChildFile("gain-sine.wav"), sine)
        || !writeWav(staging.directory.getChildFile("gain-transients.wav"), transients))
    { std::cerr << "Cannot write fixture WAV files\n"; return 1; }
    const auto jsonFile = staging.directory.getChildFile("settings.json");
    if (jsonFile.exists()) { std::cerr << "Settings destination already exists\n"; return 1; }
    auto jsonStream = jsonFile.createOutputStream();
    if (!jsonStream || !jsonStream->writeText(juce::JSON::toString(settingsJson(sine, transients)), false, false, "\n"))
    { std::cerr << "Cannot write fixture settings\n"; return 1; }
    jsonStream->flush();
    if (jsonStream->getStatus().failed())
    { std::cerr << "Cannot finalize fixture settings\n"; return 1; }
    jsonStream.reset();
    if (!publish(staging.directory, destination))
    { std::cerr << "Cannot publish fixtures; output may already exist\n"; return 1; }
    staging.published = true;
    std::cout << "Generated fixtures: " << destination.getFullPathName()
              << "\nSine peak: " << sine.outputPeak << "; transient peak: " << transients.outputPeak
              << "\nListening observations have not been collected.\n";
    return 0;
}
