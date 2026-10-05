#include <catch2/catch_test_macros.hpp>
#include "dsp/ModuleContract.h"
#include "dsp/BlockDispatcher.h"
#include <array>
#include <limits>
#include <vector>

using namespace disdorktion;

namespace
{
struct DelayParameters {};
class OneSampleDelay
{
public:
    bool prepare(const juce::dsp::ProcessSpec& spec)
    {
        if (!dispatcher.prepare(spec)) return false;
        reset();
        return true;
    }
    void reset() noexcept { history.fill(0.0f); }
    bool setParameters(const DelayParameters&) noexcept { return true; }
    unsigned int getLatencySamples() const noexcept { return 1; }
    bool process(juce::dsp::AudioBlock<float> block) noexcept
    {
        return dispatcher.process(block, [this](auto chunk) noexcept
        {
            for (std::size_t c = 0; c < chunk.getNumChannels(); ++c)
                for (std::size_t n = 0; n < chunk.getNumSamples(); ++n)
                {
                    auto& sample = chunk.getChannelPointer(c)[n];
                    const auto next = sample;
                    sample = history[c];
                    history[c] = next;
                }
        });
    }
private:
    BlockDispatcher dispatcher;
    std::array<float, 2> history {};
};
static_assert(Module<OneSampleDelay, DelayParameters>);
}

TEST_CASE("Preparation validates the documented domain and preserves configuration", "[contract]")
{
    BlockDispatcher dispatcher;
    juce::AudioBuffer<float> buffer(1, 1);
    buffer.setSample(0, 0, 3.0f);
    auto noop = [](auto) noexcept {};
    REQUIRE_FALSE(dispatcher.process(juce::dsp::AudioBlock<float>(buffer), noop));
    for (const auto rate : {8000.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0, 384000.0})
        for (const auto channels : {1u, 2u})
            for (const auto capacity : {1u, 32u, 127u, 1024u, 65536u})
                REQUIRE(dispatcher.prepare({rate, capacity, channels}));
    REQUIRE(dispatcher.prepare({48000.0, 32, 1}));
    for (const auto spec : {juce::dsp::ProcessSpec{0.0, 32, 1}, {7999.0, 32, 1},
        {384001.0, 32, 1}, {std::numeric_limits<double>::infinity(), 32, 1},
        {std::numeric_limits<double>::quiet_NaN(), 32, 1}, {48000.0, 0, 1},
        {48000.0, 65537, 1}, {48000.0, 32, 0}, {48000.0, 32, 3}})
        REQUIRE_FALSE(dispatcher.prepare(spec));
    REQUIRE(dispatcher.process(juce::dsp::AudioBlock<float>(buffer), noop));
    REQUIRE(buffer.getSample(0, 0) == 3.0f);
}

TEST_CASE("Dispatcher covers every frame once with bounded nonowning chunks", "[contract]")
{
    for (const auto channels : {1, 2})
        for (const auto capacity : {1u, 32u, 127u, 1024u})
            for (const auto count : {0, 1, 32, 64, 127, 256, 1024, 2051})
            {
                CAPTURE(channels, capacity, count);
                BlockDispatcher dispatcher;
                REQUIRE(dispatcher.prepare({48000.0, capacity, static_cast<juce::uint32>(channels)}));
                juce::AudioBuffer<float> buffer(channels, count + 2);
                for (int c = 0; c < channels; ++c)
                    for (int n = 0; n < count + 2; ++n) buffer.setSample(c, n, -7.0f);
                auto view = juce::dsp::AudioBlock<float>(buffer).getSubBlock(1, static_cast<std::size_t>(count));
                std::size_t covered = 0, calls = 0;
                REQUIRE(dispatcher.process(view, [&](auto chunk) noexcept
                {
                    // Assertions stay outside the callback.
                    if (chunk.getNumSamples() > capacity) covered = 1000000;
                    covered += chunk.getNumSamples();
                    ++calls;
                    for (std::size_t c = 0; c < chunk.getNumChannels(); ++c)
                        for (std::size_t n = 0; n < chunk.getNumSamples(); ++n)
                            chunk.getChannelPointer(c)[n] += 1.0f;
                }));
                REQUIRE(covered == static_cast<std::size_t>(count));
                REQUIRE(calls == (static_cast<std::size_t>(count) + capacity - 1) / capacity);
                for (int c = 0; c < channels; ++c)
                {
                    REQUIRE(buffer.getSample(c, 0) == -7.0f);
                    REQUIRE(buffer.getSample(c, count + 1) == -7.0f);
                    for (int n = 1; n <= count; ++n) REQUIRE(buffer.getSample(c, n) == -6.0f);
                }
            }
}

TEST_CASE("Stateful fixture isolates channels instances rejection and reset", "[contract]")
{
    OneSampleDelay a, b;
    REQUIRE(a.prepare({48000.0, 1, 2}));
    REQUIRE(b.prepare({48000.0, 32, 2}));
    juce::AudioBuffer<float> impulse(2, 3);
    impulse.clear(); impulse.setSample(0, 0, 1.0f);
    REQUIRE(a.process(juce::dsp::AudioBlock<float>(impulse)));
    REQUIRE(impulse.getSample(0, 1) == 1.0f);
    for (int n = 0; n < 3; ++n) REQUIRE(impulse.getSample(1, n) == 0.0f);
    impulse.clear(); impulse.setSample(0, 2, 2.0f);
    REQUIRE(a.process(juce::dsp::AudioBlock<float>(impulse)));
    juce::AudioBuffer<float> wrong(1, 2); wrong.setSample(0, 0, 5.0f);
    REQUIRE_FALSE(a.process(juce::dsp::AudioBlock<float>(wrong)));
    REQUIRE(wrong.getSample(0, 0) == 5.0f);
    REQUIRE_FALSE(a.prepare({48000.0, 0, 2}));
    auto empty = juce::dsp::AudioBlock<float>(impulse).getSubBlock(0, 0);
    REQUIRE(a.process(empty));
    impulse.clear();
    REQUIRE(b.process(juce::dsp::AudioBlock<float>(impulse)));
    REQUIRE(impulse.getSample(0, 0) == 0.0f);
    REQUIRE(a.process(juce::dsp::AudioBlock<float>(impulse)));
    REQUIRE(impulse.getSample(0, 0) == 2.0f);
    a.reset(); impulse.clear();
    REQUIRE(a.process(juce::dsp::AudioBlock<float>(impulse)));
    REQUIRE(impulse.getSample(0, 0) == 0.0f);
    REQUIRE(a.getLatencySamples() == 1);
}
