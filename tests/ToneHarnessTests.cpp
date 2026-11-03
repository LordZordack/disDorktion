#include "harness/AuditionEngine.h"
#include "harness/ExperimentRecord.h"
#include <catch2/catch_test_macros.hpp>
using namespace disdorktion;
using namespace disdorktion::harness;
TEST_CASE("Tone records round trip and reject foreign fields", "[tone][harness]")
{
    ExperimentRecord r;r.version=14;r.targetId="tone";r.tone={-6.f,true};
    juce::String error;REQUIRE(r.validate(error));auto json=r.toJson();ExperimentRecord restored;
    REQUIRE(ExperimentRecord::fromJson(json,restored,error));
    REQUIRE(juce::JSON::toString(json)==juce::JSON::toString(restored.toJson()));
    for(auto key:{"toneDb","bypass"})
    {auto broken=json.clone();broken.getDynamicObject()->removeProperty(key);REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));}
    for(auto key:{"gain","mix","dispersionMix","nonsense"})
    {auto broken=json.clone();broken.getDynamicObject()->setProperty(key,0);REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));}
    auto broken=json.clone();broken.getDynamicObject()->setProperty("toneDb",6.00000001);REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    broken=ExperimentRecord{}.toJson();broken.getDynamicObject()->setProperty("toneDb",0);REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
}
TEST_CASE("Tone harness transport matches isolated processing", "[tone][harness]")
{
    AuditionEngine engine;Tone reference;REQUIRE(engine.selectTarget("tone"));
    REQUIRE(engine.setToneSettings({6,false}));REQUIRE(reference.setParameters({6,false}));
    REQUIRE(engine.prepare({48000,32,2}));REQUIRE(reference.prepare({48000,32,2}));
    juce::AudioBuffer<float> a(2,2048),b;a.clear();a.setSample(0,0,1);a.setSample(1,0,-.5f);b=a;
    REQUIRE(engine.process(juce::dsp::AudioBlock<float>(a),false));REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
    for(int c=0;c<2;++c)for(int n=0;n<2048;++n)REQUIRE(a.getSample(c,n)==b.getSample(c,n));
    REQUIRE(engine.setToneControl("toneDb",-6));REQUIRE_FALSE(engine.setToneControl("bypass",.5f));
    REQUIRE_FALSE(engine.setToneControl("unknown",0));REQUIRE(engine.getLatencySamples()==0);
}
