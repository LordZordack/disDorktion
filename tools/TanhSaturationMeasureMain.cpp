#include "dsp/TanhSaturation.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr int fftFrames = 1 << 16;

struct FixtureResult
{
    juce::var json;
    std::vector<std::pair<int, std::pair<double, double>>> bins;
    unsigned sampleRate = 0;
    int fftSize = 0;
    bool passed = false;
};

bool writeFloatWav(const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
{
    if (file.exists()) return false;
    std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
    if (!stream) return false;
    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(rate).withNumChannels(static_cast<unsigned>(audio.getNumChannels()))
        .withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    return writer && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()) && writer->flush();
}

double independentTanh(double z) noexcept
{
    const auto magnitude = std::abs(z);
    if (magnitude > 40.0) return std::copysign(1.0, z);
    const auto e = std::expm1(-2.0 * magnitude);
    return std::copysign((-e) / (2.0 + e), z);
}

double windowValue(int n, int count, bool hann) noexcept
{
    return hann ? 0.5 - 0.5 * std::cos(2.0 * pi * n / static_cast<double>(count - 1)) : 1.0;
}

FixtureResult measure(const juce::File& directory, const juce::String& name, unsigned rate,
                      const juce::String& kind, double driveDb, double amplitude,
                      const std::array<int, 2>& bins, bool coherentFixture)
{
    FixtureResult result;
    result.sampleRate = rate;
    const auto count = kind == "log-sweep" ? static_cast<int>(2 * rate) : fftFrames;
    auto fftFramesForFixture = 1;
    int fftOrderForFixture = 0;
    while (fftFramesForFixture < count) { fftFramesForFixture <<= 1; ++fftOrderForFixture; }
    result.fftSize = fftFramesForFixture;
    juce::AudioBuffer<float> source(1, count), wet(1, count);
    const auto drive = std::pow(10.0, driveDb / 20.0);
    const auto outputGain = 1.0;
    for (int n = 0; n < count; ++n)
    {
        double input = 0.0;
        if (kind == "sine" || kind == "high-sine")
            input = amplitude * std::sin(2.0 * pi * bins[0] * n / count);
        else if (kind == "two-tone")
            input = amplitude * (std::sin(2.0 * pi * bins[0] * n / count)
                               + std::sin(2.0 * pi * bins[1] * n / count));
        else
        {
            const auto t = static_cast<double>(n) / rate;
            const auto duration = static_cast<double>(count) / rate;
            const auto f0 = 20.0;
            const auto f1 = 0.45 * rate;
            const auto k = std::log(f1 / f0) / duration;
            const auto phase = 2.0 * pi * f0 * std::expm1(k * t) / k;
            input = amplitude * std::sin(phase);
        }
        source.setSample(0, n, static_cast<float>(input));
    }
    wet.makeCopyOf(source);

    disdorktion::TanhSaturation saturation;
    bool good = saturation.prepare({static_cast<double>(rate), 127, 1});
    good &= saturation.setParameters({static_cast<float>(driveDb), 0.0f, false});
    saturation.reset();
    good &= saturation.process(juce::dsp::AudioBlock<float>(wet));

    double maxError = 0.0;
    std::vector<float> measured(2 * fftFramesForFixture, 0.0f), reference(2 * fftFramesForFixture, 0.0f);
    for (int n = 0; n < count; ++n)
    {
        const auto x = static_cast<double>(source.getSample(0, n));
        const auto expected = outputGain * independentTanh(drive * x);
        maxError = std::max(maxError, std::abs(static_cast<double>(wet.getSample(0, n)) - expected));
        const auto window = static_cast<float>(windowValue(n, count, !coherentFixture));
        measured[n] = wet.getSample(0, n) * window;
        reference[n] = static_cast<float>(expected) * window;
    }
    juce::dsp::FFT fft(fftOrderForFixture);
    fft.performFrequencyOnlyForwardTransform(measured.data());
    fft.performFrequencyOnlyForwardTransform(reference.data());
    const auto spectrumPeak = *std::max_element(reference.begin() + 1, reference.begin() + fftFramesForFixture / 2);
    double maxSpectralErrorDb = 0.0;
    for (int k = 0; k < fftFramesForFixture / 2; ++k)
    {
        const auto actual = static_cast<double>(measured[k]);
        const auto expected = static_cast<double>(reference[k]);
        if (expected > spectrumPeak * 1.0e-4)
        {
            const auto errorDb = std::abs(20.0 * std::log10(std::max(actual, 1.0e-30) / expected));
            maxSpectralErrorDb = std::max(maxSpectralErrorDb, errorDb);
        }
        result.bins.push_back({k, {actual, expected}});
    }

    double fundamental = 0.0;
    if (kind == "sine" || kind == "high-sine") fundamental = measured[bins[0]];
    else if (kind == "two-tone") fundamental = std::max(measured[bins[0]], measured[bins[1]]);
    else fundamental = *std::max_element(measured.begin() + 1, measured.begin() + fftFramesForFixture / 2);

    double dcDbc = 20.0 * std::log10(std::max(1.0e-30, static_cast<double>(measured[0]) / fundamental));
    double evenHarmonicMaxDbc = -300.0;
    double oddHarmonicPower = 0.0;
    double finiteAliasPower = 0.0;
    juce::Array<juce::var> oddHarmonics, aliasedHarmonics;
    juce::Array<juce::var> significant;
    if (kind == "sine" || kind == "high-sine")
    {
        for (int harmonic = 2; harmonic <= 126; harmonic += 2)
        {
            const auto folded = (harmonic * bins[0]) % fftFramesForFixture;
            const auto k = std::min(folded, fftFramesForFixture - folded);
            if (k < fftFramesForFixture / 2)
            {
                const auto dbc = 20.0 * std::log10(std::max(1.0e-30, static_cast<double>(measured[k]) / fundamental));
                evenHarmonicMaxDbc = std::max(evenHarmonicMaxDbc, dbc);
            }
        }
        for (int harmonic = 3; harmonic * bins[0] <= fftFramesForFixture / 2; harmonic += 2)
        {
            const auto magnitude = static_cast<double>(measured[harmonic * bins[0]]);
            oddHarmonicPower += magnitude * magnitude;
            auto* item = new juce::DynamicObject;
            item->setProperty("order", harmonic);
            item->setProperty("bin", harmonic * bins[0]);
            item->setProperty("frequencyHz", static_cast<double>(harmonic * bins[0]) * rate / fftFramesForFixture);
            item->setProperty("dBc", 20.0 * std::log10(std::max(1.0e-30, magnitude / fundamental)));
            oddHarmonics.add(juce::var(item));
        }
        std::array<bool, fftFrames / 2 + 1> nativeBins {};
        for (int harmonic = 1; harmonic * bins[0] <= fftFramesForFixture / 2; harmonic += 2)
            nativeBins[static_cast<std::size_t>(harmonic * bins[0])] = true;
        std::array<unsigned, fftFrames / 2> aliasCollisions {};
        for (int harmonic = 3; harmonic <= 127; harmonic += 2)
        {
            if (harmonic * bins[0] <= fftFramesForFixture / 2) continue;
            const auto wrapped = (harmonic * bins[0]) % fftFramesForFixture;
            const auto aliasBin = std::min(wrapped, fftFramesForFixture - wrapped);
            if (aliasBin <= 0 || aliasBin >= fftFramesForFixture / 2 || nativeBins[static_cast<std::size_t>(aliasBin)]) continue;
            ++aliasCollisions[static_cast<std::size_t>(aliasBin)];
            auto* item = new juce::DynamicObject;
            item->setProperty("sourceHarmonicOrder", harmonic);
            item->setProperty("foldedBin", aliasBin);
            item->setProperty("foldedFrequencyHz", static_cast<double>(aliasBin) * rate / fftFramesForFixture);
            item->setProperty("measuredBinDbc", 20.0 * std::log10(std::max(1.0e-30,
                static_cast<double>(measured[aliasBin]) / fundamental)));
            aliasedHarmonics.add(juce::var(item));
        }
        for (std::size_t k = 1; k < aliasCollisions.size(); ++k)
            if (aliasCollisions[k] != 0)
                finiteAliasPower += static_cast<double>(measured[k]) * measured[k];
        std::vector<int> order;
        order.reserve(fftFramesForFixture / 2);
        for (int k = 1; k < fftFramesForFixture / 2; ++k) order.push_back(k);
        std::partial_sort(order.begin(), order.begin() + 20, order.end(), [&](int a, int b) { return measured[a] > measured[b]; });
        for (int i = 0; i < 20; ++i)
        {
            const auto k = order[i];
            auto* item = new juce::DynamicObject;
            item->setProperty("bin", k);
            item->setProperty("frequencyHz", static_cast<double>(k) * rate / fftFramesForFixture);
            item->setProperty("dBc", 20.0 * std::log10(std::max(1.0e-30, static_cast<double>(measured[k]) / fundamental)));
            item->setProperty("referenceMagnitude", static_cast<double>(reference[k]));
            significant.add(juce::var(item));
        }
    }

    const auto timeTolerance = 2.0e-7;
    const bool singleSine = kind == "sine" || kind == "high-sine";
    const bool spectralGood = !singleSine || (dcDbc <= -100.0 && evenHarmonicMaxDbc <= -100.0);
    good &= maxError <= timeTolerance && maxSpectralErrorDb <= 0.1 && spectralGood;
    result.passed = good;

    auto* report = new juce::DynamicObject;
    report->setProperty("name", name);
    report->setProperty("fixture", kind);
    report->setProperty("sampleRateHz", static_cast<int>(rate));
    report->setProperty("frames", count);
    report->setProperty("fftFrames", fftFramesForFixture);
    report->setProperty("driveDb", driveDb);
    report->setProperty("outputDb", 0.0);
    report->setProperty("inputAmplitude", amplitude);
    juce::Array<juce::var> inputBins;
    inputBins.add(bins[0]); inputBins.add(bins[1]);
    report->setProperty("inputBins", inputBins);
    report->setProperty("window", coherentFixture ? "rectangular (coherent)" : "Hann");
    report->setProperty("reference", "sign(z)*(-expm1(-2*abs(z)))/(2+expm1(-2*abs(z))); sign snap for abs(z)>40");
    report->setProperty("maxTimeDomainError", maxError);
    report->setProperty("timeDomainTolerance", timeTolerance);
    report->setProperty("maxSignificantBinReferenceErrorDb", maxSpectralErrorDb);
    report->setProperty("significantBinThresholdDbc", -80.0);
    report->setProperty("dcDbc", dcDbc);
    report->setProperty("maxEvenHarmonicDbc", evenHarmonicMaxDbc);
    if (singleSine)
    {
        report->setProperty("oddHarmonicsBelowNyquist", oddHarmonics);
        report->setProperty("thdDb", 10.0 * std::log10(std::max(1.0e-30, oddHarmonicPower / (fundamental * fundamental))));
        report->setProperty("foldedOddHarmonicsOrders3to127", aliasedHarmonics);
        report->setProperty("finiteOrderAliasEnergyDbcExcludingNativeCollisions",
            10.0 * std::log10(std::max(1.0e-30, finiteAliasPower / (fundamental * fundamental))));
        report->setProperty("aliasCollisionRule", "Only unique folded bins not coincident with an odd harmonic at or below Nyquist enter finite-order alias energy; overlapping folded orders share one measured bin.");
    }
    report->setProperty("topSpectralBins", significant);
    report->setProperty("aliasingInterpretation", "Native-rate folded components are baseline observations, not an antialiasing pass.");
    report->setProperty("passed", good);
    result.json = juce::var(report);

    good &= writeFloatWav(directory.getChildFile(name + "-source.wav"), source, rate);
    good &= writeFloatWav(directory.getChildFile(name + "-wet.wav"), wet, rate);
    result.passed = good;
    report->setProperty("passed", good);
    return result;
}

