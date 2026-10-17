#include "harness/AuditionEngine.h"
#include "harness/ExperimentRecord.h"
#include "harness/OfflineRender.h"
#include "dsp/FullChainControls.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <limits>
#include <juce_audio_formats/juce_audio_formats.h>
using namespace disdorktion;
using namespace disdorktion::harness;
TEST_CASE("Full chain controls validate transactionally", "[full-chain][harness]")
{
    FullChainSettings settings;
    for (const auto& control : fullChainControls)
    {
        REQUIRE(setFullChainControl(settings,control.id,control.defaultValue));
        REQUIRE(getFullChainControl(settings,control.id)==control.defaultValue);
        REQUIRE_FALSE(setFullChainControl(settings,control.id,std::numeric_limits<float>::quiet_NaN()));
        REQUIRE(getFullChainControl(settings,control.id)==control.defaultValue);
    }
    REQUIRE_FALSE(setFullChainControl(settings,"bitDepth",9));
    REQUIRE_FALSE(setFullChainControl(settings,"hpResponse",.5f));
    REQUIRE_FALSE(setFullChainControl(settings,"chainBypass",.5f));
    REQUIRE_FALSE(setFullChainControl(settings,"unknown",0));
}
TEST_CASE("Full chain record eleven strictly round trips", "[full-chain][experiment]")
{
    ExperimentRecord record; record.version=11;record.targetId="full-chain";
    record.fullChain.input.gain.gainDb=12;record.fullChain.mix=.37f;
    record.fullChain.input.crusher.jitterSeed=0xffffffffu;
    record.fullChain.middle.folder.threshold=.27f;record.fullChain.finalGain.gainDb=-9;
    juce::String error; REQUIRE(record.validate(error));
    ExperimentRecord restored;REQUIRE(ExperimentRecord::fromJson(record.toJson(),restored,error));
    const auto before=juce::JSON::toString(restored.toJson());
    REQUIRE(before==juce::JSON::toString(record.toJson()));
    for(auto key:{"input","middle","output","globalMix","finalGainDb","finalMute","finalBypass","middleQuality"})
    {
        auto broken=record.toJson();broken.getDynamicObject()->removeProperty(key);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
        REQUIRE(juce::JSON::toString(restored.toJson())==before);
    }
    for(auto key:{"gain","gainDb","bitDepth","hpCutoffHz","tanhDriveDb","jitterSeed","mix"})
    {
        auto broken=record.toJson();broken.getDynamicObject()->setProperty(key,0);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(auto key:{"chainBypass","finalMute","finalBypass"})
    {
        auto broken=record.toJson();broken.getDynamicObject()->setProperty(key,1);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(auto key:{"globalMix","finalGainDb"})for(double invalid:{1e6,std::numeric_limits<double>::quiet_NaN()})
    {
        auto broken=record.toJson();broken.getDynamicObject()->setProperty(key,invalid);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(auto key:{"input","middle","output"})
    {
        auto broken=record.toJson();broken[key].getDynamicObject()->setProperty("bypass",true);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    {
        auto broken=record.toJson();broken["input"].getDynamicObject()->setProperty("jitterSeed",4294967296.);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    {
        auto broken=record.toJson();broken["middle"].getDynamicObject()->setProperty("hpCutoffHz",20);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(int version=1;version<=10;++version)
    {
        ExperimentRecord old;old.version=version;
        old.targetId=version==1?"reference-gain":version==2?"gain":version<5?"bit-crusher":version==5?"tanh-saturation":version==6?"wave-folder":version==7?"high-pass":version==8?"input-section":version==9?"middle-section":"output-section";
        REQUIRE(ExperimentRecord::fromJson(old.toJson(),restored,error));
        REQUIRE(juce::JSON::toString(old.toJson())==juce::JSON::toString(restored.toJson()));
        auto broken=old.toJson();broken.getDynamicObject()->setProperty("globalMix",.5);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
}
TEST_CASE("Full chain engine equals shared DSP through live controls and reprepare", "[full-chain][harness]")
{
    AuditionEngine engine;FullChain reference;FullChainSettings settings;
    settings.mix=.35f;settings.input.gain.gainDb=9;settings.finalGain.gainDb=-3;
    REQUIRE(engine.selectTarget("full-chain"));REQUIRE(engine.setFullChainSettings(settings));
    REQUIRE(reference.setParameters(settings));
    for(double rate:{8000.,48000.,96000.,384000.})
    {
        REQUIRE(engine.prepare({rate,127,2}));REQUIRE(reference.prepare({rate,127,2}));
        REQUIRE(engine.getLatencySamples()==reference.getLatencySamples());
        for(int event=0;event<3;++event)
        {
            INFO("sample rate " << rate << ", event " << event);
            if(event==1){settings.bypass=true;engine.setBypass(true);}
            if(event==2){settings.mix=0;REQUIRE(engine.setFullChainControl("globalMix",0));}
            REQUIRE(reference.setParameters(settings));
            juce::AudioBuffer<float> a(2,1031),b(2,1031);
            for(int c=0;c<2;++c)for(int n=0;n<1031;++n)a.setSample(c,n,.4f*std::sin(n*.071f));
            b=a;REQUIRE(engine.process(juce::dsp::AudioBlock<float>(a),false));REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
            for(int c=0;c<2;++c)for(int n=0;n<1031;++n)
            {
                INFO("channel " << c << ", frame " << n);
                REQUIRE(a.getSample(c,n)==b.getSample(c,n));
            }
        }
    }
}

TEST_CASE("Full chain offline render retains leading latency and exact frame count", "[full-chain][render]")
{
    ExperimentRecord record;record.version=11;record.targetId="full-chain";
    record.renderDurationSamples=1031;record.blockSize=127;
    record.source.kind=SourceKind::impulse;record.source.amplitude=.5;
    record.fullChain.mix=0;record.fullChain.input.gain.gainDb=24;record.fullChain.finalGain.gainDb=-6;
    const auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("disdorktion-full",{},false);
    const auto result=renderExperiment(record,directory);REQUIRE(result.status==RenderStatus::success);
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(directory.getChildFile("rendered.wav").createInputStream().release(),true));
    REQUIRE(reader);REQUIRE(reader->lengthInSamples==1031);
    juce::AudioBuffer<float> rendered(2,1031),expected(2,1031);REQUIRE(reader->read(&rendered,0,1031,0,true,true));
    SignalSource source;REQUIRE(source.prepare(record.source,48000,2));source.process(juce::dsp::AudioBlock<float>(expected));
    FullChain chain;REQUIRE(chain.setParameters(record.fullChain));REQUIRE(chain.prepare({48000,127,2}));
    REQUIRE(result.latencySamples==chain.getLatencySamples());REQUIRE(result.latencySamples>0);
    REQUIRE(chain.process(juce::dsp::AudioBlock<float>(expected)));
    for(int c=0;c<2;++c)for(int n=0;n<1031;++n)REQUIRE(rendered.getSample(c,n)==expected.getSample(c,n));
    REQUIRE(rendered.getSample(0,0)==0);REQUIRE(rendered.getSample(0,static_cast<int>(result.latencySamples))>0);
    auto json=juce::JSON::parse(directory.getChildFile("result.json"));
    REQUIRE(json["renderPolicy"].toString()=="fixed-duration-uncompensated");REQUIRE_FALSE(static_cast<bool>(json["tailFlushed"]));
    REQUIRE(json["middleQuality"].toString()=="4x-fir-adaa-v1");REQUIRE(json.hasProperty("wetPeak"));REQUIRE(json.hasProperty("postHighPassPeak"));
    reader.reset();REQUIRE(directory.deleteRecursively());
}
