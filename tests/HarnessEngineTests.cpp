#include "harness/AuditionEngine.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <limits>

using namespace disdorktion;
using namespace disdorktion::harness;
namespace
{
void fillHarness(juce::AudioBuffer<float>& audio, float value)
{
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int n = 0; n < audio.getNumSamples(); ++n) audio.setSample(c, n, value);
}
}

TEST_CASE("Harness pre-monitor samples match the direct module across partitions", "[harness][engine]")
{
    for (auto channels : {1u, 2u})
        for (auto capacity : {1u, 127u})
        {
            AuditionEngine whole, split;
            ReferenceGain direct;
            const juce::dsp::ProcessSpec spec {48000.0, capacity, channels};
            REQUIRE(whole.prepare(spec)); REQUIRE(split.prepare(spec)); REQUIRE(direct.prepare(spec));
            juce::AudioBuffer<float> a(static_cast<int>(channels), 1200), b(static_cast<int>(channels), 1200),
                                     oracle(static_cast<int>(channels), 1200);
            for (auto parameters : {GainParameters {4.0f, false}, GainParameters {0.0f, true}, GainParameters {0.5f, false}})
            {
                REQUIRE(whole.setGain(parameters.gain)); whole.setBypass(parameters.bypass);
                REQUIRE(split.setGain(parameters.gain)); split.setBypass(parameters.bypass);
                REQUIRE(direct.setParameters(parameters));
                for (int c = 0; c < static_cast<int>(channels); ++c)
                    for (int n = 0; n < 1200; ++n)
                    {
                        const auto value = static_cast<float>((37*n + 13*c) % 257 - 128) / 256.0f;
                        a.setSample(c,n,value); b.setSample(c,n,value); oracle.setSample(c,n,value);
                    }
                REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a), false));
                REQUIRE(direct.process(juce::dsp::AudioBlock<float>(oracle)));
                auto block = juce::dsp::AudioBlock<float>(b);
                std::size_t offset = 0;
                for (auto size : {0u, 1u, 32u, 127u, 1024u, 16u})
                {
                    REQUIRE(split.process(block.getSubBlock(offset, size), false));
                    offset += size;
                }
                REQUIRE(offset == 1200);
                for (int c = 0; c < static_cast<int>(channels); ++c)
                    for (int n = 0; n < 1200; ++n)
                    {
                        REQUIRE(a.getSample(c,n) == oracle.getSample(c,n));
                        REQUIRE(b.getSample(c,n) == oracle.getSample(c,n));
                    }
            }
        }
}

TEST_CASE("Harness measurements and renders are independent of monitor settings", "[harness][engine]")
{
    AuditionEngine muted, audible, offlineA, offlineB;
    for (auto* engine : {&muted, &audible, &offlineA, &offlineB})
    {
        REQUIRE(engine->setParameters({2.0f, false}));
        REQUIRE(engine->prepare({48000.0, 8, 2}));
    }
    REQUIRE(audible.setMonitoring(-6.0f, false)); audible.reset();
    REQUIRE(offlineB.setMonitoring(-60.0f, false)); offlineB.reset();
    juce::AudioBuffer<float> a(2, 1000), b(2, 1000), c(2,1000), d(2,1000);
    for (auto* audio : {&a,&b,&c,&d}) fillHarness(*audio, -0.25f);
    REQUIRE(muted.process(juce::dsp::AudioBlock<float>(a)));
    REQUIRE(audible.process(juce::dsp::AudioBlock<float>(b)));
    REQUIRE(offlineA.process(juce::dsp::AudioBlock<float>(c), false));
    REQUIRE(offlineB.process(juce::dsp::AudioBlock<float>(d), false));
    REQUIRE(a.getMagnitude(0,1000) == 0.0f);
    REQUIRE(b.getSample(0,0) == Catch::Approx(-0.5f * std::pow(10.0f,-6.0f/20.0f)));
    for (int n = 0; n < 1000; ++n) REQUIRE(c.getSample(0,n) == d.getSample(0,n));
    for (auto* engine : {&muted,&audible,&offlineA,&offlineB})
    {
        const auto meter = engine->meters();
        REQUIRE(meter.inputPeak == 0.25f); REQUIRE(meter.inputRms == 0.25f);
        REQUIRE(meter.outputPeak == 0.5f); REQUIRE(meter.outputRms == 0.5f);
    }
}