bool writeListeningFixtures(const juce::File& directory, bool transient)
{
    constexpr unsigned rate = 48000;
    constexpr int seconds = 8;
    constexpr int frames = static_cast<int>(rate * seconds);
    juce::AudioBuffer<float> source(2, frames), wet(2, frames);
    for (int n = 0; n < frames; ++n)
    {
        const auto t = static_cast<double>(n) / rate;
        const auto pulsePhase = n % static_cast<int>(rate / 4);
        const auto envelope = transient ? std::exp(-static_cast<double>(pulsePhase) / 900.0) : 1.0;
        const auto pulse = transient && pulsePhase < 32 ? 0.45 * (1.0 - pulsePhase / 32.0) : 0.0;
        const auto sample = static_cast<float>(0.18 * envelope * (std::sin(2.0 * pi * 110.0 * t)
            + 0.55 * std::sin(2.0 * pi * 2700.0 * t)) + pulse);
        source.setSample(0, n, sample);
        source.setSample(1, n, -sample);
    }
    wet.makeCopyOf(source);

    disdorktion::TanhSaturation saturation;
    if (!saturation.prepare({static_cast<double>(rate), 127, 2})) return false;
    const disdorktion::TanhSaturationSettings events[] = {
        {-24.0f, 0.0f, false}, {0.0f, 0.0f, false}, {12.0f, 0.0f, false},
        {36.0f, 0.0f, false}, {36.0f, -12.0f, false}, {12.0f, 0.0f, true},
        {-24.0f, 12.0f, false}, {0.0f, 0.0f, false}
    };
    for (int second = 0; second < seconds; ++second)
    {
        if (!saturation.setParameters(events[second])) return false;
        const auto segment = juce::dsp::AudioBlock<float>(wet).getSubBlock(static_cast<std::size_t>(second) * rate, rate);
        if (!saturation.process(segment)) return false;
    }
    const juce::String name = transient ? "transition-transients" : "transition-tones";
    return writeFloatWav(directory.getChildFile(name + "-source.wav"), source, rate)
        && writeFloatWav(directory.getChildFile(name + "-wet.wav"), wet, rate);
}

