#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "plugin/PluginProcessor.h"
#include "plugin/FactoryPresets.h"
#include "dsp/FullChainControls.h"
#include "AllocationTracking.h"
#include <cmath>

namespace
{
using disdorktion::PluginProcessor;
void control(PluginProcessor& p, const char* id, float value)
{
    if (std::string_view(id)=="bitDepth")
        for(std::size_t i=0;i<disdorktion::bitCrusherDepths.size();++i)
            if(value==static_cast<float>(disdorktion::bitCrusherDepths[i])) { value=static_cast<float>(i); break; }
    for (auto* candidate : p.getParameters())
        if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(candidate); parameter && parameter->paramID == id)
        { parameter->setValueNotifyingHost(parameter->convertTo0to1(value)); return; }
    FAIL("Missing control " << id);
}
std::unique_ptr<juce::XmlElement> state(PluginProcessor& p)
{
    juce::MemoryBlock block; p.getStateInformation(block);
    return juce::AudioProcessor::getXmlFromBinary(block.getData(), static_cast<int>(block.getSize()));
}
void restore(PluginProcessor& p, const juce::XmlElement& xml)
{
    juce::MemoryBlock block; juce::AudioProcessor::copyXmlToBinary(xml, block);
    p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
}
void signal(juce::AudioBuffer<float>& buffer, int offset)
{
    for (int c=0;c<buffer.getNumChannels();++c)
        for (int n=0;n<buffer.getNumSamples();++n)
            buffer.setSample(c,n,0.3f*std::sin(0.071f*static_cast<float>(n+offset))*(c==0?1.0f:-0.7f));
}
}

TEST_CASE("New plugin instances match the shared fixed full chain", "[adapter][fullchain]")
{
    for (const int channels : {1,2})
    {
        PluginProcessor plugin;
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add(channels==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo());
        layout.outputBuses.add(layout.inputBuses[0]); REQUIRE(plugin.setBusesLayout(layout));
        disdorktion::FullChain chain;
        REQUIRE(chain.setParameters(disdorktion::pluginDefaultSettings()));
        REQUIRE(chain.prepare({48000.0,32,static_cast<juce::uint32>(channels)}));
        plugin.prepareToPlay(48000.0,32);
        REQUIRE(plugin.getLatencySamples()==static_cast<int>(chain.getLatencySamples()));
        REQUIRE(plugin.getLatencySamples()>0);
        juce::MidiBuffer midi; int offset=0;
        for (const int frames : {0,1,31,127,2051,32})
        {
            juce::AudioBuffer<float> actual(channels,frames), expected(channels,frames);
            signal(actual,offset); expected.makeCopyOf(actual);
            plugin.processBlock(actual,midi); REQUIRE(chain.process(juce::dsp::AudioBlock<float>(expected)));
            for(int c=0;c<channels;++c) for(int n=0;n<frames;++n)
                REQUIRE(actual.getSample(c,n)==Catch::Approx(expected.getSample(c,n)).margin(2e-6f));
            offset+=frames;
        }
    }
}

