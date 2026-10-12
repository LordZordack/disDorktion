#include "dsp/WaveFolder.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace disdorktion;
static_assert(Module<WaveFolder, WaveFolderSettings>);

namespace
{
double amplitude(float db) { return std::pow(10.0, static_cast<double>(db) / 20.0); }
long double reference(long double x, long double a, long double t, long double g, long double b = 0)
{
    if (b == 1) return x;
    const auto v = std::abs(a * x) / t;
    const auto cell = static_cast<long long>(std::floor((v + 1) / 2));
    const auto q = v - 2 * cell;
    const auto positive = t * (cell % 2 == 0 ? q : -q);
    const auto wet = g * (std::signbit(x) ? -positive : positive);
    return b == 0 ? wet : b * x + (1 - b) * wet;
}
double tolerance(double t, double g) { return 2e-7 * std::max(1.0, t * g); }
void near(float actual, long double expected, double t, double g)
{
    REQUIRE(std::isfinite(actual));
    REQUIRE(std::abs(static_cast<long double>(actual) - expected) <= tolerance(t, g));
}
void fill(juce::AudioBuffer<float>& audio, float x)
{
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int n = 0; n < audio.getNumSamples(); ++n) audio.setSample(c, n, c == 0 ? x : -x);
}
struct IndependentRamp
{
    double current, start, target;
    int elapsed = 0;
    void retarget(double value)
    {
        if (value != target) { start = current; target = value; elapsed = 0; }
    }
    double next(int frames)
    {
        elapsed = std::min(elapsed + 1, frames);
        current = elapsed == frames ? target : start + (target - start) * static_cast<double>(elapsed) / frames;
        return current;
    }
};
}

TEST_CASE("Wave folder settled curve matches cell parity oracle and wet bounds", "[wave-folder]")
{
    for (float drive : {-24.f, 0.f, 12.f, 36.f})
        for (float threshold : {.05f, .5f, 1.f})
            for (float trim : {-60.f, 0.f, 12.f})
            {
                CAPTURE(drive, threshold, trim);
                WaveFolder dsp;
                REQUIRE(dsp.setParameters({drive, threshold, trim, false}));
                REQUIRE(dsp.prepare({48000, 32, 2}));
                juce::AudioBuffer<float> audio(2, 2055);
                for (int n = 0; n <= 2048; ++n) audio.setSample(0, n, -4.f + n / 256.f);
                const float tiny[] = {0.f, -0.f, 1e-20f, -1e-20f, std::numeric_limits<float>::min(),
                    std::numeric_limits<float>::denorm_min()};
                for (int n = 0; n < 6; ++n) audio.setSample(0, 2049 + n, tiny[n]);
                for (int n = 0; n < audio.getNumSamples(); ++n) audio.setSample(1, n, -audio.getSample(0, n));
                const auto original = audio;
                REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
                const auto a = amplitude(drive), g = amplitude(trim);
                for (int n = 0; n < audio.getNumSamples(); ++n)
                {
                    near(audio.getSample(0, n), reference(original.getSample(0, n), a, threshold, g), threshold, g);
                    REQUIRE(audio.getSample(1, n) == -audio.getSample(0, n));
                    REQUIRE(std::abs(audio.getSample(0, n)) <= g * threshold + tolerance(threshold, g));
                }
                REQUIRE(audio.getSample(0, 2049) == 0.f);
                REQUIRE(dsp.getLatencySamples() == 0);
            }
}