bool writeMatchedFixture(const juce::File& directory, const juce::String& label, float driveDb,
                         double& outputDb, double& rmsDeltaDb, double& commonPeakScale)
{
    constexpr unsigned rate = 48000;
    constexpr int frames = static_cast<int>(rate * 4);
    juce::AudioBuffer<float> source(2, frames), wet(2, frames);
    double inputPower = 0.0;
    double rawWetPower = 0.0;
    const auto drive = std::pow(10.0, static_cast<double>(driveDb) / 20.0);
    for (int n = 0; n < frames; ++n)
    {
        const auto t = static_cast<double>(n) / rate;
        const auto sample = static_cast<float>(0.2 * std::sin(2.0 * pi * 440.0 * t)
            + 0.08 * std::sin(2.0 * pi * 3700.0 * t));
        const auto rawWet = static_cast<float>(independentTanh(drive * sample));
        inputPower += static_cast<double>(sample) * sample;
        rawWetPower += static_cast<double>(rawWet) * rawWet;
        source.setSample(0, n, sample); source.setSample(1, n, -sample);
    }
    outputDb = std::clamp(10.0 * std::log10(inputPower / rawWetPower), -60.0, 12.0);
    wet.makeCopyOf(source);
    disdorktion::TanhSaturation saturation;
    if (!saturation.prepare({static_cast<double>(rate), 127, 2})
        || !saturation.setParameters({driveDb, static_cast<float>(outputDb), false})) return false;
    saturation.reset();
    if (!saturation.process(juce::dsp::AudioBlock<float>(wet))) return false;
    double wetPower = 0.0;
    double peak = 0.0;
    for (int n = 0; n < frames; ++n)
    {
        const auto value = static_cast<double>(wet.getSample(0, n));
        wetPower += value * value;
        peak = std::max(peak, std::abs(value));
    }
    rmsDeltaDb = 10.0 * std::log10(wetPower / inputPower);
    commonPeakScale = peak > 0.999 ? 0.999 / peak : 1.0;
    if (commonPeakScale < 1.0)
        for (int n = 0; n < frames; ++n)
            for (int channel = 0; channel < 2; ++channel)
            {
                source.setSample(channel, n, static_cast<float>(source.getSample(channel, n) * commonPeakScale));
                wet.setSample(channel, n, static_cast<float>(wet.getSample(channel, n) * commonPeakScale));
            }
    const auto stem = juce::String("matched-") + label;
    return std::abs(rmsDeltaDb) <= 0.5
        && writeFloatWav(directory.getChildFile(stem + "-source.wav"), source, rate)
        && writeFloatWav(directory.getChildFile(stem + "-wet.wav"), wet, rate);
}

