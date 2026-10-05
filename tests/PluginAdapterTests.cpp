#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "plugin/PluginProcessor.h"
#include <limits>

using disdorktion::PluginProcessor;
namespace
{
void fill(juce::AudioBuffer<float>& audio, float value)
{
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int n = 0; n < audio.getNumSamples(); ++n) audio.setSample(c, n, value);
}
juce::AudioProcessor::BusesLayout layout(juce::AudioChannelSet input, juce::AudioChannelSet output)
{
    juce::AudioProcessor::BusesLayout result;
    result.inputBuses.add(input); result.outputBuses.add(output);
    return result;
}
void restore(PluginProcessor& processor, const juce::XmlElement& xml)
{
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(xml, block);
    processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
}
}

TEST_CASE("Adapter processes active channels and silences surplus storage", "[adapter]")
{
    PluginProcessor processor;
    REQUIRE(processor.setBusesLayout(layout(juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono())));
    processor.prepareToPlay(48000.0, 32);
    juce::AudioBuffer<float> audio(2, 127); juce::MidiBuffer midi;
    fill(audio, 0.25f); processor.processBlock(audio, midi);
    for (int n = 0; n < 127; ++n)
    {
        REQUIRE(audio.getSample(0, n) == 0.25f);
        REQUIRE(audio.getSample(1, n) == 0.0f);
    }
}

TEST_CASE("Adapter reset snapshots restored controls before processing resumes", "[adapter]")
{
    PluginProcessor processor;
    processor.prepareToPlay(48000.0, 32);
    juce::XmlElement xml("DisDorktionState");
    xml.setAttribute("version", "1"); xml.setAttribute("gain", "4"); xml.setAttribute("bypass", "0");
    restore(processor, xml);
    processor.reset();
    juce::AudioBuffer<float> audio(2, 1); juce::MidiBuffer midi;
    fill(audio, 0.25f); processor.processBlock(audio, midi);
    REQUIRE(audio.getSample(0, 0) == 1.0f);
    REQUIRE(audio.getSample(1, 0) == 1.0f);
}

TEST_CASE("Adapter decodes fractional host boolean controls", "[adapter]")
{
    PluginProcessor processor;
    auto* gain = dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters()[0]);
    auto* bypass = dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters()[1]);
    REQUIRE(gain != nullptr); REQUIRE(bypass != nullptr);
    gain->setValueNotifyingHost(gain->convertTo0to1(2.0f));
    processor.prepareToPlay(48000.0, 32);
    juce::AudioBuffer<float> audio(2, 1024); juce::MidiBuffer midi;
    for (const auto value : {0.1f, 0.7f, 0.499f, 0.5f, 1.0f})
    {
        bypass->setValueNotifyingHost(value);
        fill(audio, 0.25f); processor.processBlock(audio, midi);
        REQUIRE(audio.getSample(0, 1023) == (value >= 0.5f ? 0.25f : 0.5f));
    }
}

TEST_CASE("Adapter admits matching mono stereo and rejects other layouts", "[adapter]")
{
    PluginProcessor processor;
    REQUIRE(processor.isBusesLayoutSupported(layout(juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono())));
    REQUIRE(processor.isBusesLayoutSupported(layout(juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())));
    REQUIRE_FALSE(processor.isBusesLayoutSupported(layout(juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo())));
    REQUIRE_FALSE(processor.isBusesLayoutSupported(layout(juce::AudioChannelSet::disabled(), juce::AudioChannelSet::stereo())));
    REQUIRE_FALSE(processor.isBusesLayoutSupported(layout(juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1())));
}

