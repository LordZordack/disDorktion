#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "plugin/PluginProcessor.h"
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

TEST_CASE("Plugin v3 stores every full chain scalar and rejects malformed state transactionally", "[adapter][fullchain][state]")
{
    PluginProcessor plugin;
    auto initial=state(plugin); REQUIRE(initial);
    REQUIRE(initial->getStringAttribute("version")=="3");
    REQUIRE(initial->getStringAttribute("mode")=="fullChain");
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
    for(const auto* version:{"4","0","3tail"})
    { auto bad=*initial; bad.setAttribute("version",version); restore(plugin,bad); REQUIRE(state(plugin)->isEquivalentTo(initial.get(),true)); }
    auto bad=*initial; bad.setAttribute("mode","future"); restore(plugin,bad);
    REQUIRE(state(plugin)->isEquivalentTo(initial.get(),true));
    bad=*initial; bad.setAttribute("globalMix","1.0000000001"); restore(plugin,bad);
    REQUIRE(state(plugin)->isEquivalentTo(initial.get(),true));
    control(plugin,"inputGainDb",12.0f); control(plugin,"outputGainDb",-6.0f); control(plugin,"globalMix",0.37f);
    auto saved=state(plugin); PluginProcessor roundtrip; restore(roundtrip,*saved);
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

TEST_CASE("Plugin legacy mode changes latency before and after prepare and survives v3 roundtrip", "[adapter][fullchain][state]")
{
    for(const bool prepared:{false,true}) for(const auto* version:{"1","2"})
    {
        PluginProcessor plugin; auto modern=state(plugin);
        disdorktion::FullChain reference; REQUIRE(reference.prepare({48000.0,32,2}));
        const auto modernLatency=static_cast<int>(reference.getLatencySamples());
        if(prepared) plugin.prepareToPlay(48000.0,32);
        juce::XmlElement legacy("DisDorktionState"); legacy.setAttribute("version",version);
        legacy.setAttribute("gain","2"); legacy.setAttribute("bypass","0");
        if(juce::String(version)=="2") for(const auto* id:{"inputGainDb","inputMute","inputBypass","outputGainDb","outputMute","outputBypass"}) legacy.setAttribute(id,"0");
        restore(plugin,legacy); if(!prepared) plugin.prepareToPlay(48000.0,32);
        REQUIRE(plugin.getLatencySamples()==0);
        juce::AudioBuffer<float> audio(2,1024); for(int c=0;c<2;++c) for(int n=0;n<1024;++n) audio.setSample(c,n,0.25f);
        juce::MidiBuffer midi; plugin.processBlock(audio,midi); REQUIRE(audio.getSample(0,1023)==0.5f);
        auto migrated=state(plugin); REQUIRE(migrated->getStringAttribute("mode")=="legacyGain");
        PluginProcessor roundtrip; roundtrip.prepareToPlay(48000.0,32); restore(roundtrip,*migrated); REQUIRE(roundtrip.getLatencySamples()==0);
        restore(plugin,*modern); REQUIRE(plugin.getLatencySamples()==modernLatency);
    }
}

TEST_CASE("Plugin dry capture precedes drive and host bypass retains latency without allocation", "[adapter][fullchain][realtime]")
{
    PluginProcessor quiet,driven;
    for(auto* p:{&quiet,&driven}) { control(*p,"globalMix",0.0f); p->prepareToPlay(48000.0,32); }
    control(driven,"inputGainDb",24.0f); control(driven,"inputMute",1.0f);
    juce::MidiBuffer midi; juce::AudioBuffer<float> a(2,2051),b(2,2051);
    signal(a,0); b.makeCopyOf(a); quiet.processBlock(a,midi); driven.processBlock(b,midi);
    for(int c=0;c<2;++c) for(int n=0;n<2051;++n) REQUIRE(a.getSample(c,n)==b.getSample(c,n));
    const auto latency=driven.getLatencySamples();
    {
        disdorktion::test::ScopedAllocationTracking tracking;
        driven.processBlockBypassed(b,midi); driven.reset(); driven.processBlock(b,midi);
    }
    const auto counts=disdorktion::test::getAllocationCounts();
    REQUIRE(counts.allocations==0); REQUIRE(counts.deallocations==0);
    REQUIRE(driven.getLatencySamples()==latency);
}

TEST_CASE("Plugin full chain automation final gain and mix endpoints match shared settings", "[adapter][fullchain]")
{
    PluginProcessor plugin;
    disdorktion::FullChain chain;
    disdorktion::FullChainSettings settings;
    plugin.prepareToPlay(48000.0,32); REQUIRE(chain.prepare({48000.0,32,2}));
    juce::MidiBuffer midi; juce::AudioBuffer<float> actual(2,617),expected(2,617);
    int offset=0;
    for(const float mix:{0.0f,0.25f,0.5f,0.75f,1.0f})
        for(const float finalDb:{-60.0f,0.0f,24.0f})
            for(const bool bypassed:{false,true})
            {
                control(plugin,"globalMix",mix); settings.mix=mix;
                control(plugin,"outputGainDb",finalDb); settings.finalGain.gainDb=finalDb;
                control(plugin,"chainBypass",bypassed?1.0f:0.0f); settings.bypass=bypassed;
                control(plugin,"inputGainDb",12.0f); settings.input.gain.gainDb=12.0f;
                REQUIRE(chain.setParameters(settings));
                signal(actual,offset); expected.makeCopyOf(actual);
                plugin.processBlock(actual,midi); REQUIRE(chain.process(juce::dsp::AudioBlock<float>(expected)));
                for(int c=0;c<2;++c) for(int n=0;n<617;++n)
                    REQUIRE(actual.getSample(c,n)==Catch::Approx(expected.getSample(c,n)).margin(3e-5f));
                offset+=617;
            }
}

TEST_CASE("Plugin host bypass emits exact delayed untrimmed input", "[adapter][fullchain]")
{
    PluginProcessor plugin;
    control(plugin,"referenceGain",4.0f); control(plugin,"outputGainDb",24.0f);
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

TEST_CASE("Plugin mode transitions discard dormant full chain delay and filter histories", "[adapter][fullchain][state]")
{
    for(const float mix:{0.0f,1.0f})
    {
        PluginProcessor plugin;
        control(plugin,"globalMix",mix);
        auto modern=state(plugin);
        plugin.prepareToPlay(48000.0,32);
        juce::AudioBuffer<float> audio(2,617); juce::MidiBuffer midi;
        for(int c=0;c<2;++c) for(int n=0;n<617;++n) audio.setSample(c,n,0.75f);
        plugin.processBlock(audio,midi);
        juce::XmlElement legacy("DisDorktionState");
        legacy.setAttribute("version","1"); legacy.setAttribute("gain","1"); legacy.setAttribute("bypass","0");
        restore(plugin,legacy); REQUIRE(plugin.getLatencySamples()==0);
        audio.clear(); plugin.processBlock(audio,midi);
        restore(plugin,*modern); REQUIRE(plugin.getLatencySamples()>0);
        // A stopped path must never replay samples from before the mode change.
        audio.clear(); plugin.processBlock(audio,midi);
        REQUIRE(audio.getMagnitude(0,617)==0.0f);
        REQUIRE(audio.getMagnitude(1,0,617)==0.0f);
    }
}

TEST_CASE("Plugin publishes full chain tail independently of legacy compatibility mode", "[adapter][fullchain][state]")
{
    PluginProcessor plugin;
    REQUIRE(plugin.getTailLengthSeconds()==3.0);
    const auto modern=state(plugin);
    plugin.prepareToPlay(48000.0,32);
    REQUIRE(plugin.getTailLengthSeconds()==Catch::Approx(3.0+plugin.getLatencySamples()/48000.0));
    juce::XmlElement legacy("DisDorktionState");
    legacy.setAttribute("version","1"); legacy.setAttribute("gain","1"); legacy.setAttribute("bypass","0");
    restore(plugin,legacy);
    REQUIRE(plugin.getTailLengthSeconds()==0.0);
    restore(plugin,*modern);
    REQUIRE(plugin.getTailLengthSeconds()==Catch::Approx(3.0+plugin.getLatencySamples()/48000.0));
    plugin.prepareToPlay(96000.0,127);
    REQUIRE(plugin.getTailLengthSeconds()==Catch::Approx(3.0+plugin.getLatencySamples()/96000.0));
    PluginProcessor unprepared; restore(unprepared,legacy);
    REQUIRE(unprepared.getTailLengthSeconds()==0.0);
    restore(unprepared,*modern); REQUIRE(unprepared.getTailLengthSeconds()==3.0);
}
