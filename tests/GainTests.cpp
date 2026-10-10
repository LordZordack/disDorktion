#include "dsp/Gain.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace disdorktion;
static_assert(Module<Gain, GainSettings>);

namespace
{
double amplitude(float db) { return std::pow(10.0, static_cast<double>(db) / 20.0); }
void fill(juce::AudioBuffer<float>& audio, float value = 1.0f)
{
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int n = 0; n < audio.getNumSamples(); ++n) audio.setSample(c, n, value);
}
void close(double actual, double expected)
{
    REQUIRE(std::abs(actual - expected) <= 2e-5 * std::max(1.0, std::abs(expected)));
}
void ramp(Gain& gain, int frames, double startGain, double targetGain,
          double startBypass, double targetBypass, int count = -1)
{
    if (count < 0) count = frames + 3;
    juce::AudioBuffer<float> audio(2, count);
    for (int n = 0; n < count; ++n)
    {
        audio.setSample(0, n, 0.25f);
        audio.setSample(1, n, -0.75f);
    }
    REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
    for (int n = 0; n < count; ++n)
    {
        const auto t = std::min(1.0, static_cast<double>(n + 1) / frames);
        const auto g = startGain + (targetGain - startGain) * t;
        const auto b = startBypass + (targetBypass - startBypass) * t;
        const auto effective = b + (1.0 - b) * g;
        close(audio.getSample(0, n), 0.25 * effective);
        close(audio.getSample(1, n), -0.75 * effective);
        if (n >= frames - 1 && targetBypass == 1.0)
        {
            REQUIRE(audio.getSample(0, n) == 0.25f);
            REQUIRE(audio.getSample(1, n) == -0.75f);
        }
        if (n >= frames - 1 && targetBypass == 0.0 && targetGain == 0.0)
        {
            REQUIRE(audio.getSample(0, n) == 0.0f);
            REQUIRE(audio.getSample(1, n) == 0.0f);
        }
    }
}
}

TEST_CASE("Production Gain defaults and steady dB conversion cover the processing matrix", "[production-gain]")
{
    for (const auto rate : {8000.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0, 384000.0})
        for (const auto channels : {1u, 2u})
            for (const auto capacity : {1u, 32u, 64u, 127u, 256u, 1024u})
            {
                CAPTURE(rate, channels, capacity);
                Gain gain;
                juce::AudioBuffer<float> audio(static_cast<int>(channels), 1027);
                fill(audio, -0.25f);
                REQUIRE_FALSE(gain.process(juce::dsp::AudioBlock<float>(audio)));
                REQUIRE(audio.getSample(0, 0) == -0.25f);
                REQUIRE(gain.prepare({rate, capacity, channels}));
                REQUIRE(gain.getLatencySamples() == 0);
                REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
                for (int c = 0; c < audio.getNumChannels(); ++c)
                    for (int n = 0; n < audio.getNumSamples(); ++n) REQUIRE(audio.getSample(c, n) == -0.25f);
                for (const auto db : {-60.0f, -36.0f, -6.0f, 0.0f, 6.0f, 12.0f, 24.0f})
                {
                    REQUIRE(gain.setParameters({db, false, false}));
                    gain.reset();
                    fill(audio);
                    REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
                    const auto expected = amplitude(db);
                    REQUIRE(std::abs(audio.getSample(0, 0) - expected) / expected <= 2e-6);
                    for (int c = 0; c < audio.getNumChannels(); ++c)
                        for (int n = 0; n < audio.getNumSamples(); ++n) close(audio.getSample(c, n), expected);
                    audio.clear();
                    REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
                    REQUIRE(audio.getMagnitude(0, audio.getNumSamples()) == 0.0f);
                }
            }
}

