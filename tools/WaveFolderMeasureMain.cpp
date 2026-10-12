#include "dsp/WaveFolder.h"
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
constexpr int coherentFrames = 65536;
constexpr double dbFloor = -600.0;

struct Fixture
{
    juce::String name;
    unsigned rate = 0;
    int fftFrames = 0;
    double referencePeak = 0.0, windowSum = 0.0;
    std::vector<float> measured, reference;
    juce::var json;
    bool passed = false;
};

// Cell index and parity are independent of the production modulo branches.
long double foldReference(long double x, long double drive, long double threshold, long double gain)
{
    const auto v = std::abs(drive * x) / threshold;
    const auto cell = std::floor((v + 1.0L) / 2.0L);
    const auto q = v - 2.0L * cell;
    const auto parity = std::fmod(cell, 2.0L) == 0.0L ? 1.0L : -1.0L;
    return gain * threshold * q * parity * (std::signbit(x) ? -1.0L : 1.0L);
}

double db(double amplitude)
{
    return amplitude > 0.0 && std::isfinite(amplitude)
        ? std::max(dbFloor, 20.0 * std::log10(amplitude)) : dbFloor;
}

juce::var relativeDb(double magnitude, double peak)
{
    return peak > 0.0 ? juce::var(db(magnitude / peak)) : juce::var();
}

juce::var settingsJson(const disdorktion::WaveFolderSettings& settings)
{
    auto* item = new juce::DynamicObject;
    item->setProperty("driveDb", settings.driveDb);
    item->setProperty("threshold", settings.threshold);
    item->setProperty("outputDb", settings.outputDb);
    item->setProperty("bypass", settings.bypass);
    return juce::var(item);
}

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

double amplitudeScale(const Fixture& fixture, int bin)
{
    return (bin == 0 || bin == fixture.fftFrames / 2 ? 1.0 : 2.0) / fixture.windowSum;
}

juce::var binJson(const Fixture& fixture, int bin)
{
    auto* item = new juce::DynamicObject;
    item->setProperty("bin", bin);
    item->setProperty("frequencyHz", static_cast<double>(bin) * fixture.rate / fixture.fftFrames);
    item->setProperty("measuredDbr", relativeDb(fixture.measured[bin], fixture.referencePeak));
    item->setProperty("referenceDbr", relativeDb(fixture.reference[bin], fixture.referencePeak));
    item->setProperty("measuredDbfs", db(fixture.measured[bin] * amplitudeScale(fixture, bin)));
    item->setProperty("referenceDbfs", db(fixture.reference[bin] * amplitudeScale(fixture, bin)));
    return juce::var(item);
}