TEST_CASE("Adapter lifecycle invalid buffers and failed reprepare produce silence", "[adapter]")
{
    PluginProcessor processor;
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> audio(2, 2051);
    fill(audio, 0.25f); processor.processBlock(audio, midi);
    REQUIRE(audio.getMagnitude(0, 2051) == 0.0f);
    processor.prepareToPlay(48000.0, 32);
    fill(audio, 0.25f); processor.processBlock(audio, midi);
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 2051; ++n) REQUIRE(audio.getSample(c, n) == 0.25f);
    REQUIRE(processor.getLatencySamples() == 0);
    juce::AudioBuffer<float> wrong(1, 7); fill(wrong, 0.25f);
    processor.processBlock(wrong, midi);
    REQUIRE(wrong.getMagnitude(0, 7) == 0.0f);
    for (const auto rate : {0.0, 7999.0, 384001.0, std::numeric_limits<double>::quiet_NaN()})
    {
        processor.prepareToPlay(rate, 32);
        fill(audio, 0.25f); processor.processBlock(audio, midi);
        REQUIRE(audio.getMagnitude(0, 2051) == 0.0f);
    }
    processor.prepareToPlay(48000.0, -1);
    fill(audio, 0.25f); processor.processBlock(audio, midi);
    REQUIRE(audio.getMagnitude(0, 2051) == 0.0f);
    processor.prepareToPlay(48000.0, 127);
    juce::AudioBuffer<float> empty(2, 0); processor.processBlock(empty, midi);
    processor.releaseResources(); fill(audio, 0.25f); processor.processBlock(audio, midi);
    REQUIRE(audio.getMagnitude(0, 2051) == 0.0f);
    REQUIRE(processor.setBusesLayout(layout(juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono())));
    processor.prepareToPlay(96000.0, 1);
    fill(wrong, 0.125f); processor.processBlock(wrong, midi);
    REQUIRE(wrong.getSample(0, 0) == 0.125f);
}

TEST_CASE("Adapter controls state roundtrip and invalid state retain valid values", "[adapter]")
{
    PluginProcessor processor;
    auto* gain = dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters()[0]);
    auto* bypass = dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters()[1]);
    REQUIRE(gain != nullptr); REQUIRE(bypass != nullptr);
    gain->setValueNotifyingHost(gain->convertTo0to1(2.0f));
    bypass->setValueNotifyingHost(0.0f);
    juce::MemoryBlock state; processor.getStateInformation(state);
    PluginProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    restored.prepareToPlay(48000.0, 32);
    juce::AudioBuffer<float> audio(2, 1024); juce::MidiBuffer midi;
    fill(audio, 0.25f); restored.processBlock(audio, midi);
    REQUIRE(audio.getSample(0, 0) == 0.5f);
    for (const auto value : {"nan", "inf", "-1", "5", "oops", "2tail"})
    {
        juce::XmlElement invalid("DisDorktionState");
        invalid.setAttribute("version", "1"); invalid.setAttribute("gain", value); invalid.setAttribute("bypass", "1");
        restore(restored, invalid);
        fill(audio, 0.25f); restored.processBlock(audio, midi);
        REQUIRE(audio.getSample(0, 1023) == 0.5f);
    }
    for (const auto xmlText : {"<Other version='1' gain='0' bypass='1'/>",
         "<DisDorktionState version='2' gain='0' bypass='1'/>",
         "<DisDorktionState version='1' gain='0'/>",
         "<DisDorktionState version='1' gain='0' bypass='2'/>"})
    {
        auto xml = juce::parseXML(xmlText); REQUIRE(xml != nullptr); restore(restored, *xml);
        fill(audio, 0.25f); restored.processBlock(audio, midi);
        REQUIRE(audio.getSample(0, 1023) == 0.5f);
    }
    const char garbage[] = "invalid";
    restored.setStateInformation(garbage, sizeof(garbage));
    fill(audio, 0.25f); restored.processBlock(audio, midi);
    REQUIRE(audio.getSample(0, 1023) == 0.5f);
    // Host controls are transported at the next callback, with a ten-ms transition.
    gain = dynamic_cast<juce::RangedAudioParameter*>(restored.getParameters()[0]);
    bypass = dynamic_cast<juce::RangedAudioParameter*>(restored.getParameters()[1]);
    bypass->setValueNotifyingHost(1.0f);
    fill(audio, 0.25f); restored.processBlock(audio, midi);
    REQUIRE(audio.getSample(0, 1023) == 0.25f);
    restored.reset(); audio.clear(); restored.processBlock(audio, midi);
    REQUIRE(audio.getMagnitude(0, 1024) == 0.0f);
}
