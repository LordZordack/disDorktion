#pragma once
#include "SignalSource.h"
#include "dsp/CutoffFilter.h"
#include "dsp/MiddleSection.h"
#include "dsp/OutputSection.h"
#include <juce_core/juce_core.h>

namespace disdorktion::harness
{
struct ExperimentRecord
{
    // Counts are bounded below 2^53, so JSON numeric round trips are exact.
    static constexpr std::uint64_t maximumSamples = 536870000;
    int version = 1;
    juce::String targetId = "reference-gain";
    float gain = 1.0f;
    float gainDb = 0.0f;
    float driveDb = 0.0f, outputDb = 0.0f;
    float threshold = 1.0f;
    float cutoffHz = 1000.0f;
    FilterResponse response = FilterResponse::butterworth;
    unsigned bitDepth = 64;
    float captureRateHz = 384000.0f;
    float mix = 1.0f, crush = 0.0f, shape = 0.0f, jitter = 0.0f;
    std::uint32_t jitterSeed = 1;
    bool mute = false;
    bool bypass = false;
    bool gainBypass = false, crusherBypass = false;
    MiddleSettings middle;
    OutputSettings output;
    juce::String middleQuality = "4x-fir-adaa-v1";
    SourceSettings source;
    double sampleRate = 48000.0;
    unsigned channels = 2, blockSize = 256;
    std::uint64_t renderDurationSamples = 48000;
    juce::String filePath, fileHash;
    juce::String sourceConversion = "linear;mono-duplicate;stereo-average";
    double originalSampleRate = 0.0;
    unsigned originalChannels = 0;
    float monitorDb = -18.0f;
    bool muted = true;
    juce::String midiDevice, buildIdentity, observations;

    ExperimentRecord();
    bool validate(juce::String& error) const;
    juce::var toJson() const;
    static bool fromJson(const juce::var&, ExperimentRecord&, juce::String& error);
    bool save(const juce::File&, juce::String& error) const;
    static bool load(const juce::File&, ExperimentRecord&, juce::String& error);
};
}
