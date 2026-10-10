#pragma once
#include "SignalSource.h"
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
    bool bypass = false;
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