bool writeCsv(const juce::File& file, const std::vector<std::pair<juce::String, FixtureResult>>& fixtures)
{
    if (file.exists()) return false;
    std::ofstream output(file.getFullPathName().toStdString(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "fixture,bin,frequency_hz,measured_magnitude,reference_magnitude,measured_dbc,reference_dbc\n";
    output << std::setprecision(17);
    for (const auto& fixture : fixtures)
    {
        const auto& bins = fixture.second.bins;
        double peak = 0.0;
        for (const auto& item : bins) peak = std::max(peak, item.second.first);
        for (const auto& item : bins)
        {
            const auto k = item.first;
            const auto actualDbc = 20.0 * std::log10(std::max(1.0e-30, item.second.first / peak));
            const auto referenceDbc = 20.0 * std::log10(std::max(1.0e-30, item.second.second / peak));
            if (!fixture.first.contains("two-tone") && actualDbc < -100.0 && referenceDbc < -100.0) continue;
            output << fixture.first << ',' << k << ','
                   << static_cast<double>(k) * fixture.second.sampleRate / fixture.second.fftSize << ','
                   << item.second.first << ',' << item.second.second << ',' << actualDbc << ',' << referenceDbc << '\n';
        }
    }
    output.close();
    return output.good();
}
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: disdorktion_tanh_saturation_measurements OUTPUT_DIRECTORY\n";
        return 2;
    }
    const auto requested = juce::String(argv[1]);
    const auto directory = juce::File::isAbsolutePath(requested)
        ? juce::File(requested) : juce::File::getCurrentWorkingDirectory().getChildFile(requested);
    if (directory.exists())
    {
        std::cerr << "Output directory must not exist; refusing to overwrite existing results\n";
        return 2;
    }
    if (directory.createDirectory().failed())
    {
        std::cerr << "Could not create output directory\n";
        return 2;
    }

    bool passed = true;
    std::vector<std::pair<juce::String, FixtureResult>> fixtures;
    juce::Array<juce::var> spectra;
    const std::array<double, 4> drives {-24.0, 0.0, 12.0, 36.0};
    for (const auto rate : {48000u, 96000u})
    {
        for (const auto drive : drives)
        {
            const auto suffix = "-" + juce::String(static_cast<int>(drive)) + "db";
            auto item = measure(directory, juce::String(rate) + "-sine1365" + suffix, rate, "sine", drive, 0.25, {1365, 0}, true);
            passed &= item.passed; spectra.add(item.json); fixtures.emplace_back(juce::String(rate) + "-sine1365" + suffix, std::move(item));
        }
        for (const auto descriptor : {std::array<int, 3>{9833, 0, 0}, std::array<int, 3>{997, 1499, 1},
                                      std::array<int, 3>{9833, 12289, 1}})
        {
            const auto isTwoTone = descriptor[2] != 0;
            const auto label = isTwoTone ? "two-tone" : "high-sine";
            const auto fixtureKind = isTwoTone ? "two-tone" : "high-sine";
            const auto amplitude = isTwoTone ? 0.125 : 0.25;
            const auto fixtureName = juce::String(rate) + "-" + label + "-" + juce::String(descriptor[0])
                + (isTwoTone ? "-" + juce::String(descriptor[1]) : "") + "-36db";
            const auto item = measure(directory, fixtureName, rate, fixtureKind, 36.0, amplitude,
                {descriptor[0], descriptor[1]}, true);
            passed &= item.passed; spectra.add(item.json); fixtures.emplace_back(fixtureName, item);
        }
        for (const auto sweepDrive : {0.0, 36.0})
        {
            const auto sweepName = juce::String(rate) + "-log-sweep-" + juce::String(static_cast<int>(sweepDrive)) + "db";
            const auto sweep = measure(directory, sweepName, rate, "log-sweep", sweepDrive, 0.25, {0, 0}, false);
            passed &= sweep.passed; spectra.add(sweep.json); fixtures.emplace_back(sweepName, sweep);
        }
    }

    double mildOutputDb = 0.0, extremeOutputDb = 0.0, mildRmsDeltaDb = 0.0, extremeRmsDeltaDb = 0.0;
    double mildCommonScale = 1.0, extremeCommonScale = 1.0;
    const bool listeningFixtures = writeListeningFixtures(directory, false) && writeListeningFixtures(directory, true)
        && writeMatchedFixture(directory, "mild", 0.0f, mildOutputDb, mildRmsDeltaDb, mildCommonScale)
        && writeMatchedFixture(directory, "extreme", 36.0f, extremeOutputDb, extremeRmsDeltaDb, extremeCommonScale);
    passed &= listeningFixtures;
    auto* report = new juce::DynamicObject;
    report->setProperty("tool", "disdorktion_tanh_saturation_measurements");
    report->setProperty("schemaVersion", 1);
    report->setProperty("spectra", juce::var(spectra));
    report->setProperty("latencySamples", 0);
    report->setProperty("listeningStatus", "Pending; generated fixtures are not user listening observations.");
    report->setProperty("listeningFixturesWritten", listeningFixtures);
    report->setProperty("transitionSchedule", "48000 Hz stereo; one control state per second; driveDb/outputDb/bypass: -24/0/false, 0/0/false, 12/0/false, 36/0/false, 36/-12/false, 12/0/true, -24/12/false, 0/0/false. Generated tone and transient fixtures are synthetic and are not listening observations.");
    auto* matchedSettings = new juce::DynamicObject;
    matchedSettings->setProperty("sampleRateHz", 48000);
    matchedSettings->setProperty("program", "Generated 440 Hz and 3700 Hz tones; four seconds; stereo polarity inverted.");
    matchedSettings->setProperty("mildDriveDb", 0.0);
    matchedSettings->setProperty("mildOutputDbForRmsMatch", mildOutputDb);
    matchedSettings->setProperty("mildActualWetRmsDeltaDb", mildRmsDeltaDb);
    matchedSettings->setProperty("mildCommonPeakScale", mildCommonScale);
    matchedSettings->setProperty("extremeDriveDb", 36.0);
    matchedSettings->setProperty("extremeOutputDbForRmsMatch", extremeOutputDb);
    matchedSettings->setProperty("extremeActualWetRmsDeltaDb", extremeRmsDeltaDb);
    matchedSettings->setProperty("extremeCommonPeakScale", extremeCommonScale);
    matchedSettings->setProperty("rmsAcceptanceDb", 0.5);
    matchedSettings->setProperty("matchingMethod", "Set output trim from independent double tanh-reference RMS over the fixture; actual wet RMS is checked against source within the stated limit. Apply the same additional scale to source and wet only if a peak exceeds 0.999. This is generated material, not user listening evidence.");
    report->setProperty("matchedListeningFixtureSettings", juce::var(matchedSettings));
    juce::Array<juce::var> rates; rates.add(48000); rates.add(96000);
    report->setProperty("processingRatesHz", rates);
    report->setProperty("floatWav", true);
    report->setProperty("passed", passed);
    report->setProperty("failureMeaning", "A failed numerical or spectral acceptance check is preserved in result.json; inspect fixture flags and limits.");
    const auto json = juce::JSON::toString(juce::var(report), true);
    const bool outputsWritten = directory.getChildFile("result.json").replaceWithText(json)
        && writeCsv(directory.getChildFile("spectra.csv"), fixtures);
    if (!outputsWritten)
    {
        std::cerr << "Failed to write complete measurement report; output directory retained for diagnosis\n";
        return 2;
    }
    std::cout << "Wrote " << spectra.size() << " spectral fixtures and listening WAVs to "
              << directory.getFullPathName() << "; " << (passed ? "passed" : "FAILED; inspect result.json") << '\n';
    return passed ? 0 : 1;
}
