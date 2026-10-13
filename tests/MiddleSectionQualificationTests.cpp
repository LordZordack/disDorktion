#include "dsp/MiddleSection.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <complex>

using namespace disdorktion;

TEST_CASE("Middle candidate small signal delay is fixed across every bypass", "[middle][qualification]")
{
    for (double rate : {44100., 48000., 88200., 96000., 192000.})
        for (unsigned capacity : {1u, 32u, 64u, 127u, 256u, 1024u})
            for (bool tanhBypass : {false, true})
                for (bool folderBypass : {false, true})
    {
        CAPTURE(rate, capacity, tanhBypass, folderBypass);
        MiddleSection section;
        REQUIRE(section.setParameters({{0, 0, tanhBypass}, {0, 1, 0, folderBypass}}));
        REQUIRE(section.prepare({rate, capacity, 2}, 4, MiddleLatencyCorrection::fir,
                                MiddleFoldAlgorithm::adaaTriangle));
        REQUIRE(section.getLatencySamples() == 92);
        juce::AudioBuffer<float> impulse(2, 512);
        impulse.clear(); impulse.setSample(0, 0, 1e-4f);
        REQUIRE(section.process(juce::dsp::AudioBlock<float>(impulse)));
        int peak = 0;
        for (int n = 0; n < 512; ++n)
        {
            REQUIRE(std::isfinite(impulse.getSample(0, n)));
            REQUIRE(impulse.getSample(1, n) == 0);
            if (std::abs(impulse.getSample(0, n)) > std::abs(impulse.getSample(0, peak))) peak = n;
        }
        REQUIRE(peak == 92); // FIR pre-ringing is not the reported delay.
        for (double frequency : {.001, .01, .1, .2, .3, .4})
        {
            const auto omega = 2 * juce::MathConstants<double>::pi * frequency;
            std::complex<double> response {};
            for (int n = 0; n < 512; ++n)
                response += static_cast<double>(impulse.getSample(0, n)) / 1e-4 * std::polar(1., -omega*n);
            const auto aligned = response * std::polar(1., omega*92);
            REQUIRE(std::abs(20*std::log10(std::abs(response))) <= .1);
            REQUIRE(std::abs(std::arg(aligned))*180/juce::MathConstants<double>::pi <= 5);
            REQUIRE(std::abs(20*std::log10(std::abs(.5 + .5*aligned))) <= .2);
            if (frequency == .001)
                REQUIRE(std::abs(-std::arg(aligned)/omega) < .01); // Low-frequency delay residual.
        }
    }
}

TEST_CASE("Middle candidate retains exact samples across host partitions and transitions", "[middle][qualification]")
{
    for (double rate : {44100., 48000., 88200., 96000., 192000.})
        for (unsigned channels : {1u, 2u})
            for (unsigned capacity : {1u, 32u, 64u, 127u, 256u, 1024u})
    {
        CAPTURE(rate, channels, capacity);
        MiddleSection whole, partitioned;
        REQUIRE(whole.prepare({rate, capacity, channels}, 4, MiddleLatencyCorrection::fir, MiddleFoldAlgorithm::adaaTriangle));
        REQUIRE(partitioned.prepare({rate, capacity, channels}, 4, MiddleLatencyCorrection::fir, MiddleFoldAlgorithm::adaaTriangle));
        for (int epoch = 0; epoch < 6; ++epoch)
        {
            const MiddleSettings settings {{epoch%2 ? 36.f : -24.f, epoch%2 ? -60.f : 12.f, epoch%3 == 0},
                {epoch%2 ? 36.f : 0.f, epoch%2 ? .05f : 1.f, epoch%2 ? 12.f : -60.f, epoch%3 == 1}};
            REQUIRE(whole.setParameters(settings)); REQUIRE(partitioned.setParameters(settings));
            if (epoch == 4) { whole.reset(); partitioned.reset(); }
            juce::AudioBuffer<float> a(static_cast<int>(channels), 1031), b(static_cast<int>(channels), 1031);
            for (unsigned c = 0; c < channels; ++c) for (int n = 0; n < 1031; ++n)
                a.setSample(static_cast<int>(c), n, .2f * std::sin(static_cast<float>(n + 1031*epoch) * (.13f + .02f*c)));
            b.makeCopyOf(a);
            REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a)));
            auto blocks = juce::dsp::AudioBlock<float>(b);
            REQUIRE(partitioned.process(blocks.getSubBlock(0, 0)));
            for (std::size_t offset = 0; offset < 1031;)
            {
                const auto size = std::min<std::size_t>((offset%5)*31 + 1, 1031-offset);
                REQUIRE(partitioned.process(blocks.getSubBlock(offset, size)));
                offset += size;
            }
            for (unsigned c = 0; c < channels; ++c) for (int n = 0; n < 1031; ++n)
            {
                REQUIRE(std::isfinite(a.getSample(static_cast<int>(c), n)));
                REQUIRE(std::abs(a.getSample(static_cast<int>(c), n) - b.getSample(static_cast<int>(c), n)) <= 1e-6f);
            }
            REQUIRE(whole.getLatencySamples() == 92); REQUIRE(partitioned.getLatencySamples() == 92);
        }
    }
}
