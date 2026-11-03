#include "harness/AuditionEngine.h"
#include "harness/ExperimentRecord.h"
#include "harness/OfflineRender.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <juce_audio_formats/juce_audio_formats.h>
using namespace disdorktion;
using namespace disdorktion::harness;

TEST_CASE("Dispersion records round trip and reject mixed or narrowed schemas", "[frequency-dispersion][experiment]")
{
    ExperimentRecord record;record.version=13;record.targetId="frequency-dispersion";
    record.dispersion={200.f,.5f,true};
    juce::String error;REQUIRE(record.validate(error));
    auto json=record.toJson();ExperimentRecord restored;
    REQUIRE(ExperimentRecord::fromJson(json,restored,error));
    REQUIRE(juce::JSON::toString(restored.toJson())==juce::JSON::toString(json));
    const auto before=juce::JSON::toString(restored.toJson());
    for(auto key:{"dispersionFrequencyHz","dispersionMix","bypass"})
    {
        auto broken=json.clone();broken.getDynamicObject()->removeProperty(key);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
        REQUIRE(juce::JSON::toString(restored.toJson())==before);
    }
    for(auto key:{"gain","gainDb","mix","cutoffHz","input","delayMs","nonsense"})
    {
        auto broken=json.clone();broken.getDynamicObject()->setProperty(key,0);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(auto key:{"dispersionFrequencyHz","dispersionMix"})
    {
        auto broken=json.clone();broken.getDynamicObject()->setProperty(key,std::numeric_limits<double>::quiet_NaN());
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(const auto& [key,value]:std::initializer_list<std::pair<const char*,double>>{
        {"dispersionFrequencyHz",19.9999999},{"dispersionFrequencyHz",20000.000001},{"dispersionMix",1.00000001}})
    {
        auto broken=json.clone();broken.getDynamicObject()->setProperty(key,value);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    auto broken=json.clone();broken.getDynamicObject()->setProperty("bypass",1);
    REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    ExperimentRecord legacy;broken=legacy.toJson();broken.getDynamicObject()->setProperty("dispersionMix",1);
    REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    legacy.dispersion.frequencyHz=400;REQUIRE_FALSE(legacy.validate(error));
}

TEST_CASE("Dispersion live transport matches typed module and retains controls", "[frequency-dispersion][harness]")
{
    for(double rate:{8000.,48000.,192000.,384000.})for(unsigned channels:{1u,2u})
    {
        AuditionEngine engine;FrequencyDispersion reference;FrequencyDispersionSettings settings{20.f,.75f,false};
        REQUIRE(engine.selectTarget("frequency-dispersion"));REQUIRE(engine.setFrequencyDispersionSettings(settings));
        REQUIRE(reference.setParameters(settings));REQUIRE(engine.prepare({rate,7,channels}));REQUIRE(reference.prepare({rate,7,channels}));
        juce::AudioBuffer<float> a(static_cast<int>(channels),1031),b(static_cast<int>(channels),1031);
        for(unsigned event=0;event<4;++event)
        {
            if(event==1){settings.frequencyHz=20000;settings.mix=.5f;
                for(const auto& control:frequencyDispersionControls)REQUIRE(engine.setFrequencyDispersionControl(control.id,getFrequencyDispersionControl(settings,control.id)));}
            if(event==2){settings.bypass=true;engine.setBypass(true);}
            if(event==3){settings.bypass=false;engine.setBypass(false);}
            REQUIRE(reference.setParameters(settings));
            for(int c=0;c<a.getNumChannels();++c)for(int n=0;n<1031;++n)a.setSample(c,n,static_cast<float>(.2*std::sin((n+event*1031)*.071)*(c?-1:1)));
            b=a;REQUIRE(engine.process(juce::dsp::AudioBlock<float>(a),false));REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
            for(int c=0;c<a.getNumChannels();++c)for(int n=0;n<1031;++n)REQUIRE(a.getSample(c,n)==b.getSample(c,n));
        }
        REQUIRE(engine.getLatencySamples()==0);
        for(auto id:{"frequencyHz","mix","bypass"})REQUIRE_FALSE(engine.setFrequencyDispersionControl(id,std::numeric_limits<float>::quiet_NaN()));
        REQUIRE_FALSE(engine.setFrequencyDispersionControl("bypass",.5f));REQUIRE_FALSE(engine.setFrequencyDispersionControl("missing",0));
        auto invalid=settings;invalid.frequencyHz=0;REQUIRE_FALSE(engine.setFrequencyDispersionSettings(invalid));
    }
}

TEST_CASE("Dispersion offline render exactly reproduces isolated processing", "[frequency-dispersion][harness][render]")
{
    ExperimentRecord record;record.version=13;record.targetId="frequency-dispersion";
    record.renderDurationSamples=4096;record.blockSize=127;record.channels=1;
    record.source.kind=SourceKind::impulse;record.source.amplitude=1;
    AuditionEngine engine;REQUIRE(engine.selectTarget("frequency-dispersion"));REQUIRE(engine.setFrequencyDispersionSettings(record.dispersion));
    REQUIRE(engine.prepare({record.sampleRate,7,1}));
    juce::AudioBuffer<float> expected(1,4096);expected.clear();expected.setSample(0,0,1);
    REQUIRE(engine.process(juce::dsp::AudioBlock<float>(expected),false));
    const auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("disdorktion-dispersion",{},false);
    const auto result=renderExperiment(record,directory);REQUIRE(result.status==RenderStatus::success);REQUIRE(result.samples==4096);
    const auto metadata=juce::JSON::parse(directory.getChildFile("result.json").loadFileAsString());
    REQUIRE(metadata.getProperty("renderPolicy",juce::var()).toString()=="fixed-duration-uncompensated");
    REQUIRE_FALSE(static_cast<bool>(metadata.getProperty("tailFlushed",juce::var())));
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(directory.getChildFile("rendered.wav").createInputStream().release(),true));
    REQUIRE(reader);juce::AudioBuffer<float> rendered(1,4096);REQUIRE(reader->read(&rendered,0,4096,0,true,true));
    for(int n=0;n<4096;++n)REQUIRE(rendered.getSample(0,n)==expected.getSample(0,n));
    reader.reset();REQUIRE(directory.deleteRecursively());
}