TEST_CASE("Wave folder default has exact linear identity and alternating fold landmarks", "[wave-folder]")
{
    WaveFolder dsp; REQUIRE(dsp.prepare({48000, 1, 1}));
    juce::AudioBuffer<float> audio(1, 2060);
    for (int n = 0; n <= 2048; ++n) audio.setSample(0, n, -1.f + n / 1024.f);
    const float extra[] = {std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::min(), -std::numeric_limits<float>::min(), 1e-20f, -1e-20f, -0.f, 2.f, 3.f, 4.f, 5.f};
    for (int n = 0; n < 11; ++n) audio.setSample(0, 2049 + n, extra[n]);
    const auto original = audio; REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
    for (int n = 0; n < 2056; ++n) REQUIRE(audio.getSample(0, n) == original.getSample(0, n));
    REQUIRE(audio.getSample(0, 2056) == 0.f);
    REQUIRE(audio.getSample(0, 2057) == -1.f);
    REQUIRE(audio.getSample(0, 2058) == 0.f);
    REQUIRE(audio.getSample(0, 2059) == 1.f);
    for (int n = -8; n <= 8; ++n)
    {
        audio.setSample(0, 0, static_cast<float>(n));
        REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio).getSubBlock(0, 1)));
        REQUIRE(audio.getSample(0, 0) == static_cast<float>(reference(n, 1, 1, 1)));
    }
}

TEST_CASE("Wave folder rounded corner and wrap neighbors retain expected slopes and continuity", "[wave-folder]")
{
    for (float drive : {-24.f, 0.f, 36.f})
        for (float threshold : {.05f, 1.f})
            for (float trim : {-60.f, 12.f})
            {
                const auto a = amplitude(drive), g = amplitude(trim);
                WaveFolder dsp; REQUIRE(dsp.setParameters({drive, threshold, trim, false}));
                REQUIRE(dsp.prepare({48000, 127, 1}));
                std::vector<float> inputs;
                const auto maxMultiple = static_cast<int>(std::floor(4 * a / threshold));
                for (int multiple = 1; multiple <= maxMultiple; ++multiple)
                    for (float sign : {-1.f, 1.f})
                    {
                        // The analytical corner may not be representable: evaluate the rounded input and both neighbors.
                        const auto rounded = static_cast<float>(sign * static_cast<double>(multiple) * threshold / a);
                        inputs.push_back(std::nextafter(rounded, -std::numeric_limits<float>::infinity()));
                        inputs.push_back(rounded);
                        inputs.push_back(std::nextafter(rounded, std::numeric_limits<float>::infinity()));
                    }
                juce::AudioBuffer<float> audio(1, static_cast<int>(inputs.size()));
                for (int n = 0; n < audio.getNumSamples(); ++n) audio.setSample(0, n, inputs[static_cast<std::size_t>(n)]);
                REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
                for (std::size_t n = 0; n < inputs.size(); ++n)
                {
                    CAPTURE(drive, threshold, trim, inputs[n]);
                    near(audio.getSample(0, static_cast<int>(n)), reference(inputs[n], a, threshold, g), threshold, g);
                    if (n % 3 == 0) continue;
                    const auto dx = static_cast<long double>(inputs[n]) - inputs[n - 1];
                    const auto dy = static_cast<long double>(audio.getSample(0, static_cast<int>(n))) - audio.getSample(0, static_cast<int>(n - 1));
                    REQUIRE(std::abs(dy) <= g * a * dx + 2 * tolerance(threshold, g));
                    const auto loCell = static_cast<long long>(std::floor((std::abs(a * static_cast<long double>(inputs[n - 1])) / threshold + 1) / 2));
                    const auto hiCell = static_cast<long long>(std::floor((std::abs(a * static_cast<long double>(inputs[n])) / threshold + 1) / 2));
                    if (loCell == hiCell)
                    {
                        const auto slope = loCell % 2 == 0 ? g * a : -g * a;
                        REQUIRE(std::abs(dy - slope * dx) <= 2 * tolerance(threshold, g));
                    }
                }
            }
}