TEST_CASE("Production Gain validates complete settings and preserves active history on rejection", "[production-gain]")
{
    Gain gain, reference;
    REQUIRE(gain.setParameters({-6.0f, false, false}));
    REQUIRE(reference.setParameters({-6.0f, false, false}));
    REQUIRE(gain.prepare({48000.0, 32, 2}));
    REQUIRE(reference.prepare({48000.0, 32, 2}));
    REQUIRE(gain.setParameters({24.0f, false, false}));
    REQUIRE(reference.setParameters({24.0f, false, false}));
    juce::AudioBuffer<float> a(2, 57), b(2, 57), wrong(1, 57);
    fill(a); fill(b); fill(wrong);
    REQUIRE(gain.process(juce::dsp::AudioBlock<float>(a)));
    REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
    for (const auto db : {-60.01f, 24.01f, std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        REQUIRE_FALSE(gain.setParameters({db, true, true}));
    for (const auto spec : {juce::dsp::ProcessSpec{7999.0, 32, 2}, {384001.0, 32, 2},
                           {48000.0, 0, 2}, {48000.0, 65537, 2}, {48000.0, 32, 0},
                           {48000.0, 32, 3}, {std::numeric_limits<double>::quiet_NaN(), 32, 2},
                           {std::numeric_limits<double>::infinity(), 32, 2}})
        REQUIRE_FALSE(gain.prepare(spec));
    REQUIRE(gain.process(juce::dsp::AudioBlock<float>(a).getSubBlock(0, 0)));
    REQUIRE_FALSE(gain.process(juce::dsp::AudioBlock<float>(wrong)));
    REQUIRE(wrong.getSample(0, 0) == 1.0f);
    fill(a); fill(b);
    REQUIRE(gain.process(juce::dsp::AudioBlock<float>(a)));
    REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 57; ++n) REQUIRE(a.getSample(c, n) == b.getSample(c, n));
    gain.reset();
    REQUIRE(gain.prepare({96000.0, 127, 2}));
    fill(a); REQUIRE(gain.process(juce::dsp::AudioBlock<float>(a)));
    close(a.getSample(0, 0), amplitude(24.0f));
}

TEST_CASE("Production Gain exact mute bypass and unity include every boolean combination", "[production-gain]")
{
    Gain gain;
    REQUIRE(gain.prepare({48000.0, 32, 2}));
    juce::AudioBuffer<float> audio(2, 513);
    for (const auto mute : {false, true})
        for (const auto bypass : {false, true})
            for (const auto db : {-60.0f, 0.0f, 24.0f})
            {
                REQUIRE(gain.setParameters({db, mute, bypass})); gain.reset();
                fill(audio, -0.375f);
                REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < 513; ++n)
                    {
                        if (bypass || (!mute && db == 0.0f)) REQUIRE(audio.getSample(c, n) == -0.375f);
                        else if (mute) REQUIRE(audio.getSample(c, n) == 0.0f);
                        else close(audio.getSample(c, n), -0.375 * amplitude(db));
                    }
            }
    REQUIRE(gain.setParameters({24.0f, false, false})); gain.reset();
    fill(audio, std::numeric_limits<float>::max() / 16.0f);
    REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 513; ++n) REQUIRE(std::isfinite(audio.getSample(c, n)));
}

TEST_CASE("Production Gain ramps follow independent frame interpolation including boundary rates", "[production-gain]")
{
    for (const auto rate : {8000.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0, 384000.0})
        for (const auto db : {-60.0f, -6.0f, 24.0f})
            for (const auto bypass : {false, true})
            {
                Gain gain;
                REQUIRE(gain.prepare({rate, 32, 2}));
                REQUIRE(gain.setParameters({db, false, bypass}));
                const auto frames = static_cast<int>(std::floor(rate * 0.010));
                ramp(gain, frames, 1.0, amplitude(db), 0.0, bypass ? 1.0 : 0.0);
                REQUIRE(gain.setParameters({db, true, false}));
                ramp(gain, frames, amplitude(db), 0.0, bypass ? 1.0 : 0.0, 0.0);
                REQUIRE(gain.setParameters({db, false, false}));
                ramp(gain, frames, 0.0, amplitude(db), 0.0, 0.0);
            }
}

TEST_CASE("Production Gain simultaneous interruption and retained muted or bypassed targets are continuous", "[production-gain]")
{
    Gain gain;
    REQUIRE(gain.prepare({48000.0, 32, 2}));
    REQUIRE(gain.setParameters({24.0f, false, true}));
    ramp(gain, 480, 1.0, amplitude(24.0f), 0.0, 1.0, 240);
    REQUIRE(gain.setParameters({-60.0f, false, false}));
    ramp(gain, 480, (1.0 + amplitude(24.0f)) / 2.0, amplitude(-60.0f), 0.5, 0.0);
    REQUIRE(gain.setParameters({-60.0f, true, false})); gain.reset();
    REQUIRE(gain.setParameters({12.0f, true, false}));
    ramp(gain, 480, 0.0, 0.0, 0.0, 0.0);
    REQUIRE(gain.setParameters({12.0f, false, false}));
    ramp(gain, 480, 0.0, amplitude(12.0f), 0.0, 0.0);
    REQUIRE(gain.setParameters({12.0f, true, true})); gain.reset();
    REQUIRE(gain.setParameters({-6.0f, true, true}));
    ramp(gain, 480, 0.0, 0.0, 1.0, 1.0);
    REQUIRE(gain.setParameters({-6.0f, true, false}));
    ramp(gain, 480, 0.0, 0.0, 1.0, 0.0);
    REQUIRE(gain.setParameters({-6.0f, false, true}));
    ramp(gain, 480, 0.0, amplitude(-6.0f), 0.0, 1.0);
    REQUIRE(gain.setParameters({24.0f, false, true}));
    ramp(gain, 480, amplitude(-6.0f), amplitude(24.0f), 1.0, 1.0);
    REQUIRE(gain.setParameters({24.0f, false, false}));
    ramp(gain, 480, amplitude(24.0f), amplitude(24.0f), 1.0, 0.0);
}

