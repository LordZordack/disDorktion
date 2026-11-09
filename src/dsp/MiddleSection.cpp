#include "MiddleSection.h"
#include "DriverCompensation.h"
#include <algorithm>
#include <bit>

namespace disdorktion
{
bool MiddleSection::prepare(const juce::dsp::ProcessSpec& spec, unsigned value, MiddleLatencyCorrection mode, MiddleFoldAlgorithm algorithm)
{
    if (!isValidProcessSpec(spec) || value == 0 || value > 16 || (value & (value - 1)) != 0
        || (mode != MiddleLatencyCorrection::fir && mode != MiddleLatencyCorrection::juceThiran)
        || (algorithm != MiddleFoldAlgorithm::triangle && algorithm != MiddleFoldAlgorithm::adaaTriangle)
        || (algorithm == MiddleFoldAlgorithm::adaaTriangle && mode == MiddleLatencyCorrection::juceThiran)) return false;
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
    juce::AudioBuffer<float> scratch(static_cast<int>(spec.numChannels), static_cast<int>(spec.maximumBlockSize));
    std::array<std::vector<float>, 2> history;
    for (auto& channel : history) channel.resize(newLatency);
    std::vector<float> compensationDelay(std::max(1u,newLatency));
    std::vector<float> compensationBuffer(spec.maximumBlockSize);
    (void) dispatcher.prepare(spec);
    (void) saturation.prepareOversampled(spec, value);
    (void) folder.prepareOversampled(spec, value);
    (void) adaaFolder.prepareOversampled(spec, value);
    oversampling = std::move(replacement);
    dryScratch = std::move(scratch);
    dryHistory = std::move(history);
    driverHistory = std::move(compensationDelay);
    compensationScratch = std::move(compensationBuffer);
    correctionTaps = taps;
    needsFirCorrection = useFir;
    usesAdaa = useAdaa;
    factor = value;
    latency = newLatency;
    // A symmetric FIR's full support is twice its group delay. Include the
    // correction FIR and ADAA input/correction history; Thiran gets a settling guard.
    warmupFrames = static_cast<std::size_t>(std::ceil(2.0*rawLatency))
        + (useFir ? taps.size() : 0) + (useAdaa ? 3u : 0u)
        + (mode == MiddleLatencyCorrection::juceThiran ? 64u : 0u);
    sectionMix.reset(spec.sampleRate, .010);
    nonlinearMix.reset(spec.sampleRate, .010);
    trimGain.reset(spec.sampleRate, .010);
    driver.reset(spec.sampleRate, .010);
    reset();
    return true;
}
void MiddleSection::resetWet() noexcept
{
    cachedDriver=driver.getCurrentValue();
    cachedCompensation=driverCompensationGain(cachedDriver);
    std::fill(driverHistory.begin(),driverHistory.end(),cachedCompensation);
    driverPosition=0;
    saturation.reset(); folder.reset(); adaaFolder.reset();
    if (oversampling) oversampling->reset();
    for (auto& channel : correctionHistory) channel.fill(0);
    correctionPosition = 0;
}
void MiddleSection::reset() noexcept
{
    driver.setCurrentAndTargetValue(retainedDriver);
    resetWet();
    for (auto& channel : dryHistory) std::fill(channel.begin(), channel.end(), 0.f);
    dryPosition = 0;
    wetFramesProcessed = 0;
    warmupRemaining = 0;
    holdMixDuringWarmup = false;
    wetRunning = retained.mix > 0.f && !(retained.tanh.bypass && retained.folder.bypass);
    sectionMix.setCurrentAndTargetValue(retained.mix);
    nonlinearMix.setCurrentAndTargetValue(wetRunning ? 1.f : 0.f);
    trimGain.setCurrentAndTargetValue(std::pow(10.f, retained.trimDb/20.f));
}
bool MiddleSection::setParameters(const MiddleSettings& value) noexcept
{
    if (!isValidTanhSaturationSettings(value.tanh) || !isValidWaveFolderSettings(value.folder)
        || !std::isfinite(value.mix) || value.mix < 0.f || value.mix > 1.f
        || !std::isfinite(value.trimDb) || value.trimDb < -60.f || value.trimDb > 12.f) return false;
    (void) saturation.setParameters(value.tanh);
    (void) folder.setParameters(value.folder);
    (void) adaaFolder.setParameters(value.folder);
    retained = value;
    if (!holdMixDuringWarmup) sectionMix.setTargetValue(value.mix);
    trimGain.setTargetValue(std::pow(10.f, value.trimDb/20.f));
    return true;
}
bool MiddleSection::setDriver(float value) noexcept
{
    if (!std::isfinite(value) || value<1.f || value>4.f) return false;
    retainedDriver=value;
    driver.setTargetValue(value);
    return true;
}
void MiddleSection::updateWetState() noexcept
{
    const bool wanted = retained.mix > 0.f && !(retained.tanh.bypass && retained.folder.bypass);
    if (!wanted)
    {
        warmupRemaining = 0;
        holdMixDuringWarmup = false;
        sectionMix.setTargetValue(retained.mix);
        // A zero section mix already removes the entire wet branch. Keep its
        // internal blend unchanged during that fade to avoid a second envelope.
        if (retained.tanh.bypass && retained.folder.bypass) nonlinearMix.setTargetValue(0.f);
        if (wetRunning && (sectionMix.getCurrentValue() == 0.f || nonlinearMix.getCurrentValue() == 0.f))
        {
            resetWet();
            wetRunning = false;
        }
        return;
    }
    if (!wetRunning)
    {
        resetWet();
        wetRunning = true;
        warmupRemaining = warmupFrames;
        nonlinearMix.setCurrentAndTargetValue(0.f);
        holdMixDuringWarmup = sectionMix.getCurrentValue() == 0.f;
        if (holdMixDuringWarmup) sectionMix.setCurrentAndTargetValue(0.f);
    }
    if (warmupRemaining == 0)
    {
        holdMixDuringWarmup = false;
        sectionMix.setTargetValue(retained.mix);
        nonlinearMix.setTargetValue(1.f);
    }
}
bool MiddleSection::processWet(juce::dsp::AudioBlock<float> chunk) noexcept
{
    bool good = true;
    // Only the nonlinear branch is driven. The section dry delay was captured
    // by process() before this call. Match compensation to the delayed audio.
    for (std::size_t n=0;n<chunk.getNumSamples();++n)
    {
        const auto value=driver.getNextValue();
        if (value!=cachedDriver)
        {
            cachedDriver=value;
            cachedCompensation=driverCompensationGain(value);
        }
        compensationScratch[n]=latency ? driverHistory[driverPosition] : cachedCompensation;
        driverHistory[driverPosition]=cachedCompensation;
        if (++driverPosition>=driverHistory.size()) driverPosition=0;
        if (value!=1.f)
            for (std::size_t c=0;c<chunk.getNumChannels();++c) chunk.getChannelPointer(c)[n]*=value;
    }
    auto internal = factor == 1 ? chunk : oversampling->processSamplesUp(chunk);
    good &= saturation.process(internal);
    // ADAA bypass deliberately retains its matched 1.5-internal-sample linear
    // average and droop-correction history. Never replace it with a plain skip.
    good &= usesAdaa ? adaaFolder.process(internal) : folder.process(internal);
    if (factor != 1) oversampling->processSamplesDown(chunk);
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
    for (std::size_t n=0;n<chunk.getNumSamples();++n)
        if (compensationScratch[n]!=1.f)
            for (std::size_t c=0;c<chunk.getNumChannels();++c) chunk.getChannelPointer(c)[n]*=compensationScratch[n];
    wetFramesProcessed += chunk.getNumSamples();
    return good;
}
bool MiddleSection::process(juce::dsp::AudioBlock<float> block) noexcept
{
    bool good = true;
    const auto valid = dispatcher.process(block, [this, &good](auto chunk) noexcept
    {
        for (std::size_t offset = 0; offset < chunk.getNumSamples();)
        {
            updateWetState();
            auto count = chunk.getNumSamples()-offset;
            for (auto remaining : {sectionMix.remaining(), nonlinearMix.remaining(), warmupRemaining})
                if (remaining != 0) count = std::min(count, remaining);
            auto segment = chunk.getSubBlock(offset, count);
            for (std::size_t n = 0; n < count; ++n)
            {
                for (std::size_t c = 0; c < segment.getNumChannels(); ++c)
                {
                    const auto x = segment.getChannelPointer(c)[n];
                    dryScratch.setSample(static_cast<int>(c), static_cast<int>(n), latency == 0 ? x : dryHistory[c][dryPosition]);
                    if (latency != 0) dryHistory[c][dryPosition] = x;
                }
                if (latency != 0) dryPosition = (dryPosition+1)%latency;
            }
            if (wetRunning) good &= processWet(segment);
            else driver.skip(static_cast<int>(count));
            for (std::size_t n = 0; n < count; ++n)
            {
                const auto mix = sectionMix.getNextValue(), nonlinear = nonlinearMix.getNextValue(), gain = trimGain.getNextValue();
                for (std::size_t c = 0; c < segment.getNumChannels(); ++c)
                {
                    const auto dry = dryScratch.getSample(static_cast<int>(c), static_cast<int>(n));
                    const auto wet = nonlinear == 0.f ? dry : nonlinear == 1.f ? segment.getChannelPointer(c)[n]
                        : dry + nonlinear*(segment.getChannelPointer(c)[n]-dry);
                    const auto trimmed = gain*wet;
                    segment.getChannelPointer(c)[n] = mix == 0.f ? dry : mix == 1.f ? trimmed : dry + mix*(trimmed-dry);
                }
            }
            if (warmupRemaining != 0) warmupRemaining -= count;
            offset += count;
            updateWetState();
        }
    });
    return valid && good;
}
}
