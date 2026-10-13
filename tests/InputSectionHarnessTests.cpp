#include "harness/AuditionEngine.h"
#include "harness/ExperimentRecord.h"
#include "harness/OfflineRender.h"
#include "dsp/InputSection.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace disdorktion;
using namespace disdorktion::harness;

TEST_CASE("Input section harness retains independent bypasses under common bypass", "[input-section][harness]")
{
    AuditionEngine engine;
    REQUIRE(engine.selectTarget("input-section"));
    REQUIRE(engine.prepare({48000, 7, 2}));
    InputSettings settings {{24.f, true, false}, {2, 100.f, true}};
    REQUIRE(engine.setInputSettings(settings));
    engine.setBypass(true);
    engine.reset();
    juce::AudioBuffer<float> audio(2, 31);
    for (int c=0;c<2;++c) for (int n=0;n<31;++n) audio.setSample(c,n,c ? -.25f : .5f);
    REQUIRE(engine.process(juce::dsp::AudioBlock<float>(audio), false));
    REQUIRE(audio.getSample(0,0)==.5f);
    REQUIRE(audio.getSample(1,30)==-.25f);
    engine.setBypass(false);
    engine.reset();
    REQUIRE(engine.process(juce::dsp::AudioBlock<float>(audio), false));
    REQUIRE(audio.getMagnitude(0,31)==0.f); // Original gain mute and crusher bypass restored.
}

TEST_CASE("Input section harness copies core output and latches hidden interstage overload", "[input-section][harness]")
{
    AuditionEngine engine;
    InputSection core;
    InputSettings settings {{24.f, false, false}, {4, 12345.5f, false, 1.f, .7f, -.3f, .8f, 42}};
    REQUIRE(engine.selectTarget("input-section"));
    REQUIRE(engine.setInputSettings(settings));
    REQUIRE(core.setParameters(settings));
    REQUIRE(engine.prepare({48000, 7, 2}));
    REQUIRE(core.prepare({48000, 7, 2}));
    juce::AudioBuffer<float> a(2, 137), b(2, 137);
    for(int c=0;c<2;++c) for(int n=0;n<137;++n) a.setSample(c,n,(c ? -1.f : 1.f)*.5f);
    b=a;
    REQUIRE(engine.process(juce::dsp::AudioBlock<float>(a),false));
    REQUIRE(core.process(juce::dsp::AudioBlock<float>(b)));
    for(int c=0;c<2;++c) for(int n=0;n<137;++n) REQUIRE(a.getSample(c,n)==b.getSample(c,n));
    REQUIRE(engine.meters().postGainPeak==core.getPostGainPeak());
    REQUIRE(engine.meters().postGainPeak>7.f);
    REQUIRE(engine.meters().outputPeak<=1.f);
    a.clear();
    REQUIRE(engine.process(juce::dsp::AudioBlock<float>(a),false));
    REQUIRE(engine.meters().postGainPeak==0.f);
    REQUIRE(engine.consumeOverload());
    REQUIRE_FALSE(engine.consumeOverload());
    engine.reset();
    REQUIRE(engine.meters().postGainPeak==0.f);
}

