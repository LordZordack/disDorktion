#include "harness/AuditionEngine.h"
#include "harness/ExperimentRecord.h"
#include "harness/OfflineRender.h"
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <juce_audio_formats/juce_audio_formats.h>
using namespace disdorktion;
using namespace disdorktion::harness;

TEST_CASE("Output harness retains independent settings through common bypass and rates", "[output-section][harness]")
{
    for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.})
    for(unsigned channels:{1u,2u})for(unsigned flags=0;flags<8;++flags)
    {
        AuditionEngine engine; OutputSection reference;
        OutputSettings retained {{12000.f,(flags&1)!=0,FilterResponse::chebyshev1}, {300.f,(flags&2)!=0,FilterResponse::chebyshev2}};
        REQUIRE(engine.selectTarget("output-section")); REQUIRE(engine.setOutputSettings(retained,(flags&4)!=0));
        auto effective=retained;
        if(flags&4) effective.highPass.bypass=effective.lowPass.bypass=true;
        REQUIRE(reference.setParameters(effective));
        REQUIRE(engine.prepare({rate,7,channels})); REQUIRE(reference.prepare({rate,7,channels}));
        juce::AudioBuffer<float> a(static_cast<int>(channels),1031),b(static_cast<int>(channels),1031);
        for(unsigned event=0;event<4;++event)
        {
            if(event==1)
            {
                retained.highPass.cutoffHz=20; retained.lowPass.cutoffHz=20000;
                retained.highPass.response=FilterResponse::chebyshev2; retained.lowPass.response=FilterResponse::butterworth;
                retained.highPass.bypass=(flags&2)!=0; retained.lowPass.bypass=(flags&1)!=0;
                REQUIRE(engine.setOutputCutoffHz(true,20)); REQUIRE(engine.setOutputCutoffHz(false,20000));
                REQUIRE(engine.setOutputResponse(true,retained.highPass.response)); REQUIRE(engine.setOutputResponse(false,retained.lowPass.response));
                engine.setOutputStageBypass(true,retained.highPass.bypass);engine.setOutputStageBypass(false,retained.lowPass.bypass);
            }
            if(event==2) engine.setBypass(false);
            effective=retained; if(event<2 && (flags&4)) effective.highPass.bypass=effective.lowPass.bypass=true;
            REQUIRE(reference.setParameters(effective));
            for(int c=0;c<a.getNumChannels();++c)for(int n=0;n<1031;++n)a.setSample(c,n,static_cast<float>(.7*std::sin((n+event*1031)*.071)*(c?-1:1)));
            b=a; REQUIRE(engine.process(juce::dsp::AudioBlock<float>(a),false));REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
            for(int c=0;c<a.getNumChannels();++c)for(int n=0;n<1031;++n)REQUIRE(a.getSample(c,n)==b.getSample(c,n));
            REQUIRE(engine.getLatencySamples()==0); REQUIRE(engine.meters().inputPeak<=.7f);
        }
        REQUIRE_FALSE(engine.setOutputCutoffHz(true,19)); REQUIRE_FALSE(engine.setOutputCutoffHz(false,std::numeric_limits<float>::quiet_NaN()));
        REQUIRE_FALSE(engine.setOutputResponse(true,static_cast<FilterResponse>(3)));
    }
}

