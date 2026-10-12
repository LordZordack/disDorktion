#include "AuditionEngine.h"
#include <algorithm>
#include <cmath>

namespace disdorktion::harness
{
namespace
{
bool validGain(float gain) noexcept { return std::isfinite(gain) && gain >= 0.0f && gain <= 4.0f; }
bool validGainDb(float db) noexcept { return std::isfinite(db) && db >= -60.0f && db <= 24.0f; }
bool validMonitor(float db) noexcept { return std::isfinite(db) && db >= -60.0f && db <= 0.0f; }
float monitorLevel(float db, bool muted) noexcept { return muted ? 0.0f : std::pow(10.0f, db / 20.0f); }
struct Reading { float peak = 0.0f; float rms = 0.0f; bool finite = true; };
Reading measure(juce::dsp::AudioBlock<float> block) noexcept
{
    Reading reading;
    double squares = 0.0;
    for (std::size_t c = 0; c < block.getNumChannels(); ++c)
        for (std::size_t n = 0; n < block.getNumSamples(); ++n)
        {
            const auto value = block.getChannelPointer(c)[n];
            if (!std::isfinite(value)) { reading.finite = false; continue; }
            reading.peak = std::max(reading.peak, std::abs(value));
            squares += static_cast<double>(value) * value;
        }
    if (block.getNumSamples() != 0 && block.getNumChannels() != 0)
        reading.rms = static_cast<float>(std::sqrt(squares / static_cast<double>(block.getNumSamples())
                                                 / static_cast<double>(block.getNumChannels())));
    return reading;
}
}

bool AuditionEngine::prepare(const juce::dsp::ProcessSpec& spec)
{
    prepared = false;
    clearMeters();
    if (!isValidProcessSpec(spec) || !target.prepare(spec) || !productionTarget.prepare(spec)
        || !crusherTarget.prepare(spec) || !saturationTarget.prepare(spec) || !folderTarget.prepare(spec)
        || !highPassTarget.prepare(spec) || !lowPassTarget.prepare(spec)) return false;
    channels = spec.numChannels;
    monitor.reset(spec.sampleRate, 0.010);
    prepared = true;
    reset();
    return true;
}

void AuditionEngine::reset() noexcept
{
    (void) applyParameters();
    selected->reset();
    monitor.setCurrentAndTargetValue(monitorLevel(monitorDb.load(std::memory_order_relaxed),
                                                  muted.load(std::memory_order_relaxed)));
    clearMeters();
}

bool AuditionEngine::selectTarget(std::string_view id) noexcept
{
    if (id == "reference-gain") selected = &target;
    else if (id == "gain") selected = &productionTarget;
    else if (id == "bit-crusher") selected = &crusherTarget;
    else if (id == "tanh-saturation") selected = &saturationTarget;
    else if (id == "wave-folder") selected = &folderTarget;
    else if (id == "high-pass") selected = &highPassTarget;
    else if (id == "low-pass") selected = &lowPassTarget;
    else return false;
    reset();
    return true;
}

bool AuditionEngine::setParameters(const GainParameters& parameters) noexcept
{
    if (!validGain(parameters.gain)) return false;
    gain.store(parameters.gain, std::memory_order_relaxed);
    bypass.store(parameters.bypass, std::memory_order_relaxed);
    return target.setParameters(parameters);
}

bool AuditionEngine::setGain(float value) noexcept
{
    if (!validGain(value)) return false;
    gain.store(value, std::memory_order_relaxed);
    return true;
}
bool AuditionEngine::setTanhSaturationSettings(const TanhSaturationSettings& value) noexcept
{
    if (!isValidTanhSaturationSettings(value)) return false;
    tanhDriveDb.store(value.driveDb, std::memory_order_relaxed);
    tanhOutputDb.store(value.outputDb, std::memory_order_relaxed);
    tanhBypass.store(value.bypass, std::memory_order_relaxed);
    return saturationTarget.setParameters(value);
}
bool AuditionEngine::setTanhDriveDb(float value) noexcept
{
    if (!isValidTanhSaturationSettings({value, 0.0f, false})) return false;
    tanhDriveDb.store(value, std::memory_order_relaxed);
    return true;
}
bool AuditionEngine::setTanhOutputDb(float value) noexcept
{
    if (!isValidTanhSaturationSettings({0.0f, value, false})) return false;
    tanhOutputDb.store(value, std::memory_order_relaxed);
    return true;
}
bool AuditionEngine::setWaveFolderSettings(const WaveFolderSettings& value) noexcept
{
    if (!isValidWaveFolderSettings(value)) return false;
    waveDriveDb.store(value.driveDb, std::memory_order_relaxed);
    waveThreshold.store(value.threshold, std::memory_order_relaxed);
    waveOutputDb.store(value.outputDb, std::memory_order_relaxed);
    waveBypass.store(value.bypass, std::memory_order_relaxed);
    return folderTarget.setParameters(value);
}
bool AuditionEngine::setWaveDriveDb(float value) noexcept
{
    if (!isValidWaveFolderSettings({value, 1.0f, 0.0f, false})) return false;
    waveDriveDb.store(value, std::memory_order_relaxed); return true;
}
bool AuditionEngine::setWaveThreshold(float value) noexcept
{
    if (!isValidWaveFolderSettings({0.0f, value, 0.0f, false})) return false;
    waveThreshold.store(value, std::memory_order_relaxed); return true;
}
bool AuditionEngine::setWaveOutputDb(float value) noexcept
{
    if (!isValidWaveFolderSettings({0.0f, 1.0f, value, false})) return false;
    waveOutputDb.store(value, std::memory_order_relaxed); return true;
}
bool AuditionEngine::setFilterSettings(const CutoffSettings& value) noexcept
{
    if (!isValidCutoffSettings(value) || (selected != &highPassTarget && selected != &lowPassTarget)) return false;
    const bool high = selected == &highPassTarget;
    (high ? highCutoff : lowCutoff).store(value.cutoffHz,std::memory_order_relaxed);
    (high ? highResponse : lowResponse).store(static_cast<unsigned>(value.response),std::memory_order_relaxed);
    (high ? highBypass : lowBypass).store(value.bypass,std::memory_order_relaxed);
    return high ? highPassTarget.setParameters(value) : lowPassTarget.setParameters(value);
}
bool AuditionEngine::setFilterCutoffHz(float value) noexcept
{
    if (!isValidCutoffSettings({value,false,FilterResponse::butterworth})
        || (selected != &highPassTarget && selected != &lowPassTarget)) return false;
    (selected == &highPassTarget ? highCutoff : lowCutoff).store(value,std::memory_order_relaxed);
    return true;
}
bool AuditionEngine::setFilterResponse(FilterResponse value) noexcept
{
    if (static_cast<unsigned>(value) >= filterResponseIds.size()
        || (selected != &highPassTarget && selected != &lowPassTarget)) return false;
    (selected == &highPassTarget ? highResponse : lowResponse).store(static_cast<unsigned>(value),std::memory_order_relaxed);
    return true;
}
bool AuditionEngine::setBitCrusherSettings(const BitCrusherSettings& value) noexcept
{
    if (!isValidBitCrusherSettings(value)) return false;
    bitDepth.store(value.bitDepth, std::memory_order_relaxed);
    captureRateHz.store(value.captureRateHz, std::memory_order_relaxed);
    crusherBypass.store(value.bypass, std::memory_order_relaxed);
    crusherMix.store(value.mix, std::memory_order_relaxed);
    crush.store(value.crush, std::memory_order_relaxed);
    quantizerShape.store(value.shape, std::memory_order_relaxed);
    jitter.store(value.jitter, std::memory_order_relaxed);
    jitterSeed.store(value.jitterSeed, std::memory_order_relaxed);
    return crusherTarget.setParameters(value);
}
bool AuditionEngine::setBitDepth(unsigned value) noexcept
{
    if (!isValidBitDepth(value)) return false;
    bitDepth.store(value, std::memory_order_relaxed);
    return true;
}
bool AuditionEngine::setCaptureRateHz(float value) noexcept
{
    if (!std::isfinite(value) || value < 100.0f || value > 384000.0f) return false;
    captureRateHz.store(value, std::memory_order_relaxed);
    return true;
}
bool AuditionEngine::setCrusherMix(float value) noexcept
{
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f) return false;
    crusherMix.store(value, std::memory_order_relaxed);
    return true;
}
bool AuditionEngine::setCrush(float value) noexcept
{
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f) return false;
    crush.store(value, std::memory_order_relaxed);
    return true;
}
bool AuditionEngine::setQuantizerShape(float value) noexcept
{
    if (!std::isfinite(value) || value < -1.0f || value > 1.0f) return false;
    quantizerShape.store(value, std::memory_order_relaxed);
    return true;
}
bool AuditionEngine::setJitter(float value) noexcept
{
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f) return false;
    jitter.store(value, std::memory_order_relaxed);
    return true;
}
void AuditionEngine::setJitterSeed(std::uint32_t value) noexcept
{
    jitterSeed.store(value, std::memory_order_relaxed);
}
bool AuditionEngine::setGainSettings(const GainSettings& value) noexcept
{
    if (!validGainDb(value.gainDb)) return false;
    gainDb.store(value.gainDb, std::memory_order_relaxed);
    moduleMuted.store(value.mute, std::memory_order_relaxed);
    productionBypass.store(value.bypass, std::memory_order_relaxed);
    return productionTarget.setParameters(value);
}
bool AuditionEngine::setGainDb(float value) noexcept
{
    if (!validGainDb(value)) return false;
    gainDb.store(value, std::memory_order_relaxed); return true;
}
void AuditionEngine::setModuleMuted(bool value) noexcept { moduleMuted.store(value, std::memory_order_relaxed); }
void AuditionEngine::setBypass(bool value) noexcept
{
    if (selected == &highPassTarget) { highBypass.store(value,std::memory_order_relaxed); return; }
    if (selected == &lowPassTarget) { lowBypass.store(value,std::memory_order_relaxed); return; }
    (selected == &target ? bypass : selected == &productionTarget ? productionBypass
        : selected == &saturationTarget ? tanhBypass : selected == &folderTarget ? waveBypass : crusherBypass)
        .store(value, std::memory_order_relaxed);
}
bool AuditionEngine::applyParameters() noexcept
{
    if (selected == &highPassTarget)
        return highPassTarget.setParameters({highCutoff.load(std::memory_order_relaxed), highBypass.load(std::memory_order_relaxed),
            static_cast<FilterResponse>(highResponse.load(std::memory_order_relaxed))});
    if (selected == &lowPassTarget)
        return lowPassTarget.setParameters({lowCutoff.load(std::memory_order_relaxed), lowBypass.load(std::memory_order_relaxed),
            static_cast<FilterResponse>(lowResponse.load(std::memory_order_relaxed))});
    if (selected == &folderTarget)
        return folderTarget.setParameters({waveDriveDb.load(std::memory_order_relaxed),
            waveThreshold.load(std::memory_order_relaxed), waveOutputDb.load(std::memory_order_relaxed),
            waveBypass.load(std::memory_order_relaxed)});
    if (selected == &saturationTarget)
        return saturationTarget.setParameters({tanhDriveDb.load(std::memory_order_relaxed),
            tanhOutputDb.load(std::memory_order_relaxed), tanhBypass.load(std::memory_order_relaxed)});
    if (selected == &target)
        return target.setParameters({gain.load(std::memory_order_relaxed), bypass.load(std::memory_order_relaxed)});
    if (selected == &crusherTarget)
        return crusherTarget.setParameters({bitDepth.load(std::memory_order_relaxed),
            captureRateHz.load(std::memory_order_relaxed), crusherBypass.load(std::memory_order_relaxed),
            crusherMix.load(std::memory_order_relaxed), crush.load(std::memory_order_relaxed),
            quantizerShape.load(std::memory_order_relaxed), jitter.load(std::memory_order_relaxed),
            jitterSeed.load(std::memory_order_relaxed)});
    return productionTarget.setParameters({gainDb.load(std::memory_order_relaxed),
        moduleMuted.load(std::memory_order_relaxed), productionBypass.load(std::memory_order_relaxed)});
}
void AuditionEngine::setMonitorDb(float value) noexcept
{
    if (validMonitor(value)) monitorDb.store(value, std::memory_order_relaxed);
}
void AuditionEngine::setMuted(bool value) noexcept { muted.store(value, std::memory_order_relaxed); }
bool AuditionEngine::setMonitoring(float db, bool value) noexcept
{
    if (!validMonitor(db)) return false;
    monitorDb.store(db, std::memory_order_relaxed);
    muted.store(value, std::memory_order_relaxed);
    return true;
}