TEST_CASE("Production Gain partitions repeated controls and empty blocks preserve exact samples", "[production-gain]")
{
    for (const auto rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
        for (const auto channels : {1u, 2u})
            for (const auto capacity : {1u, 32u, 64u, 127u, 256u, 1024u})
            {
                Gain whole, split;
                REQUIRE(whole.prepare({rate, capacity, channels}));
                REQUIRE(split.prepare({rate, capacity, channels}));
                juce::AudioBuffer<float> a(static_cast<int>(channels), 4098), b(static_cast<int>(channels), 4098);
                for (const auto settings : {GainSettings{24.0f, false, false}, {-60.0f, true, true}, {-6.0f, false, false}})
                {
                    for (int c = 0; c < a.getNumChannels(); ++c)
                        for (int n = 0; n < 4098; ++n)
                        {
                            const auto value = static_cast<float>((n * 37 + c * 13) % 257 - 128) / 128.0f;
                            a.setSample(c, n, value); b.setSample(c, n, value);
                        }
                    REQUIRE(whole.setParameters(settings)); REQUIRE(split.setParameters(settings));
                    REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a).getSubBlock(1, 4096)));
                    std::size_t offset = 1;
                    for (const auto size : {0u, 1u, 32u, 64u, 127u, 256u, 1024u, 2592u})
                    {
                        REQUIRE(split.setParameters(settings));
                        REQUIRE(split.process(juce::dsp::AudioBlock<float>(b).getSubBlock(offset, size)));
                        offset += size;
                    }
                    REQUIRE(offset == 4097);
                    for (int c = 0; c < a.getNumChannels(); ++c)
                        for (int n = 0; n < 4098; ++n) REQUIRE(a.getSample(c, n) == b.getSample(c, n));
                }
            }
}

TEST_CASE("Production Gain instances and duplicated mono channels own independent histories", "[production-gain]")
{
    Gain mono, stereo, input, output;
    REQUIRE(mono.prepare({48000.0, 32, 1}));
    REQUIRE(stereo.prepare({48000.0, 32, 2}));
    REQUIRE(input.prepare({48000.0, 32, 2}));
    REQUIRE(output.prepare({48000.0, 32, 2}));
    REQUIRE(mono.setParameters({24.0f, false, true}));
    REQUIRE(stereo.setParameters({24.0f, false, true}));
    juce::AudioBuffer<float> m(1, 1024), s(2, 1024); fill(m, 0.25f); fill(s, 0.25f);
    REQUIRE(mono.process(juce::dsp::AudioBlock<float>(m)));
    REQUIRE(stereo.process(juce::dsp::AudioBlock<float>(s)));
    for (int n = 0; n < 1024; ++n)
    {
        REQUIRE(m.getSample(0, n) == s.getSample(0, n));
        REQUIRE(s.getSample(0, n) == s.getSample(1, n));
    }
    REQUIRE(input.setParameters({24.0f, false, false}));
    REQUIRE(output.setParameters({-60.0f, false, false}));
    ramp(input, 480, 1.0, amplitude(24.0f), 0.0, 0.0, 240);
    ramp(output, 480, 1.0, amplitude(-60.0f), 0.0, 0.0, 240);
    input.reset();
    ramp(output, 240, (1.0 + amplitude(-60.0f)) / 2.0, amplitude(-60.0f), 0.0, 0.0, 240);
    fill(s); REQUIRE(input.process(juce::dsp::AudioBlock<float>(s)));
    close(s.getSample(0, 0), amplitude(24.0f));
}
