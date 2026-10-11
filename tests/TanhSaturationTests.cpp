#include "dsp/TanhSaturation.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace disdorktion;
static_assert(Module<TanhSaturation, TanhSaturationSettings>);
namespace
{
double amplitude(float db) { return std::pow(10.0, static_cast<double>(db) / 20.0); }
double reference(double x, double a, double g, double b = 0)
{
    if (b == 1) return x;
    const double z = a * x;
    const double e = std::expm1(-2 * std::abs(z));
    const double t = std::abs(z) > 40 ? std::copysign(1.0, z) : std::copysign(-e / (2 + e), z);
    const double wet = g * t;
    return b == 0 ? wet : b * x + (1 - b) * wet;
}
void fill(juce::AudioBuffer<float>& audio, float x)
{
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int n = 0; n < audio.getNumSamples(); ++n) audio.setSample(c, n, c == 0 ? x : -x);
}
void near(float actual, double expected, double g)
{
    REQUIRE(std::isfinite(actual));
    REQUIRE(std::abs(actual - expected) <= 2e-7 * std::max(1.0, g));
}
}

TEST_CASE("Tanh settled curve obeys independent reference symmetry bounds and drive endpoints", "[tanh]")
{
    for (float drive : {-24.f, 0.f, 12.f, 36.f})
        for (float trim : {-60.f, 0.f, 12.f})
        {
            CAPTURE(drive, trim);
            TanhSaturation dsp;
            REQUIRE(dsp.setParameters({drive, trim, false}));
            REQUIRE(dsp.prepare({48000, 32, 2}));
            juce::AudioBuffer<float> audio(2, 1033);
            for (int n = 0; n < 1025; ++n)
            {
                audio.setSample(0, n, -4.f + n / 128.f);
                audio.setSample(1, n, -audio.getSample(0, n));
            }
            const float extremes[] = {0.f, 1e-20f, -1e-20f, std::numeric_limits<float>::denorm_min(),
                -std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::max(),
                -std::numeric_limits<float>::max(), 1.f};
            for (int n = 0; n < 8; ++n)
            { audio.setSample(0, 1025 + n, extremes[n]); audio.setSample(1, 1025 + n, -extremes[n]); }
            auto original = audio;
            REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
            const auto a = amplitude(drive), g = amplitude(trim);
            for (int n = 0; n < audio.getNumSamples(); ++n)
            {
                near(audio.getSample(0, n), reference(original.getSample(0, n), a, g), g);
                REQUIRE(audio.getSample(1, n) == -audio.getSample(0, n));
                REQUIRE(std::abs(audio.getSample(0, n)) <= g + 2e-7 * std::max(1., g));
                if (n > 0 && n < 1025) REQUIRE(audio.getSample(0, n) >= audio.getSample(0, n - 1));
            }
            REQUIRE(audio.getSample(0, 1025) == 0.f);
            REQUIRE(dsp.getLatencySamples() == 0);
        }
    double previous = 0;
    for (float drive : {-24.f, 0.f, 12.f, 36.f})
    {
        TanhSaturation dsp; REQUIRE(dsp.setParameters({drive, 0, false})); REQUIRE(dsp.prepare({48000, 1, 1}));
        juce::AudioBuffer<float> audio(1, 1); audio.setSample(0, 0, .25f);
        REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
        REQUIRE(audio.getSample(0, 0) > previous); previous = audio.getSample(0, 0);
    }
}

TEST_CASE("Tanh simultaneous interrupted and repeated ramps follow independent frame interpolation", "[tanh]")
{
    for (double rate : {8000., 44100., 48000., 88200., 96000., 192000., 384000.})
    {
        CAPTURE(rate);
        const int frames = static_cast<int>(std::floor(rate * .010));
        TanhSaturation dsp; REQUIRE(dsp.prepare({rate, 32, 2}));
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
                current = start + (target - start) * static_cast<double>(elapsed) / frames;
                return current;
            }
        };
        IndependentRamp a {1, 1, 1}, g {1, 1, 1}, b {0, 0, 0};
        const TanhSaturationSettings events[] = {{36, -60, true}, {-24, 12, true}, {12, 0, false}, {0, -6, false}};
        for (int event = 0; event < 4; ++event)
        {
            const auto p = events[event]; REQUIRE(dsp.setParameters(p));
            const double targetA = amplitude(p.driveDb), targetG = amplitude(p.outputDb), targetB = p.bypass ? 1 : 0;
            a.retarget(targetA); g.retarget(targetG); b.retarget(targetB);
            const int count = event == 0 ? frames / 3 : frames + 3;
            juce::AudioBuffer<float> audio(2, count); fill(audio, .125f);
            auto block = juce::dsp::AudioBlock<float>(audio);
            REQUIRE(dsp.process(block.getSubBlock(0, 0)));
            for (int n = 0; n < count; ++n)
            {
                // Repeated settings must not restart any in-flight smoother.
                REQUIRE(dsp.setParameters(p));
                REQUIRE(dsp.process(block.getSubBlock(static_cast<std::size_t>(n), 1)));
                const double currentA = a.next(frames), currentG = g.next(frames), currentB = b.next(frames);
                near(audio.getSample(0, n), reference(.125, currentA, currentG, currentB), currentG);
                REQUIRE(audio.getSample(1, n) == -audio.getSample(0, n));
                if (currentB == 1) REQUIRE(audio.getSample(0, n) == .125f);
            }
        }
    }
}