TEST_CASE("Wave folder simultaneous interrupted repeated and bypassed ramps follow frame interpolation", "[wave-folder]")
{
    for (double rate : {8000., 44100., 48000., 88200., 96000., 192000., 384000.})
    {
        CAPTURE(rate);
        const int frames = static_cast<int>(std::floor(rate * .010));
        WaveFolder dsp; REQUIRE(dsp.prepare({rate, 32, 2}));
        IndependentRamp a {1, 1, 1}, t {1, 1, 1}, g {1, 1, 1}, b {0, 0, 0};
        const WaveFolderSettings events[] = {{36, .05f, -60, true}, {-24, .5f, 12, true},
            {12, .25f, 0, false}, {0, 1, -6, false}};
        for (int event = 0; event < 4; ++event)
        {
            const auto p = events[event]; REQUIRE(dsp.setParameters(p));
            a.retarget(amplitude(p.driveDb)); t.retarget(p.threshold); g.retarget(amplitude(p.outputDb)); b.retarget(p.bypass ? 1 : 0);
            const int count = event == 0 ? frames / 3 : frames + 3;
            juce::AudioBuffer<float> audio(2, count); fill(audio, .125f);
            auto block = juce::dsp::AudioBlock<float>(audio);
            REQUIRE(dsp.process(block.getSubBlock(0, 0)));
            for (int n = 0; n < count; ++n)
            {
                REQUIRE(dsp.setParameters(p));
                REQUIRE(dsp.process(block.getSubBlock(static_cast<std::size_t>(n), 1)));
                const double aa = a.next(frames), tt = t.next(frames), gg = g.next(frames), bb = b.next(frames);
                near(audio.getSample(0, n), reference(.125, aa, tt, gg, bb), tt, gg);
                REQUIRE(audio.getSample(1, n) == -audio.getSample(0, n));
                if (bb == 1) REQUIRE(audio.getSample(0, n) == .125f);
            }
        }
    }
}

TEST_CASE("Wave folder scalar ramps each have independent timing", "[wave-folder]")
{
    const WaveFolderSettings settings[] = {{36, 1, 0, false}, {0, .05f, 0, false},
        {0, 1, 12, false}, {0, 1, 0, true}};
    for (const auto p : settings)
    {
        WaveFolder dsp; REQUIRE(dsp.prepare({48000, 32, 1})); REQUIRE(dsp.setParameters(p));
        juce::AudioBuffer<float> audio(1, 481); fill(audio, 1.5f);
        REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
        for (int n = 0; n < 481; ++n)
        {
            const auto f = static_cast<double>(std::min(n + 1, 480)) / 480;
            const auto a = 1 + (amplitude(p.driveDb) - 1) * f;
            const auto t = 1 + (static_cast<double>(p.threshold) - 1) * f;
            const auto g = 1 + (amplitude(p.outputDb) - 1) * f;
            near(audio.getSample(0, n), reference(1.5, a, t, g, p.bypass ? f : 0), t, g);
        }
    }
}