Fixture measure(const juce::File& directory, const juce::String& name, unsigned rate,
                const juce::String& kind, const disdorktion::WaveFolderSettings& settings,
                double inputAmplitude, std::array<int, 2> inputBins)
{
    Fixture result;
    result.name = name;
    result.rate = rate;
    const bool sweep = kind == "log-sweep";
    const bool singleSine = kind == "sine";
    const int frames = sweep ? static_cast<int>(rate * 2) : coherentFrames;
    int order = 0;
    result.fftFrames = 1;
    while (result.fftFrames < frames) { result.fftFrames <<= 1; ++order; }
    juce::AudioBuffer<float> source(1, frames), wet(1, frames);
    for (int n = 0; n < frames; ++n)
    {
        double value = 0.0;
        if (singleSine || kind == "two-tone")
        {
            value = inputAmplitude * std::sin(2.0 * pi * inputBins[0] * n / frames);
            if (kind == "two-tone") value += inputAmplitude * std::sin(2.0 * pi * inputBins[1] * n / frames);
        }
        else if (sweep)
        {
            const auto t = static_cast<double>(n) / rate;
            const auto exponent = std::log(0.45 * rate / 20.0) / 2.0;
            value = inputAmplitude * std::sin(2.0 * pi * 20.0 * std::expm1(exponent * t) / exponent);
        }
        source.setSample(0, n, static_cast<float>(value));
    }
    wet.makeCopyOf(source);
    disdorktion::WaveFolder folder;
    bool good = folder.prepare({static_cast<double>(rate), 127, 1}) && folder.setParameters(settings);
    folder.reset();
    {
        const juce::ScopedNoDenormals noDenormals;
        good &= folder.process(juce::dsp::AudioBlock<float>(wet));
    }
    // Match the actual float controls, with double gain conversion as specified.
    const auto drive = std::pow(10.0, static_cast<double>(settings.driveDb) / 20.0);
    const auto gain = std::pow(10.0, static_cast<double>(settings.outputDb) / 20.0);
    double maxError = 0.0;
    bool finite = true;
    result.measured.assign(static_cast<std::size_t>(2 * result.fftFrames), 0.0f);
    result.reference.assign(static_cast<std::size_t>(2 * result.fftFrames), 0.0f);
    for (int n = 0; n < frames; ++n)
    {
        const auto input = source.getSample(0, n);
        const auto expected = settings.bypass ? static_cast<long double>(input)
            : foldReference(input, drive, settings.threshold, gain);
        const auto actual = wet.getSample(0, n);
        finite &= std::isfinite(actual);
        if (std::isfinite(actual)) maxError = std::max(maxError, static_cast<double>(std::abs(actual - expected)));
        const auto window = sweep ? 0.5 - 0.5 * std::cos(2.0 * pi * n / (frames - 1)) : 1.0;
        result.windowSum += window;
        result.measured[n] = std::isfinite(actual) ? static_cast<float>(actual * window) : 0.0f;
        result.reference[n] = static_cast<float>(expected * window);
    }
    juce::dsp::FFT fft(order);
    fft.performFrequencyOnlyForwardTransform(result.measured.data());
    fft.performFrequencyOnlyForwardTransform(result.reference.data());
    const int nyquist = result.fftFrames / 2;
    result.referencePeak = *std::max_element(result.reference.begin() + 1, result.reference.begin() + nyquist + 1);
    double maxSpectralErrorDb = 0.0, totalACPower = 0.0, referenceACPower = 0.0, evenPeak = 0.0;
    int significantBins = 0;
    for (int bin = 0; bin <= nyquist; ++bin)
    {
        const auto actual = static_cast<double>(result.measured[bin]);
        const auto expected = static_cast<double>(result.reference[bin]);
        finite &= std::isfinite(actual) && std::isfinite(expected);
        if (expected > result.referencePeak * 1.0e-4 && result.referencePeak > 0.0)
        {
            ++significantBins;
            maxSpectralErrorDb = std::max(maxSpectralErrorDb, std::abs(db(actual / expected)));
        }
        if (bin != 0)
        {
            const auto multiplicity = bin == nyquist ? 1.0 : 2.0;
            totalACPower += multiplicity * actual * actual;
            referenceACPower += multiplicity * expected * expected;
            if (bin % 2 == 0) evenPeak = std::max(evenPeak, actual);
        }
    }
    const bool normalizationValid = result.referencePeak > 0.0;
    const auto dcDbr = normalizationValid ? db(result.measured[0] / result.referencePeak) : dbFloor;
    const auto evenDbr = normalizationValid ? db(evenPeak / result.referencePeak) : dbFloor;
    const auto tolerance = 2.0e-7 * std::max(1.0, gain * static_cast<double>(settings.threshold));
    good &= finite && maxError <= tolerance && maxSpectralErrorDb <= 0.1;
    good &= !singleSine || (dcDbr <= -100.0 && evenDbr <= -100.0);
    // Silence has no reference peak: require exact zero, and report null dBr/THD.
    if (!normalizationValid) good &= totalACPower == 0.0 && result.measured[0] == 0.0f;

    auto* report = new juce::DynamicObject;
    report->setProperty("name", name);
    report->setProperty("fixture", kind);
    report->setProperty("settings", settingsJson(settings));
    report->setProperty("sampleRateHz", static_cast<int>(rate));
    report->setProperty("channels", 1);
    report->setProperty("frames", frames);
    report->setProperty("preparedBlockFrames", 127);
    report->setProperty("fftFrames", result.fftFrames);
    report->setProperty("inputAmplitudePerTone", inputAmplitude);
    juce::Array<juce::var> bins, frequencies;
    for (const auto bin : inputBins)
    {
        bins.add(bin);
        frequencies.add(static_cast<double>(bin) * rate / frames);
    }
    report->setProperty("inputBins", bins);
    report->setProperty("inputFrequenciesHz", frequencies);
    report->setProperty("window", sweep ? "symmetric Hann; zero padded to fftFrames" : "rectangular; coherent bins");
    report->setProperty("windowSum", result.windowSum);
    report->setProperty("dbfsScaling", "FFT magnitude/windowSum; multiply by 2 for bins other than DC and Nyquist; peak amplitude dBFS");
    report->setProperty("dbrNormalization", "Largest non-DC reference magnitude including Nyquist; undefined for zero reference AC power");
    report->setProperty("referencePeakMagnitude", result.referencePeak);
    report->setProperty("dbrNormalizationValid", normalizationValid);
    report->setProperty("dbFloor", dbFloor);
    report->setProperty("independentReference", "long double cell/parity: v=abs(a*x)/T; k=floor((v+1)/2); q=v-2*k; g*T*q*(-1)^k*sign(x), evaluated from the saved float source and float controls");
    report->setProperty("maxSampleReferenceError", maxError);
    report->setProperty("sampleReferenceTolerance", tolerance);
    report->setProperty("allFinite", finite);
    report->setProperty("significantReferenceBinThresholdDbr", -80.0);
    report->setProperty("significantReferenceBinCount", significantBins);
    report->setProperty("maxSignificantBinDeviationDb", maxSpectralErrorDb);
    report->setProperty("significantBinDeviationLimitDb", 0.1);
    report->setProperty("dcDbr", relativeDb(result.measured[0], result.referencePeak));
    report->setProperty("maxEvenIndexDbr", relativeDb(evenPeak, result.referencePeak));
    report->setProperty("symmetryLimitDbr", -100.0);
    report->setProperty("symmetryAcceptanceApplicable", singleSine);
    report->setProperty("totalACPowerFFTUnits", totalACPower);
    report->setProperty("referenceACPowerFFTUnits", referenceACPower);
    if (sweep)
    {
        report->setProperty("sweepStartHz", 20.0);
        report->setProperty("sweepEndHz", 0.45 * rate);
        report->setProperty("sweepDurationSeconds", 2.0);
        report->setProperty("sweepPhase", "2*pi*20*expm1(log(0.45*rate/20)/2*t)/(log(0.45*rate/20)/2)");
    }
    if (singleSine || kind == "silence")
    {
        const auto fundamental = singleSine ? static_cast<double>(result.measured[inputBins[0]]) : 0.0;
        const auto fundamentalPower = 2.0 * fundamental * fundamental;
        const auto residualRatio = totalACPower > 0.0
            ? std::sqrt(std::max(0.0, totalACPower - fundamentalPower) / totalACPower) : 0.0;
        const bool thdValid = singleSine && normalizationValid && fundamental > 0.0
            && db(fundamental / result.referencePeak) >= -40.0;
        double harmonicPower = 0.0;
        juce::Array<juce::var> oddHarmonics, foldbacks;
        if (singleSine)
        {
            report->setProperty("fundamental", binJson(result, inputBins[0]));
            for (int harmonic = 3; harmonic * inputBins[0] <= nyquist; harmonic += 2)
            {
                const auto bin = harmonic * inputBins[0];
                harmonicPower += (bin == nyquist ? 1.0 : 2.0) * static_cast<double>(result.measured[bin]) * result.measured[bin];
                auto item = binJson(result, bin);
                item.getDynamicObject()->setProperty("order", harmonic);
                oddHarmonics.add(item);
            }
            for (int harmonic = 3; harmonic <= 255; harmonic += 2)
            {
                const auto rawBin = harmonic * inputBins[0];
                if (rawBin <= nyquist) continue;
                const auto wrapped = rawBin % result.fftFrames;
                const auto bin = std::min(wrapped, result.fftFrames - wrapped);
                auto item = binJson(result, bin);
                auto* object = item.getDynamicObject();
                object->setProperty("sourceHarmonicOrder", harmonic);
                object->setProperty("sourceFrequencyHz", static_cast<double>(rawBin) * rate / result.fftFrames);
                object->setProperty("coincidesWithNativeOddHarmonic", bin % inputBins[0] == 0 && (bin / inputBins[0]) % 2 == 1);
                foldbacks.add(item);
            }
        }
        report->setProperty("oddHarmonicsBelowNyquist", oddHarmonics);
        report->setProperty("foldedOddHarmonicLocationsOrders3to255", foldbacks);
        report->setProperty("nonFundamentalRmsRatio", residualRatio);
        report->setProperty("nonFundamentalRmsRatioDefinition", "sqrt(max(0,totalACPower-fundamentalPower)/totalACPower); exactly 0 when totalACPower=0; includes aliased and residual components");
        report->setProperty("conventionalTHDValid", thdValid);
        report->setProperty("conventionalTHDRatio", thdValid ? juce::var(std::sqrt(harmonicPower / fundamentalPower)) : juce::var());
        report->setProperty("conventionalTHDDb", thdValid ? juce::var(db(std::sqrt(harmonicPower / fundamentalPower))) : juce::var());
        report->setProperty("conventionalTHDDefinition", "RMS of measured odd harmonics 3 and above whose unfolded frequencies are at/below Nyquist divided by measured fundamental RMS; null below -40 dBr or for zero power. Aliased contributions can collide with these bins.");
        report->setProperty("foldbackCollisionLimit", "Each location is a measured bin, not an isolated harmonic amplitude. Higher odd orders beyond this finite list and native harmonics can share bins and interfere; no summed alias energy or alias rejection metric is inferred.");
    }
    if (kind == "two-tone")
    {
        juce::Array<juce::var> locations;
        for (int first = -7; first <= 7; ++first)
            for (int second = -7; second <= 7; ++second)
            {
                const int degree = std::abs(first) + std::abs(second);
                if (degree < 3 || degree > 7 || degree % 2 == 0 || first == 0 || second == 0) continue;
                const auto raw = std::abs(first * inputBins[0] + second * inputBins[1]);
                if (raw == 0) continue;
                const auto wrapped = raw % result.fftFrames;
                const auto bin = std::min(wrapped, result.fftFrames - wrapped);
                auto item = binJson(result, bin);
                auto* object = item.getDynamicObject();
                object->setProperty("firstToneMultiplier", first);
                object->setProperty("secondToneMultiplier", second);
                object->setProperty("unfoldedFrequencyHz", static_cast<double>(raw) * rate / result.fftFrames);
                object->setProperty("foldedAboveNyquist", raw > nyquist);
                locations.add(item);
            }
        report->setProperty("intermodulationLocationsThroughOrder7", locations);
        report->setProperty("intermodulationInterpretation", "Candidate odd-degree sum/difference locations, with duplicates retained to expose collisions. Each measured bin can contain multiple products, including higher orders; this is not isolated product energy.");
    }
    std::vector<int> ranked(static_cast<std::size_t>(nyquist));
    std::iota(ranked.begin(), ranked.end(), 1);
    std::partial_sort(ranked.begin(), ranked.begin() + 20, ranked.end(), [&](int a, int b) { return result.measured[a] > result.measured[b]; });
    juce::Array<juce::var> top;
    for (int n = 0; n < 20; ++n) top.add(binJson(result, ranked[n]));
    report->setProperty("topSpectralBins", top);
    report->setProperty("aliasingInterpretation", "Native sampled-reference agreement verifies transfer implementation fidelity. Native harmonics, intermodulation, and foldback patterns characterize a baseline for Q1; no alias suppression pass is claimed.");
    good &= writeFloatWav(directory.getChildFile(name + "-source.wav"), source, rate);
    good &= writeFloatWav(directory.getChildFile(name + "-wet.wav"), wet, rate);
    result.passed = good;
    report->setProperty("passed", good);
    result.json = juce::var(report);
    return result;
}