TEST_CASE("Input section experiment version eight is strict and transactional", "[input-section][experiment]")
{
    ExperimentRecord record;
    record.version=8; record.targetId="input-section";
    record.gainDb=12.f; record.mute=true; record.gainBypass=true;
    record.bitDepth=4; record.captureRateHz=12345.5f;
    record.mix=.25f; record.crush=.5f; record.shape=-.2f; record.jitter=.8f;
    record.jitterSeed=4294967295u; record.crusherBypass=false; record.bypass=true;
    juce::String error;
    REQUIRE(record.validate(error));
    ExperimentRecord restored;
    REQUIRE(ExperimentRecord::fromJson(record.toJson(),restored,error));
    REQUIRE(juce::JSON::toString(restored.toJson())==juce::JSON::toString(record.toJson()));
    const auto before=juce::JSON::toString(restored.toJson());
    for (const auto key : {"gainDb","mute","gainBypass","crusherBypass","bitDepth","captureRateHz","mix","crush","shape","jitter","jitterSeed"})
    {
        auto broken=record.toJson(); broken.getDynamicObject()->removeProperty(key);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
        REQUIRE(juce::JSON::toString(restored.toJson())==before);
    }
    for(int version=1;version<8;++version)
    {
        auto broken=record.toJson(); broken.getDynamicObject()->setProperty("version",version);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(const auto id : {"gain","bit-crusher","high-pass","reference-gain"})
    {
        auto broken=record.toJson(); broken.getDynamicObject()->setProperty("targetId",id);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for (const auto key : {"driveDb","outputDb","threshold","cutoffHz","gain"})
    {
        auto broken=record.toJson(); broken.getDynamicObject()->setProperty(key,0.f);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for (const auto key : {"gainBypass","crusherBypass","mute"})
    {
        auto broken=record.toJson(); broken.getDynamicObject()->setProperty(key,1);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for (auto key : {"gainDb","mix","shape","captureRateHz","jitterSeed"})
    {
        auto broken=record.toJson(); broken.getDynamicObject()->setProperty(key,1.0e20);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
}

TEST_CASE("Input section offline render reports overload before wet clamping", "[input-section][render]")
{
    ExperimentRecord record;
    record.version=8;record.targetId="input-section";
    record.gainDb=24.f;record.bitDepth=4;record.blockSize=7;
    record.renderDurationSamples=137;record.source.durationSamples=137;
    record.source.amplitude=.5;
    const auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("disdorktion-input-test-"+juce::Uuid().toString());
    const auto rendered=renderExperiment(record,directory);
    REQUIRE(rendered.status==RenderStatus::success);
    REQUIRE(rendered.postGainPeak>1.f);
    REQUIRE(rendered.outputPeak<=1.);
    REQUIRE(rendered.overloaded);
    REQUIRE(rendered.latencySamples==0);
    REQUIRE(rendered.samples==137);
    const auto result=juce::JSON::parse(directory.getChildFile("result.json").loadFileAsString());
    REQUIRE(static_cast<double>(result["postGainPeak"])==rendered.postGainPeak);
    REQUIRE(directory.deleteRecursively());
}

TEST_CASE("Input section rendered samples match core across bypass states and monitoring", "[input-section][render]")
{
    const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("disdorktion-input-oracle-"+juce::Uuid().toString());
    REQUIRE(root.createDirectory().wasOk());
    struct Cleanup { juce::File path; ~Cleanup() { path.deleteRecursively(); } } cleanup {root};
    const auto read=[](const juce::File& file)
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(file.createInputStream().release(),true));
        REQUIRE(reader!=nullptr); REQUIRE(reader->usesFloatingPointData);
        REQUIRE(reader->bitsPerSample==32); REQUIRE(reader->lengthInSamples==1031);
        juce::AudioBuffer<float> samples(2,1031);
        REQUIRE(reader->read(&samples,0,1031,0,true,true));
        return samples;
    };
    for(unsigned flags=0;flags<8;++flags)
    {
        CAPTURE(flags);
        ExperimentRecord record;
        record.version=8;record.targetId="input-section";
        record.gainDb=12.f;record.bitDepth=3;record.captureRateHz=12345.5f;
        record.mix=.7f;record.crush=.6f;record.shape=-.4f;record.jitter=.8f;record.jitterSeed=42;
        record.gainBypass=(flags&1)!=0;record.crusherBypass=(flags&2)!=0;record.bypass=(flags&4)!=0;
        record.renderDurationSamples=1031;record.source.durationSamples=1031;record.blockSize=7;
        const auto a=root.getChildFile(juce::String(flags)+"a"), b=root.getChildFile(juce::String(flags)+"b");
        REQUIRE(renderExperiment(record,a).status==RenderStatus::success);
        auto source=read(a.getChildFile("source.wav"));
        auto actual=read(a.getChildFile("rendered.wav"));
        InputSection core;
        REQUIRE(core.setParameters({{record.gainDb,record.mute,record.gainBypass||record.bypass},
            {record.bitDepth,record.captureRateHz,record.crusherBypass||record.bypass,record.mix,
             record.crush,record.shape,record.jitter,record.jitterSeed}}));
        REQUIRE(core.prepare({record.sampleRate,31,2}));
        REQUIRE(core.process(juce::dsp::AudioBlock<float>(source)));
        record.monitorDb=0.f;record.muted=false;record.blockSize=127;
        REQUIRE(renderExperiment(record,b).status==RenderStatus::success);
        auto monitored=read(b.getChildFile("rendered.wav"));
        for(int c=0;c<2;++c) for(int n=0;n<1031;++n)
        {
            REQUIRE(actual.getSample(c,n)==source.getSample(c,n));
            REQUIRE(actual.getSample(c,n)==monitored.getSample(c,n));
        }
    }
}

TEST_CASE("Input section live child edits survive common override release", "[input-section][harness]")
{
    for(unsigned flags=0;flags<4;++flags)
    {
        CAPTURE(flags);
        AuditionEngine engine; InputSection reference;
        InputSettings settings {{12.f,false,false},{4,12345.5f,false,.7f,.6f,-.4f,.8f,42}};
        REQUIRE(engine.selectTarget("input-section"));
        REQUIRE(engine.setInputSettings(settings,true));
        auto effective=settings; effective.gain.bypass=effective.crusher.bypass=true;
        REQUIRE(reference.setParameters(effective));
        REQUIRE(engine.prepare({48000,7,2})); REQUIRE(reference.prepare({48000,7,2}));
        juce::AudioBuffer<float> a(2,137), b(2,137);
        for(unsigned event=0;event<5;++event)
        {
            if(event==1)
            {
                settings.gain.bypass=(flags&1)!=0;settings.crusher.bypass=(flags&2)!=0;
                engine.setInputGainBypass(settings.gain.bypass);engine.setInputCrusherBypass(settings.crusher.bypass);
            }
            if(event==2) engine.setBypass(false);
            effective=settings;
            if(event<2) effective.gain.bypass=effective.crusher.bypass=true;
            REQUIRE(reference.setParameters(effective));
            for(int c=0;c<2;++c) for(int n=0;n<137;++n)
                a.setSample(c,n,static_cast<float>(.4*std::sin((n+event*137)*.071)*(c ? -1 : 1)));
            b=a;
            REQUIRE(engine.process(juce::dsp::AudioBlock<float>(a),false));
            REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
            for(int c=0;c<2;++c) for(int n=0;n<137;++n) REQUIRE(a.getSample(c,n)==b.getSample(c,n));
        }
    }
}
