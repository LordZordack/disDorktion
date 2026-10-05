#pragma once
#include <juce_dsp/juce_dsp.h>
#include <concepts>
#include <cmath>

namespace disdorktion
{
// See docs/DSP_CONTRACT.md for storage, threading and failure semantics.
inline bool isValidProcessSpec(const juce::dsp::ProcessSpec& spec) noexcept
{
    return std::isfinite(spec.sampleRate) && spec.sampleRate >= 8000.0
        && spec.sampleRate <= 384000.0 && spec.maximumBlockSize >= 1
        && spec.maximumBlockSize <= 65536 && (spec.numChannels == 1 || spec.numChannels == 2);
}

template<class T, class Parameters>
concept Module = requires(T module, const T constant, const Parameters& parameters,
                          const juce::dsp::ProcessSpec& spec, juce::dsp::AudioBlock<float> block)
{
    { module.prepare(spec) } -> std::same_as<bool>;
    { module.reset() } noexcept -> std::same_as<void>;
    { module.setParameters(parameters) } noexcept -> std::same_as<bool>;
    { module.process(block) } noexcept -> std::same_as<bool>;
    { constant.getLatencySamples() } noexcept -> std::same_as<unsigned int>;
};
}