bool writeCsv(const juce::File& file, const std::vector<Fixture>& fixtures)
{
    if (file.exists()) return false;
    std::ofstream output(file.getFullPathName().toStdString(), std::ios::out | std::ios::binary);
    output << "fixture,bin,frequency_hz,measured_fft_magnitude,reference_fft_magnitude,measured_dbfs,reference_dbfs,measured_dbr,reference_dbr,dbr_valid\n" << std::setprecision(17);
    for (const auto& fixture : fixtures)
        for (int bin = 0; bin <= fixture.fftFrames / 2; ++bin)
        {
            const auto actual = static_cast<double>(fixture.measured[bin]);
            const auto expected = static_cast<double>(fixture.reference[bin]);
            output << fixture.name << ',' << bin << ',' << static_cast<double>(bin) * fixture.rate / fixture.fftFrames
                   << ',' << actual << ',' << expected << ',' << db(actual * amplitudeScale(fixture, bin))
                   << ',' << db(expected * amplitudeScale(fixture, bin)) << ',';
            if (fixture.referencePeak > 0.0)
                output << db(actual / fixture.referencePeak) << ',' << db(expected / fixture.referencePeak) << ",true\n";
            else output << ",,false\n";
        }
    output.close();
    return output.good();
}

juce::var transitionFixtures(const juce::File& directory, bool transient)
{
    constexpr unsigned rate = 48000;
    constexpr int seconds = 8, frames = static_cast<int>(rate * seconds);
    const std::array<disdorktion::WaveFolderSettings, seconds> events {{
        {-24.0f, 1.0f, 0.0f, false}, {6.0f, .5f, 0.0f, false},
        {18.0f, .25f, 0.0f, false}, {36.0f, .05f, 0.0f, false},
        {36.0f, 1.0f, -12.0f, false}, {12.0f, .5f, 0.0f, true},
        {-24.0f, .05f, 12.0f, true}, {0.0f, 1.0f, 0.0f, false}
    }};
    juce::AudioBuffer<float> source(2, frames), wet(2, frames);
    for (int n = 0; n < frames; ++n)
    {
        const auto t = static_cast<double>(n) / rate;
        const auto pulseFrame = n % static_cast<int>(rate / 4);
        const auto envelope = transient ? std::exp(-static_cast<double>(pulseFrame) / 900.0) : 1.0;
        const auto pulse = transient && pulseFrame < 32 ? .45 * (1.0 - pulseFrame / 32.0) : 0.0;
        const auto value = static_cast<float>(.18 * envelope * (std::sin(2.0 * pi * 110.0 * t)
            + .55 * std::sin(2.0 * pi * 2700.0 * t)) + pulse);
        source.setSample(0, n, value); source.setSample(1, n, -value);
    }
    wet.makeCopyOf(source);
    disdorktion::WaveFolder folder;
    bool good = folder.prepare({rate, 127, 2});
    juce::Array<juce::var> schedule;
    for (int second = 0; second < seconds; ++second)
    {
        auto event = settingsJson(events[second]);
        event.getDynamicObject()->setProperty("frame", second * static_cast<int>(rate));
        schedule.add(event);
        good &= folder.setParameters(events[second]);
        const juce::ScopedNoDenormals noDenormals;
        good &= folder.process(juce::dsp::AudioBlock<float>(wet).getSubBlock(static_cast<std::size_t>(second) * rate, rate));
    }
    const juce::String name = transient ? "transition-transients" : "transition-tones";
    good &= writeFloatWav(directory.getChildFile(name + "-source.wav"), source, rate);
    good &= writeFloatWav(directory.getChildFile(name + "-wet.wav"), wet, rate);
    auto* report = new juce::DynamicObject;
    report->setProperty("name", name);
    report->setProperty("sampleRateHz", static_cast<int>(rate));
    report->setProperty("channels", 2);
    report->setProperty("frames", frames);
    report->setProperty("preparedBlockFrames", 127);
    report->setProperty("events", schedule);
    report->setProperty("initialSettings", settingsJson({0.0f, 1.0f, 0.0f, false}));
    report->setProperty("rampFrames", 480);
    report->setProperty("rampConvention", "Independent linear a,T,g,b ramps; advance before applying each frame. Initial prepared defaults; no reset between events. Second channel is source polarity inverse.");
    report->setProperty("sourceDefinition", transient
        ? "0.18*exp(-(n%12000)/900)*(sin(2*pi*110*t)+0.55*sin(2*pi*2700*t)) plus 0.45*(1-(n%12000)/32) for first 32 frames each pulse"
        : "0.18*(sin(2*pi*110*t)+0.55*sin(2*pi*2700*t)); t=n/48000");
    report->setProperty("listeningStatus", "Synthetic transition material; human listening pending");
    report->setProperty("passed", good);
    return juce::var(report);
}

