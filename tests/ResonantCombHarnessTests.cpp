#include "harness/AuditionEngine.h"
#include "harness/ExperimentRecord.h"
#include "harness/OfflineRender.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <juce_audio_formats/juce_audio_formats.h>
using namespace disdorktion;
using namespace disdorktion::harness;

TEST_CASE("Comb records preserve typed controls and reject foreign schemas", "[resonant-comb][experiment]")
{
    ExperimentRecord record; record.version=12; record.targetId="resonant-comb";
    juce::String error; REQUIRE(record.validate(error));
    auto json=record.toJson();
    REQUIRE(json.getProperty("delayMs",0)==juce::var(5.f));
    json.getDynamicObject()->setProperty("feedback",-.5);
    json.getDynamicObject()->setProperty("dampingEnabled",false);
    ExperimentRecord restored; REQUIRE(ExperimentRecord::fromJson(json,restored,error));
    REQUIRE(juce::JSON::toString(restored.toJson())==juce::JSON::toString(json));
    const auto before=juce::JSON::toString(restored.toJson());
    for(auto key:{"delayMs","feedforward","feedback","dampingHz","dampingEnabled","combMix","bypass"})
    {
        auto broken=restored.toJson();broken.getDynamicObject()->removeProperty(key);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
        REQUIRE(juce::JSON::toString(restored.toJson())==before);
    }
    for(auto key:{"gain","gainDb","mix","cutoffHz","input","hpCutoffHz","driveDb","nonsense"})
    {
        auto broken=json.clone();broken.getDynamicObject()->setProperty(key,0);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(auto key:{"delayMs","feedforward","feedback","dampingHz","combMix"})
    {
        auto broken=json.clone();broken.getDynamicObject()->setProperty(key,std::numeric_limits<double>::quiet_NaN());
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(auto key:{"dampingEnabled","bypass"})
    {
        auto broken=json.clone();broken.getDynamicObject()->setProperty(key,1);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    ExperimentRecord legacy;
    auto broken=legacy.toJson(); broken.getDynamicObject()->setProperty("delayMs",5);
    REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
}

TEST_CASE("Comb selection produces its delayed feedback response offline", "[resonant-comb][harness][render]")
{
    AuditionEngine engine; REQUIRE(engine.selectTarget("resonant-comb"));
    REQUIRE(engine.prepare({48000,7,1}));
    juce::AudioBuffer<float> audio(1,1000); audio.clear();audio.setSample(0,0,1);
    REQUIRE(engine.process(juce::dsp::AudioBlock<float>(audio),false));
    REQUIRE(audio.getSample(0,0)==1.f); REQUIRE(audio.getSample(0,240)!=0.f);
    REQUIRE(engine.getLatencySamples()==0);
    ExperimentRecord record;record.version=12;record.targetId="resonant-comb";
    record.renderDurationSamples=1000;record.blockSize=127;record.channels=1;
    record.source.kind=SourceKind::impulse;record.source.amplitude=1;
    const auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("disdorktion-comb",{},false);
    const auto result=renderExperiment(record,directory);REQUIRE(result.status==RenderStatus::success);REQUIRE(result.samples==1000);
    const auto metadata=juce::JSON::parse(directory.getChildFile("result.json").loadFileAsString());
    REQUIRE(metadata.getDynamicObject());
    REQUIRE(metadata.getProperty("renderPolicy",juce::var()).isString());
    REQUIRE(metadata.getProperty("renderPolicy",juce::var()).toString()=="fixed-duration-uncompensated");
    REQUIRE(metadata.getProperty("tailFlushed",juce::var()).isBool());
    REQUIRE_FALSE(static_cast<bool>(metadata.getProperty("tailFlushed",juce::var())));
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(directory.getChildFile("rendered.wav").createInputStream().release(),true));
    REQUIRE(reader);juce::AudioBuffer<float> rendered(1,1000);REQUIRE(reader->read(&rendered,0,1000,0,true,true));
    for(int n=0;n<1000;++n)REQUIRE(rendered.getSample(0,n)==audio.getSample(0,n));
    reader.reset();REQUIRE(directory.deleteRecursively());
}


TEST_CASE("Comb live transport matches retained typed DSP controls", "[resonant-comb][harness]")
{
    for(double rate:{8000.,48000.,192000.,384000.})for(unsigned channels:{1u,2u})
    {
        AuditionEngine engine;ResonantComb reference;ResonantCombSettings settings;
        settings.delayMs=.25f;settings.feedback=-.95f;settings.feedforward=1.f;settings.dampingEnabled=false;
        REQUIRE(engine.selectTarget("resonant-comb"));REQUIRE(engine.setResonantCombSettings(settings));
        REQUIRE(reference.setParameters(settings));REQUIRE(engine.prepare({rate,7,channels}));REQUIRE(reference.prepare({rate,7,channels}));
        juce::AudioBuffer<float> a(static_cast<int>(channels),1031),b(static_cast<int>(channels),1031);
        for(unsigned event=0;event<4;++event)
        {
            if(event==1)
            {
                settings.delayMs=40;settings.feedback=.95f;settings.feedforward=-1;settings.dampingHz=20;settings.dampingEnabled=true;settings.mix=.75f;
                for(const auto& control:resonantCombControls)REQUIRE(engine.setResonantCombControl(control.id,getResonantCombControl(settings,control.id)));
            }
            if(event==2){settings.bypass=true;engine.setBypass(true);}
            if(event==3){settings.bypass=false;engine.setBypass(false);}
            REQUIRE(reference.setParameters(settings));
            for(int c=0;c<a.getNumChannels();++c)for(int n=0;n<1031;++n)a.setSample(c,n,static_cast<float>(.2*std::sin((n+event*1031)*.071)*(c?-1:1)));
            b=a;REQUIRE(engine.process(juce::dsp::AudioBlock<float>(a),false));REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
            for(int c=0;c<a.getNumChannels();++c)for(int n=0;n<1031;++n)REQUIRE(a.getSample(c,n)==b.getSample(c,n));
        }
        for(auto id:{"delayMs","feedforward","feedback","dampingHz","mix","dampingEnabled","bypass"})
            REQUIRE_FALSE(engine.setResonantCombControl(id,std::numeric_limits<float>::quiet_NaN()));
        REQUIRE_FALSE(engine.setResonantCombControl("feedback",1));REQUIRE_FALSE(engine.setResonantCombControl("dampingEnabled",.5f));
        auto invalid=settings;invalid.delayMs=0;REQUIRE_FALSE(engine.setResonantCombSettings(invalid));
        REQUIRE_FALSE(engine.setResonantCombControl("missing",0));
    }
}

TEST_CASE("Comb record numeric limits are checked before float narrowing", "[resonant-comb][experiment]")
{
    ExperimentRecord record;record.version=12;record.targetId="resonant-comb";ExperimentRecord restored;juce::String error;
    for(const auto& [key,value]:std::initializer_list<std::pair<const char*,double>>{{"delayMs",.249999999},{"delayMs",40.0000001},
        {"feedforward",1.00000001},{"feedback",.950000001},{"feedback",-.950000001},{"dampingHz",19.9999999},{"dampingHz",20000.00001},{"combMix",1.00000001}})
    {
        auto broken=record.toJson();broken.getDynamicObject()->setProperty(key,value);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    ResonantCombTarget target;
    REQUIRE(target.descriptor().id=="resonant-comb");REQUIRE(target.descriptor().controls.size()==7);
    REQUIRE_FALSE(target.setControl("bypass",.5f));REQUIRE_FALSE(target.setControl("dampingEnabled",2));
    REQUIRE_FALSE(target.setControl("missing",1));REQUIRE(target.setControl("feedback",-.95f));
    REQUIRE(target.setControl("dampingEnabled",0));target.setBypass(true);REQUIRE(target.prepare({48000,32,1}));
    juce::AudioBuffer<float> audio(1,1000);for(int n=0;n<1000;++n)audio.setSample(0,n,.5f);
    REQUIRE(target.process(juce::dsp::AudioBlock<float>(audio)));for(int n=0;n<1000;++n)REQUIRE(audio.getSample(0,n)==.5f);
}
