#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "plugin/PluginProcessor.h"
#include <limits>
#include "AllocationTracking.h"
using disdorktion::PluginProcessor;
TEST_CASE("Issue38 exposes Driver and removes obsolete prototype controls", "[adapter][issue38]")
{
    PluginProcessor plugin;
    auto* driver = plugin.parameterState().getParameter("driver");
    REQUIRE(driver != nullptr);
    REQUIRE(driver->convertFrom0to1(0.0f) == 1.0f);
    REQUIRE(driver->convertFrom0to1(1.0f) == 4.0f);
    REQUIRE(driver->convertFrom0to1(driver->getDefaultValue()) == 1.0f);
    for (const auto* id : {"referenceGain", "referenceBypass", "inputMute", "inputBypass",
         "outputMute", "outputBypass", "chainBypass", "tanhOutputDb", "folderOutputDb",
         "tanhBypass", "folderBypass", "hpBypass", "lpBypass", "combDampingEnabled"})
        REQUIRE(plugin.parameterState().getParameter(id) == nullptr);
    REQUIRE(plugin.parameterState().getParameter("waveshaperMix") != nullptr);
    REQUIRE(plugin.parameterState().getParameter("waveshaperTrimDb") != nullptr);
}

namespace {
juce::AudioProcessor::BusesLayout layout(juce::AudioChannelSet input, juce::AudioChannelSet output)
{
    juce::AudioProcessor::BusesLayout result; result.inputBuses.add(input); result.outputBuses.add(output); return result;
}
void fill(juce::AudioBuffer<float>& audio, float value)
{
    for(int c=0;c<audio.getNumChannels();++c) for(int n=0;n<audio.getNumSamples();++n) audio.setSample(c,n,value);
}
}
TEST_CASE("Adapter admits matching mono stereo and rejects other layouts", "[adapter]")
{
    PluginProcessor p;
    REQUIRE(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::mono(),juce::AudioChannelSet::mono())));
    REQUIRE(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::stereo(),juce::AudioChannelSet::stereo())));
    REQUIRE_FALSE(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::mono(),juce::AudioChannelSet::stereo())));
    REQUIRE_FALSE(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::disabled(),juce::AudioChannelSet::stereo())));
    REQUIRE_FALSE(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::create5point1(),juce::AudioChannelSet::create5point1())));
}
TEST_CASE("Adapter invalid lifecycle buffers silence output and surplus channels", "[adapter]")
{
    PluginProcessor p; juce::MidiBuffer midi; juce::AudioBuffer<float> audio(2,2051);
    fill(audio,0.25f); p.processBlock(audio,midi); REQUIRE(audio.getMagnitude(0,2051)==0);
    p.prepareToPlay(48000,32); REQUIRE(p.getLatencySamples()>0);
    fill(audio,0.25f); p.processBlock(audio,midi); REQUIRE(std::isfinite(audio.getSample(0,2050)));
    juce::AudioBuffer<float> wrong(1,7); fill(wrong,0.25f); p.processBlock(wrong,midi); REQUIRE(wrong.getMagnitude(0,7)==0);
    for(const auto rate : {0.0,7999.0,384001.0,std::numeric_limits<double>::quiet_NaN()})
    {
        p.prepareToPlay(rate,32); fill(audio,0.25f); p.processBlock(audio,midi); REQUIRE(audio.getMagnitude(0,2051)==0);
    }
    p.prepareToPlay(48000,-1); fill(audio,0.25f); p.processBlock(audio,midi); REQUIRE(audio.getMagnitude(0,2051)==0);
    p.prepareToPlay(48000,32); p.releaseResources(); fill(audio,0.25f); p.processBlock(audio,midi); REQUIRE(audio.getMagnitude(0,2051)==0);
    REQUIRE(p.setBusesLayout(layout(juce::AudioChannelSet::mono(),juce::AudioChannelSet::mono())));
    p.prepareToPlay(96000,1); fill(audio,0.125f); p.processBlock(audio,midi);
    REQUIRE(audio.getMagnitude(1,0,2051)==0);
}
TEST_CASE("Adapter production callbacks and reset allocate no storage", "[adapter][realtime]")
{
    PluginProcessor p; p.prepareToPlay(48000,32); juce::MidiBuffer midi;
    for(const int frames : {0,1,32,127,2051})
    {
        juce::AudioBuffer<float> audio(2,frames); fill(audio,0.25f);
        { disdorktion::test::ScopedAllocationTracking tracking; p.processBlock(audio,midi); p.processBlockBypassed(audio,midi); p.reset(); }
        const auto counts=disdorktion::test::getAllocationCounts(); REQUIRE(counts.allocations==0); REQUIRE(counts.deallocations==0);
    }
}