TEST_CASE("Plugin v5 stores every full chain scalar and rejects malformed state transactionally", "[adapter][fullchain][state]")
{
    PluginProcessor plugin;
    auto initial=state(plugin); REQUIRE(initial);
    REQUIRE(initial->getStringAttribute("version")=="5");
    REQUIRE_FALSE(initial->hasAttribute("mode"));
    for(const auto& item:disdorktion::fullChainControls) REQUIRE(initial->hasAttribute(item.id));
    for(const auto& item:disdorktion::fullChainControls)
    {
        auto bad=*initial; bad.removeAttribute(item.id); restore(plugin,bad);
        REQUIRE(state(plugin)->isEquivalentTo(initial.get(),true));
        for(const auto* value:{"nan","inf","1tail"," 1","999999999999"})
        {
            bad=*initial; bad.setAttribute(item.id,value); restore(plugin,bad);
            REQUIRE(state(plugin)->isEquivalentTo(initial.get(),true));
        }
    }
    for(const auto* version:{"1","2","3","4","0","5tail"})
    { auto bad=*initial; bad.setAttribute("version",version); restore(plugin,bad); REQUIRE(state(plugin)->isEquivalentTo(initial.get(),true)); }
    auto bad=*initial; bad.setAttribute("globalMix","1.0000000001"); restore(plugin,bad);
    REQUIRE(state(plugin)->isEquivalentTo(initial.get(),true));
    control(plugin,"inputGainDb",12.0f); control(plugin,"outputGainDb",-6.0f); control(plugin,"globalMix",0.37f);
    auto saved=state(plugin); PluginProcessor roundtrip; restore(roundtrip,*saved);
    INFO("Saved: " << saved->toString().toStdString());
    INFO("Restored: " << state(roundtrip)->toString().toStdString());
    REQUIRE(state(roundtrip)->isEquivalentTo(saved.get(),true));
    control(plugin,"folderThreshold",0.05f); control(plugin,"bitDepth",16.0f);
    saved=state(plugin); restore(roundtrip,*saved);
    REQUIRE(state(roundtrip)->isEquivalentTo(saved.get(),true));
    auto endpoint=*saved; endpoint.setAttribute("folderThreshold","0.05"); restore(roundtrip,endpoint);
    REQUIRE(state(roundtrip)->getDoubleAttribute("folderThreshold")==Catch::Approx(0.05));
    const auto baseline=state(roundtrip);
    for(const auto* depth:{"9","16.000000001"})
    { auto invalid=*saved; invalid.setAttribute("bitDepth",depth); restore(roundtrip,invalid); REQUIRE(state(roundtrip)->isEquivalentTo(baseline.get(),true)); }
    endpoint.setAttribute("folderThreshold","0.04999999999"); restore(roundtrip,endpoint);
    REQUIRE(state(roundtrip)->isEquivalentTo(baseline.get(),true));
}

TEST_CASE("Plugin host bypass emits exact delayed untrimmed input", "[adapter][fullchain]")
{
    PluginProcessor plugin;
    control(plugin,"driver",4.0f); control(plugin,"outputGainDb",24.0f);
    plugin.prepareToPlay(48000.0,32);
    const auto latency=plugin.getLatencySamples(); REQUIRE(latency>0);
    juce::AudioBuffer<float> audio(2,2051),original(2,2051); signal(audio,0); original.makeCopyOf(audio);
    juce::MidiBuffer midi; plugin.processBlockBypassed(audio,midi);
    for(int c=0;c<2;++c) for(int n=0;n<2051;++n)
        REQUIRE(audio.getSample(c,n)==(n<latency?0.0f:original.getSample(c,n-latency)));
    REQUIRE(plugin.getLatencySamples()==latency);
    // The delay has stayed hot through ordinary processing before host bypass.
    signal(audio,2051); original.makeCopyOf(audio); plugin.processBlock(audio,midi);
    signal(audio,4102); plugin.processBlockBypassed(audio,midi);
    for(int c=0;c<2;++c) for(int n=0;n<latency;++n)
        REQUIRE(audio.getSample(c,n)==original.getSample(c,2051-latency+n));
}

