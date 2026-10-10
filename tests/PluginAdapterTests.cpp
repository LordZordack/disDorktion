#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "plugin/PluginProcessor.h"
#include <limits>
#include "AllocationTracking.h"

using disdorktion::PluginProcessor;
namespace
{
void restore(PluginProcessor& processor, const juce::XmlElement& xml);
void fill(juce::AudioBuffer<float>& audio, float value)
{
    for (int c = 0; c < audio.getNumChannels(); ++c)
        for (int n = 0; n < audio.getNumSamples(); ++n) audio.setSample(c, n, value);
}

TEST_CASE("Adapter appends production host controls after unchanged legacy controls", "[adapter][production]")
{
    PluginProcessor processor;
    const auto& controls = processor.getParameters();
    REQUIRE(controls.size() == 8);
    const char* ids[] = {"referenceGain", "referenceBypass", "inputGainDb", "inputMute",
                         "inputBypass", "outputGainDb", "outputMute", "outputBypass"};
    for (int i = 0; i < 8; ++i)
    {
        const auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(controls[i]);
        REQUIRE(parameter != nullptr);
        REQUIRE(parameter->paramID == ids[i]);
        REQUIRE(parameter->convertFrom0to1(parameter->getDefaultValue()) == (i == 0 ? 1.0f : 0.0f));
        if (i == 2 || i == 5)
        {
            REQUIRE(parameter->convertFrom0to1(0.0f) == -60.0f);
            REQUIRE(parameter->convertFrom0to1(1.0f) == 24.0f);
        }
    }
}

namespace
{
juce::RangedAudioParameter& hostControl(PluginProcessor& processor, int index)
{
    REQUIRE(processor.getParameters().size() == 8);
    auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(processor.getParameters()[index]);
    REQUIRE(parameter != nullptr);
    return *parameter;
}
void setHostControl(PluginProcessor& processor, int index, float value)
{
    auto& parameter = hostControl(processor, index);
    parameter.setValueNotifyingHost(parameter.convertTo0to1(value));
}
std::unique_ptr<juce::XmlElement> savedControls(PluginProcessor& processor)
{
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    return juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
}
juce::XmlElement productionState()
{
    juce::XmlElement xml("DisDorktionState");
    xml.setAttribute("version", "2");
    for (const auto* attribute : {"gain", "bypass", "inputGainDb", "inputMute", "inputBypass",
                                 "outputGainDb", "outputMute", "outputBypass"})
        xml.setAttribute(attribute, juce::String(attribute) == "gain" ? "1" : "0");
    return xml;
}
}

TEST_CASE("Adapter production endpoints mute and bypass are independent at either position", "[adapter][production]")
{
    for (const int position : {2, 5})
        for (const float db : {-60.0f, 0.0f, 24.0f})
            for (const bool muted : {false, true})
                for (const bool bypassed : {false, true})
                {
                    PluginProcessor processor;
                    setHostControl(processor, position, db);
                    setHostControl(processor, position + 1, muted ? 1.0f : 0.0f);
                    setHostControl(processor, position + 2, bypassed ? 1.0f : 0.0f);
                    processor.prepareToPlay(48000.0, 32);
                    juce::AudioBuffer<float> audio(2, 127); juce::MidiBuffer midi;
                    for (int n = 0; n < 127; ++n) { audio.setSample(0, n, 0.25f); audio.setSample(1, n, -0.125f); }
                    processor.processBlock(audio, midi);
                    const double factor = bypassed ? 1.0 : muted ? 0.0 : std::pow(10.0, db / 20.0);
                    for (int n = 0; n < 127; ++n)
                    {
                        REQUIRE(audio.getSample(0, n) == Catch::Approx(0.25 * factor).margin(2e-5));
                        REQUIRE(audio.getSample(1, n) == Catch::Approx(-0.125 * factor).margin(2e-5));
                        if (muted && !bypassed) REQUIRE(audio.getSample(0, n) == 0.0f);
                        if (bypassed || db == 0.0f && !muted) REQUIRE(audio.getSample(0, n) == 0.25f);
                    }
                }
}

TEST_CASE("Adapter production pair follows independent opposite ramps and reset snapshots", "[adapter][production]")
{
    PluginProcessor processor, separate;
    processor.prepareToPlay(48000.0, 32); separate.prepareToPlay(48000.0, 32);
    setHostControl(processor, 2, 24.0f); setHostControl(processor, 5, -60.0f);
    juce::AudioBuffer<float> audio(2, 480), other(2, 480); juce::MidiBuffer midi;
    fill(audio, 0.125f); fill(other, 0.125f);
    processor.processBlock(audio, midi); separate.processBlock(other, midi);
    const double inputEnd = std::pow(10.0, 24.0 / 20.0);
    for (int n = 0; n < 480; ++n)
    {
        const double fraction = (n + 1) / 480.0;
        const double expected = 0.125 * (1.0 + (inputEnd - 1.0) * fraction) * (1.0 + (0.001 - 1.0) * fraction);
        REQUIRE(audio.getSample(0, n) == Catch::Approx(expected).margin(2e-5));
        REQUIRE(other.getSample(0, n) == 0.125f);
    }
    setHostControl(processor, 3, 1.0f); processor.reset();
    fill(audio, 0.125f); processor.processBlock(audio, midi);
    REQUIRE(audio.getMagnitude(0, 480) == 0.0f);
    fill(other, 0.125f); separate.processBlock(other, midi);
    REQUIRE(other.getSample(0, 0) == 0.125f);
}

TEST_CASE("Adapter legacy imports preserve tiny linear values and reset all production defaults", "[adapter][production]")
{
    for (const float value : {0.0f, 0.0000001f, 0.0001f, 0.001f, 1.0f, 4.0f})
        for (const bool bypass : {false, true})
        {
            PluginProcessor processor;
            setHostControl(processor, 2, 24.0f); setHostControl(processor, 3, 1.0f);
            setHostControl(processor, 4, 1.0f); setHostControl(processor, 5, -60.0f);
            setHostControl(processor, 6, 1.0f); setHostControl(processor, 7, 1.0f);
            juce::XmlElement xml("DisDorktionState");
            xml.setAttribute("version", "1"); xml.setAttribute("gain", static_cast<double>(value));
            xml.setAttribute("bypass", bypass ? "1" : "0"); restore(processor, xml);
            for (int index = 2; index < 8; ++index)
                REQUIRE(hostControl(processor, index).convertFrom0to1(hostControl(processor, index).getValue()) == 0.0f);
            processor.prepareToPlay(48000.0, 32);
            juce::AudioBuffer<float> audio(2, 1); juce::MidiBuffer midi;
            fill(audio, 0.25f); processor.processBlock(audio, midi);
            REQUIRE(audio.getSample(0, 0) == (bypass ? 0.25f : 0.25f * value));
            auto saved = savedControls(processor); REQUIRE(saved != nullptr);
            REQUIRE(saved->getStringAttribute("version") == "2");
            REQUIRE(static_cast<float>(saved->getDoubleAttribute("gain")) == value);
        }
}

TEST_CASE("Adapter v2 restores synchronized host values raw DSP controls and roundtrip state", "[adapter][production]")
{
    PluginProcessor processor;
    auto xml = productionState();
    xml.setAttribute("gain", "2"); xml.setAttribute("inputGainDb", "24"); xml.setAttribute("outputGainDb", "-60");
    restore(processor, xml);
    REQUIRE(hostControl(processor, 0).convertFrom0to1(hostControl(processor, 0).getValue()) == 2.0f);
    REQUIRE(hostControl(processor, 2).convertFrom0to1(hostControl(processor, 2).getValue()) == 24.0f);
    REQUIRE(hostControl(processor, 5).convertFrom0to1(hostControl(processor, 5).getValue()) == -60.0f);
    auto saved = savedControls(processor); REQUIRE(saved != nullptr);
    REQUIRE(saved->isEquivalentTo(&xml, true));
    PluginProcessor restored; restore(restored, *saved); restored.prepareToPlay(48000.0, 32);
    juce::AudioBuffer<float> audio(2, 1); juce::MidiBuffer midi;
    fill(audio, 0.25f); restored.processBlock(audio, midi);
    REQUIRE(audio.getSample(0, 0) == Catch::Approx(0.5 * std::pow(10.0, 24.0 / 20.0) * 0.001).margin(2e-5));
    for (int index = 0; index < 8; ++index)
        REQUIRE(hostControl(restored, index).getValue() == hostControl(processor, index).getValue());
}

TEST_CASE("Adapter rejects incomplete or strictly invalid v2 state before publishing controls", "[adapter][production]")
{
    PluginProcessor processor; auto valid = productionState();
    valid.setAttribute("gain", "2"); valid.setAttribute("inputGainDb", "24");
    valid.setAttribute("outputMute", "1"); restore(processor, valid);
    const auto baseline = savedControls(processor); REQUIRE(baseline != nullptr);
    for (const auto* attribute : {"gain", "bypass", "inputGainDb", "inputMute", "inputBypass", "outputGainDb", "outputMute", "outputBypass"})
    {
        auto missing = productionState(); missing.removeAttribute(attribute); restore(processor, missing);
        REQUIRE(savedControls(processor)->isEquivalentTo(baseline.get(), true));
        for (const auto* malformed : {"", "nan", "inf", "1tail", " 1", "1 ", "999", "-999"})
        {
            auto invalid = productionState(); invalid.setAttribute(attribute, malformed); restore(processor, invalid);
            REQUIRE(savedControls(processor)->isEquivalentTo(baseline.get(), true));
        }
    }
    for (const auto* attribute : {"bypass", "inputMute", "inputBypass", "outputMute", "outputBypass"})
        for (const auto* malformed : {"0.0", "1.0", "true", "false", "01", "-0"})
        {
            auto invalid = productionState(); invalid.setAttribute(attribute, malformed); restore(processor, invalid);
            REQUIRE(savedControls(processor)->isEquivalentTo(baseline.get(), true));
        }
    for (const auto* attribute : {"inputGainDb", "outputGainDb"})
        for (const auto* outside : {"24.0000001", "-60.0000001"})
        {
            auto invalid = productionState(); invalid.setAttribute(attribute, outside); restore(processor, invalid);
            REQUIRE(savedControls(processor)->isEquivalentTo(baseline.get(), true));
        }
}

TEST_CASE("Adapter v1 restores match the unchanged legacy reference including active ramps", "[adapter][production]")
{
    for (const float value : {0.0f, 0.0000001f, 0.0001f, 0.001f, 1.0f, 4.0f})
        for (const bool bypass : {false, true})
        {
            PluginProcessor processor; processor.prepareToPlay(48000.0, 32);
            disdorktion::ReferenceGain reference; REQUIRE(reference.prepare({48000.0, 32, 2}));
            juce::XmlElement xml("DisDorktionState"); xml.setAttribute("version", "1");
            xml.setAttribute("gain", static_cast<double>(value)); xml.setAttribute("bypass", bypass ? "1" : "0");
            restore(processor, xml); REQUIRE(reference.setParameters({value, bypass}));
            juce::AudioBuffer<float> actual(2, 617), expected(2, 617); juce::MidiBuffer midi;
            fill(actual, 0.25f); fill(expected, 0.25f);
            processor.processBlock(actual, midi);
            REQUIRE(reference.process(juce::dsp::AudioBlock<float>(expected)));
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < 617; ++n) REQUIRE(actual.getSample(c, n) == expected.getSample(c, n));
        }
}

