#include "dsp/MiddleSection.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
juce::File resolve(const char* path)
{
    return juce::File::isAbsolutePath(path) ? juce::File(path)
        : juce::File::getCurrentWorkingDirectory().getChildFile(path);
}
double rms(const juce::AudioBuffer<float>& audio)
{
    double energy = 0;
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int n = 0; n < audio.getNumSamples(); ++n)
        {
            const double x = audio.getSample(c, n);
            require(std::isfinite(x), "Nonfinite audio");
            energy += x*x;
        }
    return std::sqrt(energy/(audio.getNumChannels()*static_cast<double>(audio.getNumSamples())));
}
struct Render
{
    juce::String name;
    juce::AudioBuffer<float> audio;
    unsigned latency = 0;
    double unmatchedRms = 0, appliedGain = 1;
    float tanhDriveDb = 0, folderDriveDb = 0, threshold = 1;
};
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: disdorktion_middle_listening SOURCE_WAV NEW_OUTPUT_DIRECTORY\n";
        return 2;
    }
    const auto input = resolve(argv[1]), output = resolve(argv[2]);
    if (output.exists()) { std::cerr << "Refusing to overwrite existing output\n"; return 2; }
    try
    {
        juce::WavAudioFormat format;
        auto inputStream = input.createInputStream();
        require(inputStream != nullptr, "Cannot open input WAV");
        const auto sourceHash = juce::SHA256(*inputStream).toHexString();
        require(inputStream->setPosition(0), "Cannot rewind input WAV");
        std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(inputStream.release(), true));
        require(reader != nullptr, "Cannot read input WAV");
        require(reader->numChannels >= 1 && reader->numChannels <= 2, "Input must be mono or stereo");
        require(reader->lengthInSamples > 0 && std::isfinite(reader->sampleRate) && reader->sampleRate >= 8000 && reader->sampleRate <= 384000,
                "Unsupported input duration or sample rate");
        // A bounded listening excerpt; retain the original sample rate and level.
        const auto count = static_cast<int>(std::min(reader->lengthInSamples,
            static_cast<juce::int64>(30*reader->sampleRate)));
        const auto channels = static_cast<int>(reader->numChannels);
        juce::AudioBuffer<float> source(channels, count);
        require(reader->read(&source, 0, count, 0, true, true), "Input read failed");
        require(rms(source) > 1e-9, "Input is silent");
        for (int c = 0; c < channels; ++c)
            require(source.getMagnitude(c, 0, count) <= 1.f, "Input exceeds nominal [-1,1] domain");
        std::vector<Render> renders;
        renders.push_back({"reference", source, 0});
        const juce::ScopedNoDenormals noDenormals;
        struct Recipe { const char* name; float tanhDriveDb, folderDriveDb, threshold; };
        constexpr std::array<Recipe,4> recipes {{{"mild",0,0,.75f}, {"moderate",6,0,.5f},
            {"stress-12",12,12,.25f}, {"stress-36",36,36,.25f}}};
        for (const auto& recipe : recipes) for (int option = 0; option < 3; ++option)
        {
            const bool adaa = option != 0;
            const unsigned factor = option == 2 ? 8 : 4;
            disdorktion::MiddleSection middle;
            require(middle.setParameters({{recipe.tanhDriveDb,0,false},
                {recipe.folderDriveDb,recipe.threshold,0,false}}), "Settings rejected");
            require(middle.prepare({reader->sampleRate,127,reader->numChannels},factor,
                disdorktion::MiddleLatencyCorrection::fir, adaa ? disdorktion::MiddleFoldAlgorithm::adaaTriangle
                    : disdorktion::MiddleFoldAlgorithm::triangle), "Preparation rejected");
            const auto latency = middle.getLatencySamples();
            juce::AudioBuffer<float> working(channels,count+static_cast<int>(latency));
            working.clear();
            for (int c = 0; c < channels; ++c) working.copyFrom(c,0,source,c,0,count);
            require(middle.process(juce::dsp::AudioBlock<float>(working)), "Processing failed");
            Render render {juce::String(recipe.name)+"-"+(adaa ? "adaa" : "triangle")+"-"
                +juce::String(factor)+"x", juce::AudioBuffer<float>(channels,count), latency};
            render.tanhDriveDb = recipe.tanhDriveDb;
            render.folderDriveDb = recipe.folderDriveDb;
            render.threshold = recipe.threshold;
            for (int c = 0; c < channels; ++c) render.audio.copyFrom(c,0,working,c,static_cast<int>(latency),count);
            renders.push_back(std::move(render));
        }
        double peak = 0;
        for (auto& render : renders)
        {
            const auto level = rms(render.audio);
            require(level > 1e-9, "Silent rendered output");
            render.unmatchedRms = level;
            render.appliedGain = .1/level;
            render.audio.applyGain(static_cast<float>(.1/level));
            peak = std::max(peak,static_cast<double>(render.audio.getMagnitude(0,count)));
        }
        const auto commonScale = std::min(1.,.8/peak);
        require(output.createDirectory().wasOk(), "Output directory creation failed");
        auto* report = new juce::DynamicObject;
        const juce::var reportValue(report);
        report->setProperty("sourceFile",input.getFileName());
        report->setProperty("sourceSha256",sourceHash);
        report->setProperty("sourceBytes",input.getSize());
        report->setProperty("buildId",DISDORKTION_BUILD_ID);
        report->setProperty("gitRevision",DISDORKTION_GIT_REVISION);
        report->setProperty("workingTreeEvaluation",true);
        report->setProperty("juceVersion",juce::SystemStats::getJUCEVersion());
        report->setProperty("createdUtc",juce::Time::getCurrentTime().toISO8601(true));
        report->setProperty("sampleRate",reader->sampleRate);
        report->setProperty("channels",channels);
        report->setProperty("frames",count);
        report->setProperty("sourceFrames",reader->lengthInSamples);
        report->setProperty("excerptStartSeconds",0);
        report->setProperty("commonPeakScale",commonScale);
        report->setProperty("protocol","Cascade; mild/moderate/stress recipes with per-file parameters; integer-delay crop; whole-excerpt RMS matched within 0.001 dB including reference; common peak limit 0.8. RMS is not perceptual loudness.");
        report->setProperty("listeningStatus","Pending human comparison; no quality acceptance inferred.");
        juce::Array<juce::var> files;
        for (auto& render : renders)
        {
            render.audio.applyGain(static_cast<float>(commonScale));
            const auto level = rms(render.audio);
            require(std::abs(20*std::log10(level/(.1*commonScale))) < .001, "RMS match failed");
            require(render.audio.getMagnitude(0,count) <= .80001f, "Headroom failed");
            std::unique_ptr<juce::OutputStream> stream(output.getChildFile(render.name+".wav").createOutputStream());
            require(stream != nullptr, "WAV open failed");
            auto writer = format.createWriterFor(stream,juce::AudioFormatWriterOptions{}
                .withSampleRate(reader->sampleRate).withNumChannels(channels).withBitsPerSample(32)
                .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
            require(writer && writer->writeFromAudioSampleBuffer(render.audio,0,count) && writer->flush(), "WAV write failed");
            auto* file = new juce::DynamicObject;
            file->setProperty("file",render.name+".wav"); file->setProperty("alignedBySamples",static_cast<int>(render.latency));
            file->setProperty("rms",level); file->setProperty("peak",render.audio.getMagnitude(0,count));
            file->setProperty("rmsBeforeMatching",render.unmatchedRms);
            file->setProperty("appliedGain",render.appliedGain*commonScale);
            file->setProperty("path",render.name == "reference" ? "reference" : "cascade");
            if (render.name != "reference")
            {
                file->setProperty("tanhDriveDb",render.tanhDriveDb);
                file->setProperty("folderDriveDb",render.folderDriveDb);
                file->setProperty("threshold",render.threshold);
                file->setProperty("tanhOutputDb",0); file->setProperty("folderOutputDb",0);
                file->setProperty("factor",render.name.endsWith("8x") ? 8 : 4);
                file->setProperty("foldAlgorithm",render.name.contains("adaa") ? "adaaTriangle" : "triangle");
            }
            files.add(juce::var(file));
        }
        report->setProperty("files",juce::var(files));
        require(output.getChildFile("result.json").replaceWithText(juce::JSON::toString(reportValue)), "Report write failed");
        std::cout << "Rendered " << renders.size() << " matched files, " << count/reader->sampleRate
            << " seconds at " << reader->sampleRate << " Hz. Human listening pending.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << "; any partial output retained\n";
        return 1;
    }
}