TEST_CASE("Harness meters aggregate channels and overload remains latched", "[harness][engine]")
{
    AuditionEngine engine;
    REQUIRE(engine.prepare({48000.0, 1, 2}));
    juce::AudioBuffer<float> audio(2, 1);
    audio.setSample(0,0,1.25f); audio.setSample(1,0,0.0f);
    REQUIRE(engine.process(juce::dsp::AudioBlock<float>(audio)));
    REQUIRE(engine.meters().inputRms == Catch::Approx(1.25f/std::sqrt(2.0f)));
    REQUIRE(engine.meters().overloaded);
    for (int i = 0; i < 20; ++i) { audio.clear(); REQUIRE(engine.process(juce::dsp::AudioBlock<float>(audio))); }
    REQUIRE(engine.meters().outputPeak == 0.0f);
    REQUIRE(engine.meters().overloaded);
    REQUIRE(engine.consumeOverload()); REQUIRE_FALSE(engine.consumeOverload());
    audio.setSample(0,0,2.0f); REQUIRE(engine.process(juce::dsp::AudioBlock<float>(audio)));
    engine.reset(); REQUIRE_FALSE(engine.meters().overloaded);
}

TEST_CASE("Harness rejects malformed controls and preparation and silences failure", "[harness][engine]")
{
    AuditionEngine engine;
    REQUIRE(engine.setParameters({2.0f,false}));
    for (auto bad : {-1.0f,4.1f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
    { REQUIRE_FALSE(engine.setGain(bad)); REQUIRE_FALSE(engine.setParameters({bad,true})); }
    REQUIRE_FALSE(engine.setMonitoring(-61.0f,false)); REQUIRE_FALSE(engine.setMonitoring(1.0f,false));
    REQUIRE_FALSE(engine.setMonitoring(std::numeric_limits<float>::quiet_NaN(),false));
    REQUIRE_FALSE(engine.selectTarget("missing")); REQUIRE(engine.selectTarget("reference-gain"));
    REQUIRE(auditionTargets().size() == 2);
    REQUIRE(engine.prepare({8000.0,1,1}));
    juce::AudioBuffer<float> mono(1,8), stereo(2,8);
    fillHarness(mono,0.25f); REQUIRE(engine.process(juce::dsp::AudioBlock<float>(mono),false));
    REQUIRE(mono.getSample(0,0) == 0.5f);
    fillHarness(stereo,0.25f); REQUIRE_FALSE(engine.process(juce::dsp::AudioBlock<float>(stereo)));
    REQUIRE(stereo.getMagnitude(0,8) == 0.0f);
    for (const auto spec : {juce::dsp::ProcessSpec{0.0,8,1}, {48000.0,0,1}, {48000.0,8,3}})
    {
        REQUIRE_FALSE(engine.prepare(spec)); fillHarness(mono,0.25f);
        REQUIRE_FALSE(engine.process(juce::dsp::AudioBlock<float>(mono),false));
        REQUIRE(mono.getMagnitude(0,8) == 0.0f);
    }
    REQUIRE(engine.prepare({384000.0,65536,1}));
    mono.setSample(0,0,std::numeric_limits<float>::quiet_NaN());
    REQUIRE_FALSE(engine.process(juce::dsp::AudioBlock<float>(mono)));
    REQUIRE(mono.getMagnitude(0,8) == 0.0f);
}

TEST_CASE("Production target is selectable and preserves module versus monitor mute", "[harness][engine][gain]")
{
    AuditionEngine engine;
    REQUIRE(engine.selectTarget("gain"));
    REQUIRE(engine.selectedTargetId() == "gain");
    REQUIRE(engine.setGainSettings({6.0f, false, false}));
    REQUIRE(engine.prepare({48000.0, 8, 2}));
    juce::AudioBuffer<float> audio(2, 32);
    fillHarness(audio, 0.125f);
    REQUIRE(engine.process(juce::dsp::AudioBlock<float>(audio), false));
    REQUIRE(audio.getSample(0, 0) == Catch::Approx(0.125 * std::pow(10.0, 6.0 / 20.0)));
    engine.setModuleMuted(true); engine.reset(); fillHarness(audio, 0.125f);
    REQUIRE(engine.process(juce::dsp::AudioBlock<float>(audio), false));
    REQUIRE(audio.getMagnitude(0, 32) == 0.0f);
    engine.setBypass(true); engine.reset(); fillHarness(audio, 0.125f);
    REQUIRE(engine.process(juce::dsp::AudioBlock<float>(audio), false));
    REQUIRE(audio.getSample(0, 0) == 0.125f);
    REQUIRE(engine.selectTarget("reference-gain"));
    fillHarness(audio, 0.125f);
    REQUIRE(engine.process(juce::dsp::AudioBlock<float>(audio), false));
    REQUIRE(audio.getSample(0, 0) == 0.125f);
}

TEST_CASE("Production harness samples equal direct gain through interrupted controls", "[harness][engine][gain]")
{
    for (const auto channels : {1u, 2u})
    {
        AuditionEngine engine; Gain direct;
        REQUIRE(engine.selectTarget("gain"));
        REQUIRE(engine.prepare({48000.0, 32, channels})); REQUIRE(direct.prepare({48000.0, 32, channels}));
        juce::AudioBuffer<float> actual(static_cast<int>(channels), 257), expected(static_cast<int>(channels), 257);
        for (const auto parameters : {GainSettings{24.0f, false, false}, GainSettings{-60.0f, true, false},
                                     GainSettings{6.0f, true, true}, GainSettings{-12.0f, false, false}})
        {
            REQUIRE(engine.setGainDb(parameters.gainDb)); engine.setModuleMuted(parameters.mute); engine.setBypass(parameters.bypass);
            REQUIRE(direct.setParameters(parameters));
            for (int c = 0; c < static_cast<int>(channels); ++c)
                for (int n = 0; n < 257; ++n)
                {
                    const auto sample = static_cast<float>((n * 17 + c * 13) % 127 - 63) / 128.0f;
                    actual.setSample(c, n, sample); expected.setSample(c, n, sample);
                }
            REQUIRE(engine.process(juce::dsp::AudioBlock<float>(actual).getSubBlock(0, 0), false));
            REQUIRE(engine.process(juce::dsp::AudioBlock<float>(actual), false));
            REQUIRE(direct.process(juce::dsp::AudioBlock<float>(expected)));
            for (int c = 0; c < static_cast<int>(channels); ++c)
                for (int n = 0; n < 257; ++n) REQUIRE(actual.getSample(c, n) == expected.getSample(c, n));
        }
    }
}

TEST_CASE("Harness reset restores retained endpoints and instances stay independent", "[harness][engine]")
{
    AuditionEngine a,b;
    REQUIRE(a.prepare({48000.0,8,1})); REQUIRE(b.prepare({48000.0,8,1}));
    REQUIRE(a.setGain(4.0f)); a.reset();
    juce::AudioBuffer<float> aa(1,32),bb(1,32); fillHarness(aa,0.125f); fillHarness(bb,0.125f);
    REQUIRE(a.process(juce::dsp::AudioBlock<float>(aa).getSubBlock(0,0),false));
    REQUIRE(a.process(juce::dsp::AudioBlock<float>(aa),false)); REQUIRE(b.process(juce::dsp::AudioBlock<float>(bb),false));
    REQUIRE(aa.getSample(0,0) == 0.5f); REQUIRE(bb.getSample(0,0) == 0.125f);
    a.setBypass(true); a.reset(); fillHarness(aa,0.125f);
    REQUIRE(a.process(juce::dsp::AudioBlock<float>(aa),false)); REQUIRE(aa.getSample(0,0) == 0.125f);
    REQUIRE(a.getLatencySamples() == 0);
}