TEST_CASE("Adapter restores notify parameter attachments and fractional float state roundtrips", "[adapter][production]")
{
    PluginProcessor processor;
    float attachmentValue = 0.0f;
    int notifications = 0;
    juce::ParameterAttachment attachment(hostControl(processor, 2), [&](float value)
        { attachmentValue = value; ++notifications; }, nullptr);
    auto xml = productionState(); xml.setAttribute("inputGainDb", "12.345678");
    restore(processor, xml);
    REQUIRE(notifications > 0);
    REQUIRE(attachmentValue == hostControl(processor, 2).convertFrom0to1(hostControl(processor, 2).getValue()));
    const auto saved = savedControls(processor); REQUIRE(saved != nullptr);
    PluginProcessor restored; restore(restored, *saved);
    REQUIRE(hostControl(restored, 2).getValue() == hostControl(processor, 2).getValue());
    REQUIRE(savedControls(restored)->isEquivalentTo(saved.get(), true));
}

TEST_CASE("Adapter production callbacks and resets allocate no storage", "[adapter][production][realtime]")
{
    PluginProcessor processor; processor.prepareToPlay(48000.0, 32);
    juce::AudioBuffer<float> audio(2, 2051), wrong(1, 7), empty(2, 0); juce::MidiBuffer midi;
    processor.processBlock(audio, midi); // Warm up external callback helpers before measuring.
    for (const bool mute : {false, true})
        for (const bool bypass : {false, true})
        {
            setHostControl(processor, 2, 24.0f); setHostControl(processor, 5, -60.0f);
            setHostControl(processor, 3, mute ? 1.0f : 0.0f); setHostControl(processor, 7, bypass ? 1.0f : 0.0f);
            {
                disdorktion::test::ScopedAllocationTracking tracking;
                processor.processBlock(audio, midi); processor.processBlock(empty, midi);
                processor.processBlock(wrong, midi); processor.reset();
                processor.processBlock(audio, midi); processor.releaseResources();
                processor.processBlock(audio, midi);
            }
            const auto counts = disdorktion::test::getAllocationCounts();
            REQUIRE(counts.allocations == 0); REQUIRE(counts.deallocations == 0);
            processor.prepareToPlay(48000.0, 32);
        }
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
