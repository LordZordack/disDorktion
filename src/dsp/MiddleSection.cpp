#include "MiddleSection.h"
#include <bit>

namespace disdorktion
{
bool MiddleSection::prepare(const juce::dsp::ProcessSpec& spec, unsigned value, MiddleLatencyCorrection mode, MiddleFoldAlgorithm algorithm)
{
    if (!isValidProcessSpec(spec) || value == 0 || value > 16 || (value & (value - 1)) != 0
        || (mode != MiddleLatencyCorrection::fir && mode != MiddleLatencyCorrection::juceThiran)
        || (algorithm != MiddleFoldAlgorithm::triangle && algorithm != MiddleFoldAlgorithm::adaaTriangle)
        || (algorithm == MiddleFoldAlgorithm::adaaTriangle && mode == MiddleLatencyCorrection::juceThiran)) return false;
    // Allocate and initialise off the processing thread before changing live state.
    auto replacement = std::make_unique<juce::dsp::Oversampling<float>>(
        spec.numChannels, static_cast<std::size_t>(std::countr_zero(value)),
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, mode == MiddleLatencyCorrection::juceThiran);
    replacement->initProcessing(spec.maximumBlockSize);
    const bool useAdaa = algorithm == MiddleFoldAlgorithm::adaaTriangle;
    const double rawLatency = replacement->getLatencyInSamples() + (useAdaa ? adaaFolder.getLatencyInInternalSamples()/value : 0.0);
    const auto fraction = std::ceil(rawLatency) - rawLatency;
    const bool useFir = mode == MiddleLatencyCorrection::fir && fraction > 1e-6;
    std::array<double, 65> taps {};
    double addedLatency = 0;
    if (useFir)
    {
        addedLatency = 32.0 + fraction;
        juce::dsp::WindowingFunction<double>::fillWindowingTables(taps.data(), taps.size(),
            juce::dsp::WindowingFunction<double>::kaiser, false, 8.6);
        double sum = 0;
        for (std::size_t n = 0; n < taps.size(); ++n)
        {
            const auto x = static_cast<double>(n) - addedLatency;
            const auto sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(juce::MathConstants<double>::pi*x)/(juce::MathConstants<double>::pi*x);
            taps[n] *= sinc;
            sum += taps[n];
        }
        for (auto& tap : taps) tap /= sum;
    }
    const auto newLatency = static_cast<unsigned>(std::lround(rawLatency + addedLatency));
    // All fallible domain checks have passed; these preparations cannot reject.
    (void) dispatcher.prepare(spec);
    (void) saturation.prepareOversampled(spec, value);
    (void) folder.prepareOversampled(spec, value);
    (void) adaaFolder.prepareOversampled(spec, value);
    oversampling = std::move(replacement);
    correctionTaps = taps;
    needsFirCorrection = useFir;
    usesAdaa = useAdaa;
    factor = value;
    latency = newLatency;
    reset();
    return true;
}
void MiddleSection::reset() noexcept
{
    saturation.reset(); folder.reset(); adaaFolder.reset();
    if (oversampling) oversampling->reset();
    for (auto& channel : correctionHistory) channel.fill(0);
    correctionPosition = 0;
}
bool MiddleSection::setParameters(const MiddleSettings& value) noexcept
{
    if (!isValidTanhSaturationSettings(value.tanh) || !isValidWaveFolderSettings(value.folder)) return false;
    (void) saturation.setParameters(value.tanh);
    (void) folder.setParameters(value.folder);
    (void) adaaFolder.setParameters(value.folder);
    return true;
}
bool MiddleSection::process(juce::dsp::AudioBlock<float> block) noexcept
{
    bool good = true;
    const auto valid = dispatcher.process(block, [this, &good](auto chunk) noexcept
    {
        if (factor == 1)
        {
            good &= saturation.process(chunk);
            good &= usesAdaa ? adaaFolder.process(chunk) : folder.process(chunk);
        }
        else
        {
            auto internal = oversampling->processSamplesUp(chunk);
            good &= saturation.process(internal);
            good &= usesAdaa ? adaaFolder.process(internal) : folder.process(internal);
            oversampling->processSamplesDown(chunk);
        }
        if (needsFirCorrection)
            for (std::size_t n = 0; n < chunk.getNumSamples(); ++n)
                {
                    for (std::size_t c = 0; c < chunk.getNumChannels(); ++c)
                    {
                        auto& history = correctionHistory[c];
                        history[correctionPosition] = chunk.getChannelPointer(c)[n];
                        history[correctionPosition + correctionTaps.size()] = history[correctionPosition];
                        double y = 0;
                        const auto* samples = history.data() + correctionPosition;
                        for (std::size_t tap = 0; tap < correctionTaps.size(); ++tap)
                            y += correctionTaps[tap] * samples[tap];
                        chunk.getChannelPointer(c)[n] = static_cast<float>(y);
                    }
                    correctionPosition = correctionPosition == 0 ? correctionTaps.size()-1 : correctionPosition-1;
                }
    });
    return valid && good;
}
}