juce::var matchedFixture(const juce::File& directory, bool aggressive)
{
    constexpr unsigned rate = 48000;
    constexpr int frames = 4 * static_cast<int>(rate);
    disdorktion::WaveFolderSettings settings {aggressive ? 18.0f : 6.0f, aggressive ? .25f : .5f, 0.0f, false};
    juce::AudioBuffer<float> source(2, frames), wet(2, frames);
    const auto drive = std::pow(10.0, static_cast<double>(settings.driveDb) / 20.0);
    double sourcePower = 0.0, referencePower = 0.0;
    for (int n = 0; n < frames; ++n)
    {
        const auto t = static_cast<double>(n) / rate;
        const auto value = static_cast<float>(.2 * std::sin(2.0 * pi * 440.0 * t) + .08 * std::sin(2.0 * pi * 3700.0 * t));
        source.setSample(0, n, value); source.setSample(1, n, -value);
        sourcePower += static_cast<double>(value) * value;
        const auto reference = foldReference(value, drive, settings.threshold, 1.0L);
        referencePower += static_cast<double>(reference * reference);
    }
    const bool powersValid = sourcePower > 0.0 && referencePower > 0.0;
    settings.outputDb = powersValid ? static_cast<float>(std::clamp(10.0 * std::log10(sourcePower / referencePower), -60.0, 12.0)) : 0.0f;
    wet.makeCopyOf(source);
    disdorktion::WaveFolder folder;
    bool good = powersValid && folder.prepare({rate, 127, 2}) && folder.setParameters(settings);
    folder.reset();
    {
        const juce::ScopedNoDenormals noDenormals;
        good &= folder.process(juce::dsp::AudioBlock<float>(wet));
    }
    double wetPower = 0.0, peak = 0.0;
    for (int n = 0; n < frames; ++n)
    {
        const auto value = static_cast<double>(wet.getSample(0, n));
        wetPower += value * value;
        peak = std::max({peak, std::abs(value), std::abs(static_cast<double>(source.getSample(0, n)))});
    }
    const bool rmsValid = wetPower > 0.0 && sourcePower > 0.0;
    const auto delta = rmsValid ? 10.0 * std::log10(wetPower / sourcePower) : 0.0;
    const auto commonScale = peak > .999 ? .999 / peak : 1.0;
    source.applyGain(static_cast<float>(commonScale)); wet.applyGain(static_cast<float>(commonScale));
    const juce::String name = aggressive ? "matched-aggressive" : "matched-mild";
    good &= rmsValid && std::abs(delta) <= .5;
    good &= writeFloatWav(directory.getChildFile(name + "-source.wav"), source, rate);
    good &= writeFloatWav(directory.getChildFile(name + "-wet.wav"), wet, rate);
    auto* report = new juce::DynamicObject;
    report->setProperty("name", name);
    report->setProperty("sampleRateHz", static_cast<int>(rate));
    report->setProperty("channels", 2);
    report->setProperty("frames", frames);
    report->setProperty("settings", settingsJson(settings));
    report->setProperty("sourceDefinition", "0.2*sin(2*pi*440*t)+0.08*sin(2*pi*3700*t), t=n/48000; second channel polarity inverse; common scale applied to both saved files");
    report->setProperty("actualWetRmsDeltaDb", rmsValid ? juce::var(delta) : juce::var());
    report->setProperty("rmsMatchLimitDb", .5);
    report->setProperty("commonPeakScale", commonScale);
    report->setProperty("matchingMethod", "Trim from independent reference RMS; check rendered RMS; common peak scale for source and wet if needed. These saved signals are comparison material, not listening observations.");
    report->setProperty("passed", good);
    return juce::var(report);
}
}