TEST_CASE("Output records version ten strictly validate without changing legacy schemas", "[output-section][experiment]")
{
    ExperimentRecord record;record.version=10;record.targetId="output-section";
    record.output={{12000,true,FilterResponse::chebyshev1},{300,false,FilterResponse::chebyshev2}};record.bypass=true;
    juce::String error; REQUIRE(record.validate(error));
    ExperimentRecord restored; REQUIRE(ExperimentRecord::fromJson(record.toJson(),restored,error));
    const auto before=juce::JSON::toString(restored.toJson()); REQUIRE(before==juce::JSON::toString(record.toJson()));
    for(auto key:{"hpCutoffHz","lpCutoffHz","hpResponse","lpResponse","hpBypass","lpBypass"})
    {
        auto broken=record.toJson();broken.getDynamicObject()->removeProperty(key);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));REQUIRE(juce::JSON::toString(restored.toJson())==before);
    }
    for(auto key:{"gain","gainDb","mute","cutoffHz","response","middleQuality","tanhDriveDb","folderThreshold","bitDepth","threshold","driveDb","gainBypass"})
    {
        auto broken=record.toJson();broken.getDynamicObject()->setProperty(key,0);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(auto key:{"hpCutoffHz","lpCutoffHz"})for(double invalid:{19.,20000.00001,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
    {
        auto broken=record.toJson();broken.getDynamicObject()->setProperty(key,invalid);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(auto key:{"hpResponse","lpResponse","hpBypass","lpBypass"})
    {
        auto broken=record.toJson();broken.getDynamicObject()->setProperty(key,1);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(int version=1;version<=9;++version)
    {
        ExperimentRecord old;old.version=version;
        old.targetId=version==1?"reference-gain":version==2?"gain":version<5?"bit-crusher":version==5?"tanh-saturation":version==6?"wave-folder":version==7?"high-pass":version==8?"input-section":"middle-section";
        REQUIRE(ExperimentRecord::fromJson(old.toJson(),restored,error));
        REQUIRE(restored.version==version);REQUIRE(restored.targetId==old.targetId);
        REQUIRE(juce::JSON::toString(old.toJson())==juce::JSON::toString(restored.toJson()));
        for(auto key:{"hpCutoffHz","lpCutoffHz","hpResponse","lpResponse","hpBypass","lpBypass"})
        {
            auto broken=old.toJson();broken.getDynamicObject()->setProperty(key,0);
            REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
        }
    }
}

TEST_CASE("Output target rejects malformed scalar controls and preserves module bypass", "[output-section][harness]")
{
    OutputSectionTarget target; REQUIRE(target.descriptor().id=="output-section");
    REQUIRE(target.setControl("hpCutoffHz",12000));REQUIRE(target.setControl("lpCutoffHz",300));
    REQUIRE(target.setControl("hpResponse",1));REQUIRE(target.setControl("lpResponse",2));
    for(float value:{-.1f,.5f,3.f,std::numeric_limits<float>::quiet_NaN()})REQUIRE_FALSE(target.setControl("hpResponse",value));
    REQUIRE_FALSE(target.setControl("lpBypass",.5f));REQUIRE_FALSE(target.setControl("unknown",0));
    REQUIRE(target.setControl("hpBypass",1)); target.setBypass(true);REQUIRE(target.prepare({48000,32,1}));
    juce::AudioBuffer<float> audio(1,1000);for(int n=0;n<1000;++n)audio.setSample(0,n,.5f);
    REQUIRE(target.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(audio.getSample(0,999)==.5f);
    target.setBypass(false); target.reset();
    REQUIRE(target.process(juce::dsp::AudioBlock<float>(audio)));REQUIRE(audio.getSample(0,999)>.49f);
}

TEST_CASE("Output offline render equals native ordered section with exact frame count", "[output-section][render]")
{
    ExperimentRecord record;record.version=10;record.targetId="output-section";
    record.renderDurationSamples=1031;record.blockSize=127;
    record.output={{200,false,FilterResponse::chebyshev1},{8000,false,FilterResponse::chebyshev2}};
    const auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("disdorktion-output",{},false);
    const auto result=renderExperiment(record,directory); REQUIRE(result.status==RenderStatus::success);
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(directory.getChildFile("rendered.wav").createInputStream().release(),true));
    REQUIRE(reader);REQUIRE(reader->lengthInSamples==1031);
    juce::AudioBuffer<float> rendered(2,1031),expected(2,1031); REQUIRE(reader->read(&rendered,0,1031,0,true,true));
    SignalSource source;REQUIRE(source.prepare(record.source,48000,2));
    source.process(juce::dsp::AudioBlock<float>(expected));
    OutputSection section;REQUIRE(section.setParameters(record.output));REQUIRE(section.prepare({48000,127,2}));
    REQUIRE(section.process(juce::dsp::AudioBlock<float>(expected)));
    for(int c=0;c<2;++c)for(int n=0;n<1031;++n) REQUIRE(rendered.getSample(c,n)==expected.getSample(c,n));
    reader.reset();REQUIRE(directory.deleteRecursively());
}
