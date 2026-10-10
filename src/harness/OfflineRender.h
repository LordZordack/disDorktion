#pragma once
#include "ExperimentRecord.h"
#include <atomic>

namespace disdorktion::harness
{
enum class RenderStatus { success, invalidRequest, ioFailure };
struct RenderResult
{
    RenderStatus status = RenderStatus::invalidRequest;
    juce::String message;
    std::uint64_t samples = 0;
    double inputPeak = 0, inputRms = 0, outputPeak = 0, outputRms = 0;
    bool overloaded = false;
    unsigned latencySamples = 0;
};
// Blocking, device-free worker/CLI operation. Existing destinations are rejected.
RenderResult renderExperiment(const ExperimentRecord&, const juce::File& outputDirectory,
                              const std::atomic<bool>* cancel = nullptr);
}
