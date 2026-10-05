#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/ReferenceGain.h"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace disdorktion;
static_assert(Module<ReferenceGain, GainParameters>);
namespace
{
void fill(juce::AudioBuffer<float>& buffer, float value)
{
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int n = 0; n < buffer.getNumSamples(); ++n) buffer.setSample(c, n, value);
}
}

TEST_CASE("Gain prepares at unity and validates controls atomically", "[gain]")
{
    for (const auto rate : {8000.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0, 384000.0})
        for (const auto channels : {1, 2})
        {
            ReferenceGain gain;
            juce::AudioBuffer<float> audio(channels, 1024); fill(audio, -0.25f);
            REQUIRE_FALSE(gain.process(juce::dsp::AudioBlock<float>(audio)));
            REQUIRE(audio.getSample(0, 0) == -0.25f);
            REQUIRE(gain.prepare({rate, 127, static_cast<juce::uint32>(channels)}));
            REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
            for (int c = 0; c < channels; ++c)
                for (int n = 0; n < 1024; ++n) REQUIRE(audio.getSample(c, n) == -0.25f);
            REQUIRE(gain.getLatencySamples() == 0);
        }
    ReferenceGain gain;
    REQUIRE(gain.setParameters({2.0f, false}));
    for (const auto invalid : {-0.1f, 4.1f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()})
        REQUIRE_FALSE(gain.setParameters({invalid, true}));
    REQUIRE(gain.prepare({48000.0, 32, 1}));
    juce::AudioBuffer<float> audio(1, 1); fill(audio, 0.25f);
    REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
    REQUIRE(audio.getSample(0, 0) == 0.5f);
    REQUIRE_FALSE(gain.prepare({48000.0, 0, 1}));
    fill(audio, 0.25f); REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
    REQUIRE(audio.getSample(0, 0) == 0.5f);
}

TEST_CASE("Gain and bypass ramps follow independent frame math", "[gain]")
{
    for (const auto rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
        for (const auto target : {0.0f, 0.5f, 4.0f})
        {
            CAPTURE(rate, target);
            const auto frames = static_cast<int>(std::floor(rate * 0.010));
            ReferenceGain gain;
            REQUIRE(gain.prepare({rate, 32, 2}));
            REQUIRE(gain.setParameters({target, true}));
            juce::AudioBuffer<float> audio(2, frames + 3); fill(audio, 0.25f);
            auto empty = juce::dsp::AudioBlock<float>(audio).getSubBlock(0, 0);
            REQUIRE(gain.process(empty));
            juce::AudioBuffer<float> wrong(1, 1); fill(wrong, 1.0f);
            REQUIRE_FALSE(gain.process(juce::dsp::AudioBlock<float>(wrong)));
            REQUIRE(wrong.getSample(0, 0) == 1.0f);
            REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
            for (int n = 0; n < frames + 3; ++n)
            {
                const auto t = std::min(1.0, static_cast<double>(n + 1) / frames);
                const auto g = 1.0 + (target - 1.0) * t;
                const auto expected = 0.25 * (t + (1.0 - t) * g);
                REQUIRE(audio.getSample(0, n) == Catch::Approx(expected).margin(0.0001));
                REQUIRE(audio.getSample(1, n) == audio.getSample(0, n));
            }
            REQUIRE(audio.getSample(0, frames - 1) == 0.25f);
            REQUIRE(gain.setParameters({target, false}));
            fill(audio, 0.25f);
            REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
            for (int n = 0; n < frames + 3; ++n)
            {
                const auto b = 1.0 - std::min(1.0, static_cast<double>(n + 1) / frames);
                REQUIRE(audio.getSample(0, n) == Catch::Approx(0.25 * (b + (1.0 - b) * target)).margin(0.0001));
            }
        }
}

TEST_CASE("Gain reset reprepare endpoints and silence are deterministic", "[gain]")
{
    ReferenceGain gain;
    REQUIRE(gain.prepare({48000.0, 1, 2}));
    juce::AudioBuffer<float> audio(2, 1024);
    for (const auto target : {0.0f, 1.0f, 4.0f})
    {
        REQUIRE(gain.setParameters({target, false})); gain.reset();
        fill(audio, -0.25f); REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < 1024; ++n) REQUIRE(audio.getSample(c, n) == -0.25f * target);
        audio.clear(); REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
        REQUIRE(audio.getMagnitude(0, 1024) == 0.0f);
    }
    REQUIRE(gain.setParameters({1.0f, false})); gain.reset();
    fill(audio, 0.125f); REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
    REQUIRE(audio.getSample(0, 0) == 0.125f);
    REQUIRE(gain.setParameters({0.0f, true}));
    REQUIRE(gain.prepare({96000.0, 127, 2}));
    fill(audio, 0.25f); REQUIRE(gain.process(juce::dsp::AudioBlock<float>(audio)));
    REQUIRE(audio.getSample(0, 0) == 0.25f);
}

TEST_CASE("Block partitions oversized dispatch and automation preserve output", "[gain]")
{
    for (const auto rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
        for (const auto channels : {1, 2})
            for (const auto capacity : {1u, 32u, 127u, 1024u})
            {
                ReferenceGain whole, split;
                REQUIRE(whole.prepare({rate, capacity, static_cast<juce::uint32>(channels)}));
                REQUIRE(split.prepare({rate, capacity, static_cast<juce::uint32>(channels)}));
                juce::AudioBuffer<float> a(channels, 4098), b(channels, 4098);
                for (int c = 0; c < channels; ++c)
                    for (int n = 0; n < 4098; ++n)
                    {
                        const auto value = static_cast<float>((n * 37 + c * 13) % 257 - 128) / 128.0f;
                        a.setSample(c, n, value); b.setSample(c, n, value);
                    }
                for (const auto target : {GainParameters{4.0f, false}, {0.0f, true}, {0.5f, false}})
                {
                    REQUIRE(whole.setParameters(target)); REQUIRE(split.setParameters(target));
                    auto va = juce::dsp::AudioBlock<float>(a).getSubBlock(1, 4096);
                    auto vb = juce::dsp::AudioBlock<float>(b).getSubBlock(1, 4096);
                    REQUIRE(whole.process(va));
                    std::size_t offset = 0;
                    for (const auto size : {0u, 1u, 32u, 64u, 127u, 256u, 1024u, 2592u})
                    {
                        REQUIRE(split.process(vb.getSubBlock(offset, size)));
                        offset += size;
                    }
                    REQUIRE(offset == 4096);
                    for (int c = 0; c < channels; ++c)
                        for (int n = 0; n < 4098; ++n) REQUIRE(a.getSample(c, n) == b.getSample(c, n));
                }
            }
}