TEST_CASE("Plugin automation follows shared controls including section mixes and Driver", "[adapter][fullchain][issue38]")
{
    PluginProcessor plugin;
    disdorktion::FullChain reference;
    auto settings = disdorktion::pluginDefaultSettings();
    REQUIRE(reference.setParameters(settings));
    plugin.prepareToPlay(48000, 32); REQUIRE(reference.prepare({48000,32,2}));
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> actual(2,617), expected(2,617);
    struct Change { const char* id; float value; };
    const Change changes[] {{"driver",4},{"inputGainDb",12},{"globalMix",0},
        {"crusherMix",0},{"waveshaperMix",0},{"waveshaperTrimDb",6},
        {"tanhDriveDb",-24},{"folderDriveDb",-24},{"hpCutoffHz",20},{"lpCutoffHz",20000},
        {"globalMix",0.37f},{"waveshaperMix",0.8f},{"folderDriveDb",9},
        {"combMix",0},{"combMix",0.75f},{"dispersionMix",0.7f},{"toneDb",6},{"driver",1}};
    int offset = 0;
    for (const auto& change : changes)
    {
        CAPTURE(change.id,change.value);
        control(plugin,change.id,change.value);
        REQUIRE(disdorktion::setFullChainControl(settings,change.id,change.value));
        REQUIRE(reference.setParameters(settings));
        signal(actual,offset); expected.makeCopyOf(actual);
        plugin.processBlock(actual,midi); REQUIRE(reference.process(juce::dsp::AudioBlock<float>(expected)));
        for(int c=0;c<2;++c) for(int n=0;n<617;++n)
            REQUIRE(actual.getSample(c,n)==Catch::Approx(expected.getSample(c,n)).margin(3e-5f));
        offset += 617;
    }
}

TEST_CASE("Plugin Driver automation cancels after global dry split with Input Gain retained", "[adapter][fullchain][realtime][issue38]")
{
    PluginProcessor plugin;
    control(plugin,"globalMix",0); control(plugin,"inputGainDb",6);
    plugin.prepareToPlay(48000,32);
    const int latency = plugin.getLatencySamples();
    REQUIRE(latency > 0);
    juce::MidiBuffer midi; int offset=0;
    for(const int frames : {1,31,617,7,2051,32})
    {
        control(plugin,"driver",(offset%2) ? 4.0f : 1.0f);
        juce::AudioBuffer<float> audio(2,frames); signal(audio,offset);
        { disdorktion::test::ScopedAllocationTracking tracking; plugin.processBlock(audio,midi); }
        const auto counts=disdorktion::test::getAllocationCounts();
        REQUIRE(counts.allocations==0); REQUIRE(counts.deallocations==0);
        for(int c=0;c<2;++c) for(int n=0;n<frames;++n)
        {
            const auto t=offset+n-latency;
            const auto expected=t<0 ? 0.0f : 0.3f*std::sin(0.071f*t)*(c==0?1.0f:-0.7f)*juce::Decibels::decibelsToGain(6.0f);
            REQUIRE(audio.getSample(c,n)==Catch::Approx(expected).margin(2e-6f));
        }
        REQUIRE(plugin.getLatencySamples()==latency); offset+=frames;
    }
}

TEST_CASE("Plugin v5 restore flushes wet and host bypass histories without changing latency", "[adapter][fullchain][state]")
{
    PluginProcessor plugin;
    const auto clean=state(plugin);
    control(plugin,"combDelayMs",40); control(plugin,"combFeedback",0.95f); control(plugin,"combMix",1);
    control(plugin,"dispersionMix",1); control(plugin,"toneDb",6);
    plugin.prepareToPlay(48000,32);
    const auto latency=plugin.getLatencySamples();
    juce::MidiBuffer midi; juce::AudioBuffer<float> audio(2,4096);
    signal(audio,0); plugin.processBlock(audio,midi);
    restore(plugin,*clean);
    REQUIRE(plugin.getLatencySamples()==latency);
    REQUIRE(plugin.getTailLengthSeconds()==Catch::Approx(20.0+latency/48000.0));
    audio.clear(); plugin.processBlock(audio,midi);
    REQUIRE(audio.getMagnitude(0,audio.getNumSamples())==0);
    audio.clear(); plugin.processBlockBypassed(audio,midi);
    REQUIRE(audio.getMagnitude(0,audio.getNumSamples())==0);
    control(plugin,"combMix",1); control(plugin,"dispersionMix",1);
    audio.clear(); plugin.processBlock(audio,midi);
    REQUIRE(audio.getMagnitude(0,audio.getNumSamples())==0);
}