bool AuditionEngine::process(juce::dsp::AudioBlock<float> block, bool applyMonitor) noexcept
{
    const juce::ScopedNoDenormals noDenormals;
    if (!prepared || block.getNumChannels() != channels)
    {
        block.clear();
        inputPeak.store(0.0f, std::memory_order_relaxed); inputRms.store(0.0f, std::memory_order_relaxed);
        outputPeak.store(0.0f, std::memory_order_relaxed); outputRms.store(0.0f, std::memory_order_relaxed);
        return false;
    }
    const auto before = measure(block);
    inputPeak.store(before.peak, std::memory_order_relaxed);
    inputRms.store(before.rms, std::memory_order_relaxed);
    if (!before.finite
        || !applyParameters()
        || !selected->process(block))
    {
        block.clear();
        outputPeak.store(0.0f, std::memory_order_relaxed); outputRms.store(0.0f, std::memory_order_relaxed);
        if (!before.finite) overload.store(true, std::memory_order_relaxed);
        return false;
    }
    const auto after = measure(block);
    outputPeak.store(after.peak, std::memory_order_relaxed);
    outputRms.store(after.rms, std::memory_order_relaxed);
    if (after.peak > 1.0f || !after.finite) overload.store(true, std::memory_order_relaxed);
    if (!after.finite) { block.clear(); return false; }
    if (applyMonitor)
    {
        monitor.setTargetValue(monitorLevel(monitorDb.load(std::memory_order_relaxed), muted.load(std::memory_order_relaxed)));
        for (std::size_t n = 0; n < block.getNumSamples(); ++n)
        {
            const auto level = monitor.getNextValue();
            for (std::size_t c = 0; c < block.getNumChannels(); ++c) block.getChannelPointer(c)[n] *= level;
        }
    }
    return true;
}

MeterSnapshot AuditionEngine::meters() const noexcept
{
    // Scalar readings may span adjacent callbacks; no retry loop blocks processing.
    return {inputPeak.load(std::memory_order_relaxed), inputRms.load(std::memory_order_relaxed),
            outputPeak.load(std::memory_order_relaxed), outputRms.load(std::memory_order_relaxed),
            overload.load(std::memory_order_relaxed)};
}
bool AuditionEngine::consumeOverload() noexcept { return overload.exchange(false, std::memory_order_relaxed); }
void AuditionEngine::clearMeters() noexcept
{
    inputPeak.store(0.0f, std::memory_order_relaxed); inputRms.store(0.0f, std::memory_order_relaxed);
    outputPeak.store(0.0f, std::memory_order_relaxed); outputRms.store(0.0f, std::memory_order_relaxed);
    overload.store(false, std::memory_order_relaxed);
}
}