TEST_CASE("Tanh preparation rejection parameter rejection reset and independent instances preserve state", "[tanh]")
{
    TanhSaturation dsp, control;
    juce::AudioBuffer<float> audio(1, 1); fill(audio, .25f);
    REQUIRE_FALSE(dsp.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(audio.getSample(0, 0) == .25f);
    for (auto* module : {&dsp, &control}) { REQUIRE(module->setParameters({12, -6, false})); REQUIRE(module->prepare({48000, 32, 1})); REQUIRE(module->setParameters({36, 12, true})); }
    for (float invalid : {-25.f, 37.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) REQUIRE_FALSE(dsp.setParameters({invalid, 0, false}));
    for (float invalid : {-61.f, 13.f, -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) REQUIRE_FALSE(dsp.setParameters({0, invalid, false}));
    const juce::dsp::ProcessSpec invalidSpecs[] = {{7999, 32, 1}, {384001, 32, 1}, {0, 32, 1},
        {std::numeric_limits<double>::quiet_NaN(), 32, 1}, {48000, 0, 1}, {48000, 65537, 1}, {48000, 32, 0}, {48000, 32, 3}};
    for (const auto spec : invalidSpecs) REQUIRE_FALSE(dsp.prepare(spec));
    juce::AudioBuffer<float> wrong(2, 512); fill(wrong, .25f); REQUIRE_FALSE(dsp.process(juce::dsp::AudioBlock<float>(wrong))); REQUIRE(wrong.getSample(0, 0) == .25f);
    juce::AudioBuffer<float> other(1, 1);
    for (int n = 0; n < 513; ++n)
    {
        fill(audio, .25f); fill(other, .25f);
        REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(control.process(juce::dsp::AudioBlock<float>(other)));
        REQUIRE(audio.getSample(0, 0) == other.getSample(0, 0));
    }
    for (bool bypass : {false, true})
    {
        REQUIRE(dsp.setParameters({-24, 12, bypass})); dsp.reset();
        fill(audio, .25f); REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
        const auto settled = audio.getSample(0, 0);
        near(settled, reference(.25, amplitude(-24), amplitude(12), bypass ? 1 : 0), amplitude(12));
        REQUIRE(dsp.setParameters({36, -60, !bypass})); REQUIRE(dsp.setParameters({-24, 12, bypass})); dsp.reset();
        fill(audio, .25f); REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(audio.getSample(0, 0) == settled);
        REQUIRE(dsp.prepare({96000, 65536, 1})); fill(audio, .25f); REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(audio.getSample(0, 0) == settled);
        audio.clear(); REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(audio.getSample(0, 0) == 0.f);
    }
}

TEST_CASE("Tanh full rate layout capacity matrix partitions and sentinels are sample identical", "[tanh]")
{
    for (double rate : {8000., 44100., 48000., 88200., 96000., 192000., 384000.})
        for (int channels : {1, 2})
            for (auto capacity : {1u, 32u, 64u, 127u, 256u, 1024u, 65536u})
            {
                CAPTURE(rate, channels, capacity);
                TanhSaturation whole, split;
                REQUIRE(whole.prepare({rate, capacity, static_cast<juce::uint32>(channels)}));
                REQUIRE(split.prepare({rate, capacity, static_cast<juce::uint32>(channels)}));
                juce::AudioBuffer<float> a(channels, 4100), b(channels, 4100);
                for (const auto p : {TanhSaturationSettings{36, 12, false}, {-24, -60, true}, {12, -6, false}})
                {
                    for (int c = 0; c < channels; ++c)
                        for (int n = 0; n < 4100; ++n)
                        { const float x = static_cast<float>((n * 37 + c * 19) % 257 - 128) / 128.f; a.setSample(c, n, x); b.setSample(c, n, x); }
                    REQUIRE(whole.setParameters(p)); REQUIRE(split.setParameters(p));
                    const float first = a.getSample(0, 0), last = a.getSample(0, 4099);
                    auto va = juce::dsp::AudioBlock<float>(a).getSubBlock(1, 4098), vb = juce::dsp::AudioBlock<float>(b).getSubBlock(1, 4098);
                    REQUIRE(whole.process(va));
                    std::size_t offset = 0;
                    for (auto count : {0u, 1u, 32u, 64u, 127u, 256u, 1024u, 2594u}) { REQUIRE(split.process(vb.getSubBlock(offset, count))); offset += count; }
                    REQUIRE(offset == 4098);
                    REQUIRE(a.getSample(0, 0) == first); REQUIRE(a.getSample(0, 4099) == last);
                    for (int c = 0; c < channels; ++c)
                        for (int n = 0; n < 4100; ++n) REQUIRE(a.getSample(c, n) == b.getSample(c, n));
                }
            }
}

TEST_CASE("Tanh silence remains exact through automation and bypass preserves finite samples", "[tanh]")
{
    TanhSaturation dsp; REQUIRE(dsp.prepare({48000, 32, 2}));
    juce::AudioBuffer<float> audio(2, 1027);
    for (const auto p : {TanhSaturationSettings{36, 12, true}, {-24, -60, true}, {0, 0, false}})
    {
        REQUIRE(dsp.setParameters(p)); audio.clear(); REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 1027; ++n) REQUIRE(audio.getSample(c, n) == 0.f);
    }
    REQUIRE(dsp.setParameters({36, 12, true})); dsp.reset();
    const float values[] = {0.f, -0.f, std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
        std::numeric_limits<float>::denorm_min(), -std::numeric_limits<float>::denorm_min(), .1234567f, -4.f};
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 1027; ++n) audio.setSample(c, n, values[(n + c) % 8]);
    auto original = audio; REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 1027; ++n)
        { REQUIRE(audio.getSample(c, n) == original.getSample(c, n)); REQUIRE(std::signbit(audio.getSample(c, n)) == std::signbit(original.getSample(c, n))); }
}