int main(int argc, char** argv)
{
    if (argc != 2) { std::cerr << "Usage: disdorktion_wave_folder_measurements OUTPUT_DIRECTORY\n"; return 2; }
    const auto requested = juce::String(argv[1]);
    const auto directory = juce::File::isAbsolutePath(requested) ? juce::File(requested)
        : juce::File::getCurrentWorkingDirectory().getChildFile(requested);
    if (directory.exists())
    { std::cerr << "Output directory must not exist; refusing to alter previous results\n"; return 2; }
    if (directory.createDirectory().failed()) { std::cerr << "Cannot create output directory\n"; return 2; }
    bool passed = true;
    std::vector<Fixture> fixtures;
    juce::Array<juce::var> spectra, transitions, matched;
    auto add = [&](Fixture fixture)
    {
        passed &= fixture.passed;
        spectra.add(fixture.json);
        fixtures.push_back(std::move(fixture));
    };
    for (const auto rate : {48000u, 96000u})
    {
        for (const auto drive : {-24.0f, 0.0f, 12.0f, 36.0f})
            for (const auto threshold : {1.0f, .5f, .05f})
                for (const auto bin : {1365, 9833})
                {
                    const auto name = juce::String(rate) + "-sine" + juce::String(bin) + "-drive" + juce::String(drive, 0)
                        + "-threshold" + juce::String(threshold, 2);
                    add(measure(directory, name, rate, "sine", {drive, threshold, 0.0f, false}, .25, {bin, 0}));
                }
        for (const auto aggressive : {false, true})
        {
            const disdorktion::WaveFolderSettings settings {aggressive ? 18.0f : 6.0f, aggressive ? .25f : .5f, 0.0f, false};
            const juce::String label = aggressive ? "aggressive" : "mild";
            for (const auto bins : {std::array<int, 2>{997, 1499}, std::array<int, 2>{9833, 12289}})
                add(measure(directory, juce::String(rate) + "-two-tone-" + juce::String(bins[0]) + "-" + juce::String(bins[1]) + "-" + label,
                            rate, "two-tone", settings, .125, bins));
            add(measure(directory, juce::String(rate) + "-log-sweep-" + label, rate, "log-sweep", settings, .25, {0, 0}));
        }
        add(measure(directory, juce::String(rate) + "-silence", rate, "silence", {36.0f, .05f, 12.0f, false}, 0.0, {0, 0}));
    }
    for (const bool transient : {false, true})
    {
        auto result = transitionFixtures(directory, transient);
        passed &= static_cast<bool>(result.getProperty("passed", false));
        transitions.add(result);
    }
    for (const bool aggressive : {false, true})
    {
        auto result = matchedFixture(directory, aggressive);
        passed &= static_cast<bool>(result.getProperty("passed", false));
        matched.add(result);
    }
    const bool csvWritten = writeCsv(directory.getChildFile("spectra.csv"), fixtures);
    passed &= csvWritten;
    auto* report = new juce::DynamicObject;
    report->setProperty("tool", "disdorktion_wave_folder_measurements");
    report->setProperty("schemaVersion", 1);
    report->setProperty("createdUtc", juce::Time::getCurrentTime().toISO8601(true));
    report->setProperty("juceVersion", juce::SystemStats::getJUCEVersion());
#if defined(DISDORKTION_BUILD_ID)
    report->setProperty("buildId", DISDORKTION_BUILD_ID);
#else
    report->setProperty("buildId", "unavailable");
#endif
#if defined(DISDORKTION_GIT_REVISION)
    report->setProperty("gitRevision", DISDORKTION_GIT_REVISION);
#else
    report->setProperty("gitRevision", "unavailable; configure DISDORKTION_GIT_REVISION to record source revision");
#endif
#if defined(_MSC_FULL_VER)
    report->setProperty("compiler", "MSVC " + juce::String(_MSC_FULL_VER));
#elif defined(__clang__)
    report->setProperty("compiler", "Clang " __clang_version__);
#elif defined(__GNUC__)
    report->setProperty("compiler", "GCC " __VERSION__);
#else
    report->setProperty("compiler", "unavailable");
#endif
#if defined(NDEBUG)
    report->setProperty("configuration", "Release");
#else
    report->setProperty("configuration", "Debug");
#endif
    report->setProperty("buildDate", __DATE__ " " __TIME__);
    report->setProperty("operatingSystem", juce::SystemStats::getOperatingSystemName());
    report->setProperty("spectra", spectra);
    report->setProperty("transitionFixtures", transitions);
    report->setProperty("matchedListeningFixtures", matched);
    report->setProperty("latencySamples", 0);
    report->setProperty("floatWavBits", 32);
    report->setProperty("listeningStatus", "Pending; generated WAVs are synthetic comparison material and are not user listening evidence");
    report->setProperty("qualityStatus", "Native-rate baseline for Q1; no anti-aliasing acceptance pass");
    report->setProperty("failureMeaning", "Numerical, symmetry, significant-bin, and I/O failures are retained for diagnosis. Human listening and CPU qualification are separate gates.");
    report->setProperty("spectraCsvWritten", csvWritten);
    report->setProperty("passed", passed);
    if (!directory.getChildFile("result.json").replaceWithText(juce::JSON::toString(juce::var(report), true)))
    { std::cerr << "Failed to write result.json; destination retained\n"; return 2; }
    std::cout << "Wrote " << spectra.size() << " spectral fixtures to " << directory.getFullPathName()
              << "; " << (passed ? "passed" : "FAILED; inspect result.json") << '\n';
    return passed ? 0 : 1;
}