TEST_CASE("Wave folder whole invalid updates and failed preparation preserve samples and ramp progress", "[wave-folder]")
{
    WaveFolder dsp, control, unprepared;
    juce::AudioBuffer<float> audio(1, 1), other(1, 1); fill(audio, .25f);
    REQUIRE_FALSE(unprepared.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(audio.getSample(0, 0) == .25f);
    for (auto* module : {&dsp, &control})
    {
        REQUIRE(module->setParameters({12, .5f, -6, false})); REQUIRE(module->prepare({48000, 32, 1}));
        REQUIRE(module->setParameters({36, .05f, 12, true}));
    }
    // Reject after the ramps have advanced, so a reset or altered target is observable.
    for (int n = 0; n < 57; ++n)
    {
        fill(audio, .25f); fill(other, .25f);
        REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(control.process(juce::dsp::AudioBlock<float>(other)));
    }
    for (float invalid : {-25.f, 37.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        REQUIRE_FALSE(dsp.setParameters({invalid, 1, -60, false}));
    for (float invalid : {.049f, 1.001f, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        REQUIRE_FALSE(dsp.setParameters({-24, invalid, -60, false}));
    for (float invalid : {-61.f, 13.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        REQUIRE_FALSE(dsp.setParameters({-24, 1, invalid, false}));
    const juce::dsp::ProcessSpec invalidSpecs[] = {{7999, 32, 1}, {384001, 32, 1}, {0, 32, 1},
        {std::numeric_limits<double>::infinity(), 32, 1}, {std::numeric_limits<double>::quiet_NaN(), 32, 1},
        {48000, 0, 1}, {48000, 65537, 1}, {48000, 32, 0}, {48000, 32, 3}};
    for (const auto spec : invalidSpecs) REQUIRE_FALSE(dsp.prepare(spec));
    juce::AudioBuffer<float> wrong(2, 512); fill(wrong, .25f); const auto untouched = wrong;
    REQUIRE_FALSE(dsp.process(juce::dsp::AudioBlock<float>(wrong)));
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 512; ++n) REQUIRE(wrong.getSample(c, n) == untouched.getSample(c, n));
    for (int n = 0; n < 513; ++n)
    {
        fill(audio, .25f); fill(other, .25f);
        REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(control.process(juce::dsp::AudioBlock<float>(other)));
        REQUIRE(audio.getSample(0, 0) == other.getSample(0, 0));
    }
}

TEST_CASE("Wave folder reset reprepare and independent instances are deterministic", "[wave-folder]")
{
    WaveFolder dsp, untouched, other;
    for (auto* module : {&dsp, &untouched, &other}) REQUIRE(module->prepare({48000, 32, 1}));
    REQUIRE(dsp.setParameters({36, .05f, 12, true}));
    juce::AudioBuffer<float> audio(1, 1), baseline(1, 1);
    for (int n = 0; n < 512; ++n)
    {
        fill(audio, .25f); REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
        other.reset(); REQUIRE(other.setParameters({12, .25f, -6, n % 2 == 0}));
        fill(audio, .25f); fill(baseline, .25f);
        REQUIRE(untouched.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(other.process(juce::dsp::AudioBlock<float>(baseline)));
        REQUIRE(audio.getSample(0, 0) == .25f);
    }
    for (bool bypass : {false, true})
    {
        REQUIRE(dsp.setParameters({12, .25f, -6, bypass})); dsp.reset();
        fill(audio, .25f); REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
        const auto settled = audio.getSample(0, 0);
        near(settled, reference(.25, amplitude(12), .25, amplitude(-6), bypass ? 1 : 0), .25, amplitude(-6));
        REQUIRE(dsp.setParameters({-24, 1, 12, !bypass})); REQUIRE(dsp.setParameters({12, .25f, -6, bypass})); dsp.reset();
        fill(audio, .25f); REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(audio.getSample(0, 0) == settled);
        REQUIRE(dsp.prepare({96000, 65536, 1})); fill(audio, .25f);
        REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(audio.getSample(0, 0) == settled);
    }
}

TEST_CASE("Wave folder F1 rate layout capacity matrix and event partitions are sample identical", "[wave-folder]")
{
    for (double rate : {8000., 44100., 48000., 88200., 96000., 192000., 384000.})
        for (int channels : {1, 2})
            for (auto capacity : {1u, 32u, 64u, 127u, 256u, 1024u, 65536u})
            {
                CAPTURE(rate, channels, capacity);
                WaveFolder whole, split;
                REQUIRE(whole.prepare({rate, capacity, static_cast<juce::uint32>(channels)}));
                REQUIRE(split.prepare({rate, capacity, static_cast<juce::uint32>(channels)}));
                juce::AudioBuffer<float> a(channels, 4100), b(channels, 4100);
                for (const auto p : {WaveFolderSettings{36, .05f, 12, false}, {-24, 1, -60, true}, {12, .25f, -6, false}})
                {
                    for (int c = 0; c < channels; ++c)
                        for (int n = 0; n < 4100; ++n)
                        {
                            const float x = static_cast<float>((n * 37 + c * 19) % 257 - 128) / 32.f;
                            a.setSample(c, n, x); b.setSample(c, n, x);
                        }
                    REQUIRE(whole.setParameters(p)); REQUIRE(split.setParameters(p));
                    auto va = juce::dsp::AudioBlock<float>(a).getSubBlock(1, 4098), vb = juce::dsp::AudioBlock<float>(b).getSubBlock(1, 4098);
                    REQUIRE(whole.process(va));
                    std::size_t offset = 0;
                    for (auto count : {0u, 1u, 32u, 64u, 127u, 256u, 1024u, 2594u})
                    { REQUIRE(split.process(vb.getSubBlock(offset, count))); offset += count; }
                    REQUIRE(offset == 4098);
                    for (int c = 0; c < channels; ++c)
                    {
                        REQUIRE(a.getSample(c, 0) == static_cast<float>((c * 19) % 257 - 128) / 32.f);
                        REQUIRE(a.getSample(c, 4099) == static_cast<float>((4099 * 37 + c * 19) % 257 - 128) / 32.f);
                        for (int n = 0; n < 4100; ++n) REQUIRE(a.getSample(c, n) == b.getSample(c, n));
                    }
                }
            }
}

TEST_CASE("Wave folder finite extremes are safety cases and settled bypass preserves every bit", "[wave-folder]")
{
    const float values[] = {0.f, -0.f, std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
        std::numeric_limits<float>::lowest(), std::numeric_limits<float>::min(), -std::numeric_limits<float>::min(),
        std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min(), 1e20f, -1e20f, .1234567f, -4.f};
    WaveFolder dsp; REQUIRE(dsp.prepare({48000, 32, 2}));
    juce::AudioBuffer<float> audio(2, 1027);
    for (float drive : {-24.f, 36.f})
        for (float threshold : {.05f, 1.f})
            for (float trim : {-60.f, 12.f})
                for (bool bypass : {false, true})
                {
                    REQUIRE(dsp.setParameters({drive, threshold, trim, bypass})); dsp.reset();
                    for (int c = 0; c < 2; ++c)
                        for (int n = 0; n < 1027; ++n) audio.setSample(c, n, values[(n + c) % 13]);
                    const auto original = audio; REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
                    for (int c = 0; c < 2; ++c)
                        for (int n = 0; n < 1027; ++n)
                        {
                            const auto y = audio.getSample(c, n); REQUIRE(std::isfinite(y));
                            if (bypass)
                            {
                                REQUIRE(y == original.getSample(c, n));
                                REQUIRE(std::signbit(y) == std::signbit(original.getSample(c, n)));
                            }
                            else REQUIRE(std::abs(y) <= amplitude(trim) * threshold + tolerance(threshold, amplitude(trim)));
                        }
                }
    for (bool bypass : {false, true, false})
    {
        REQUIRE(dsp.setParameters({36, .05f, 12, bypass}));
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 1027; ++n) audio.setSample(c, n, c == 0 ? std::numeric_limits<float>::max() : -std::numeric_limits<float>::max());
        REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 1027; ++n) REQUIRE(std::isfinite(audio.getSample(c, n)));
    }
}

TEST_CASE("Wave folder silence remains exact throughout all automation", "[wave-folder]")
{
    WaveFolder dsp; REQUIRE(dsp.prepare({48000, 32, 2}));
    juce::AudioBuffer<float> audio(2, 1027);
    for (const auto p : {WaveFolderSettings{36, .05f, 12, true}, {-24, 1, -60, true}, {12, .25f, 0, false}})
    {
        REQUIRE(dsp.setParameters(p)); audio.clear(); REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 1027; ++n) REQUIRE(audio.getSample(c, n) == 0.f);
    }
    REQUIRE(isValidWaveFolderSettings({-24, .05f, -60, false}));
    REQUIRE(isValidWaveFolderSettings({36, 1, 12, true}));
    REQUIRE_FALSE(isValidWaveFolderSettings({0, .049f, 0, false}));
}
