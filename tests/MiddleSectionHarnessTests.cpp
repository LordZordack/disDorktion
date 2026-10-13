#include "harness/AuditionEngine.h"
#include "harness/ExperimentRecord.h"
#include "harness/OfflineRender.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>

using namespace disdorktion;
using namespace disdorktion::harness;

TEST_CASE("Middle section qualification target is selectable", "[middle-section][harness]")
{
    disdorktion::harness::AuditionEngine engine;
    REQUIRE(engine.selectTarget("middle-section"));
    REQUIRE(engine.prepare({48000, 7, 2}));
    REQUIRE(engine.getLatencySamples() == 92);
}

TEST_CASE("Middle harness matches fixed candidate across controls bypasses and rates", "[middle-section][harness]")
{
    for (double rate : {44100.,48000.,88200.,96000.,192000.})
    for (unsigned channels : {1u,2u})
    for (unsigned flags=0; flags<8; ++flags)
    {
        CAPTURE(rate,channels,flags);
        AuditionEngine engine; MiddleSection core;
        MiddleSettings retained {{36.f,-12.f,(flags&1)!=0},{-24.f,.05f,12.f,(flags&2)!=0}};
        auto effective=retained;
        if (flags&4) effective.tanh.bypass=effective.folder.bypass=true;
        REQUIRE(engine.selectTarget("middle-section"));
        REQUIRE(engine.setMiddleSettings(retained,(flags&4)!=0));
        REQUIRE(core.setParameters(effective));
        REQUIRE(engine.prepare({rate,7,channels}));
        REQUIRE(core.prepare({rate,7,channels},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
        juce::AudioBuffer<float> a(static_cast<int>(channels),1031), b(static_cast<int>(channels),1031);
        for (unsigned event=0;event<5;++event)
        {
            if(event==1)
            {
                retained={{6.f,-3.f,(flags&2)!=0},{24.f,1.f,-6.f,(flags&1)!=0}};
                REQUIRE(engine.setMiddleTanhDriveDb(retained.tanh.driveDb)); REQUIRE(engine.setMiddleTanhOutputDb(retained.tanh.outputDb));
                REQUIRE(engine.setMiddleFolderDriveDb(retained.folder.driveDb)); REQUIRE(engine.setMiddleFolderOutputDb(retained.folder.outputDb));
                REQUIRE(engine.setMiddleThreshold(retained.folder.threshold));
                engine.setMiddleTanhBypass(retained.tanh.bypass); engine.setMiddleFolderBypass(retained.folder.bypass);
            }
            if(event==2) engine.setBypass(false);
            effective=retained;
            if(event<2 && (flags&4)) effective.tanh.bypass=effective.folder.bypass=true;
            REQUIRE(core.setParameters(effective));
            for(int c=0;c<a.getNumChannels();++c)for(int n=0;n<1031;++n)
                a.setSample(c,n,static_cast<float>(.4*std::sin((n+event*1031)*.071)*(c?-1:1)));
            b=a;
            REQUIRE(engine.process(juce::dsp::AudioBlock<float>(a),false)); REQUIRE(core.process(juce::dsp::AudioBlock<float>(b)));
            for(int c=0;c<a.getNumChannels();++c)for(int n=0;n<1031;++n) REQUIRE(a.getSample(c,n)==b.getSample(c,n));
            REQUIRE(engine.getLatencySamples()==92);
            if(event==3) { engine.reset(); core.reset(); }
        }
        REQUIRE_FALSE(engine.setMiddleThreshold(.049f)); REQUIRE_FALSE(engine.setMiddleTanhDriveDb(37.f));
        REQUIRE_FALSE(engine.setMiddleFolderOutputDb(std::numeric_limits<float>::quiet_NaN()));
    }
}

TEST_CASE("Middle record version nine validates strict transactional schemas", "[middle-section][experiment]")
{
    ExperimentRecord record;record.version=9;record.targetId="middle-section";
    record.middle={{36.f,-12.f,true},{24.f,.05f,12.f,false}};record.bypass=true;
    juce::String error; REQUIRE(record.validate(error));
    ExperimentRecord restored; REQUIRE(ExperimentRecord::fromJson(record.toJson(),restored,error));
    const auto before=juce::JSON::toString(restored.toJson());
    REQUIRE(before==juce::JSON::toString(record.toJson()));
    for(auto key:{"middleQuality","tanhDriveDb","tanhOutputDb","tanhBypass","folderDriveDb","folderOutputDb","folderThreshold","folderBypass"})
    {
        auto broken=record.toJson();broken.getDynamicObject()->removeProperty(key);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error)); REQUIRE(juce::JSON::toString(restored.toJson())==before);
    }
    for(auto key:{"gain","gainDb","mute","bitDepth","captureRateHz","mix","crush","shape","jitter","jitterSeed","gainBypass","crusherBypass","driveDb","outputDb","threshold","cutoffHz","response"})
    {
        auto broken=record.toJson();broken.getDynamicObject()->setProperty(key,0);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error)); REQUIRE(juce::JSON::toString(restored.toJson())==before);
    }
    for(auto key:{"tanhDriveDb","tanhOutputDb","folderDriveDb","folderOutputDb","folderThreshold"})
    for(double invalid:{1e20,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
    {
        auto broken=record.toJson();broken.getDynamicObject()->setProperty(key,invalid);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error)); REQUIRE(juce::JSON::toString(restored.toJson())==before);
    }
    for(auto pair:{std::pair{"tanhDriveDb",36.0000001},std::pair{"folderDriveDb",-24.0000001},std::pair{"tanhOutputDb",12.0000001},std::pair{"folderOutputDb",-60.0000001},std::pair{"folderThreshold",.0499999999}})
    {
        auto broken=record.toJson();broken.getDynamicObject()->setProperty(pair.first,pair.second);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(auto key:{"middleQuality","targetId"})
    {
        auto broken=record.toJson();broken.getDynamicObject()->setProperty(key,"unsupported");
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(auto key:{"tanhBypass","folderBypass"})
    {
        auto broken=record.toJson();broken.getDynamicObject()->setProperty(key,1);
        REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
    }
    for(int version=1;version<=8;++version)
    {
        ExperimentRecord old;old.version=version;
        const char* ids[]={"reference-gain","gain","bit-crusher","bit-crusher","tanh-saturation","wave-folder","high-pass","input-section"};
        old.targetId=ids[version-1]; REQUIRE(old.validate(error));
        REQUIRE(ExperimentRecord::fromJson(old.toJson(),restored,error));
        REQUIRE(restored.middle.tanh.driveDb==0.f); REQUIRE(restored.middle.folder.threshold==1.f);
        for(auto key:{"middleQuality","tanhDriveDb","tanhOutputDb","tanhBypass","folderDriveDb","folderOutputDb","folderThreshold","folderBypass"})
        {
            auto broken=old.toJson();broken.getDynamicObject()->setProperty(key,record.toJson()[key]);
            REQUIRE_FALSE(ExperimentRecord::fromJson(broken,restored,error));
        }
    }
}

TEST_CASE("Middle offline output retains fixed duration latency and matches direct candidate", "[middle-section][render]")
{
    const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("disdorktion-middle-"+juce::Uuid().toString());
    REQUIRE(root.createDirectory().wasOk());
    struct Cleanup { juce::File path; ~Cleanup(){path.deleteRecursively();} } cleanup{root};
    const auto read=[](const juce::File& file)
    {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(file.createInputStream().release(),true));
        REQUIRE(reader!=nullptr); REQUIRE(reader->lengthInSamples==1031); REQUIRE(reader->usesFloatingPointData);
        juce::AudioBuffer<float> result(2,1031); REQUIRE(reader->read(&result,0,1031,0,true,true));return result;
    };
    for(unsigned flags=0;flags<8;++flags)
    {
        ExperimentRecord record;record.version=9;record.targetId="middle-section";record.blockSize=7;
        record.middle={{12.f,-6.f,(flags&1)!=0},{24.f,.125f,-12.f,(flags&2)!=0}};record.bypass=(flags&4)!=0;
        record.renderDurationSamples=record.source.durationSamples=1031;
        const auto a=root.getChildFile(juce::String(flags)+"a"),b=root.getChildFile(juce::String(flags)+"b");
        const auto rendered=renderExperiment(record,a);REQUIRE(rendered.status==RenderStatus::success);REQUIRE(rendered.samples==1031);REQUIRE(rendered.latencySamples==92);
        const auto json=juce::JSON::parse(a.getChildFile("result.json").loadFileAsString());
        REQUIRE(json["renderPolicy"].toString()=="fixed-duration-uncompensated");REQUIRE(json["tailFlushed"].isBool());REQUIRE_FALSE(static_cast<bool>(json["tailFlushed"]));
        REQUIRE(json["middleQuality"].toString()==record.middleQuality);
        auto expected=read(a.getChildFile("source.wav")),actual=read(a.getChildFile("rendered.wav"));
        MiddleSection core;auto settings=record.middle;if(record.bypass)settings.tanh.bypass=settings.folder.bypass=true;
        REQUIRE(core.setParameters(settings));REQUIRE(core.prepare({48000,31,2},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
        REQUIRE(core.process(juce::dsp::AudioBlock<float>(expected)));
        record.monitorDb=0.f;record.muted=false;record.blockSize=127;
        REQUIRE(renderExperiment(record,b).status==RenderStatus::success);auto monitored=read(b.getChildFile("rendered.wav"));
        for(int c=0;c<2;++c)for(int n=0;n<1031;++n)
        { REQUIRE(actual.getSample(c,n)==expected.getSample(c,n));REQUIRE(actual.getSample(c,n)==monitored.getSample(c,n)); }
    }
}

TEST_CASE("Middle and isolated stage controls retain independent state across selection", "[middle-section][harness]")
{
    AuditionEngine engine;
    MiddleSettings middle {{12.f,-6.f,true},{24.f,.125f,-12.f,false}};
    REQUIRE(engine.selectTarget("middle-section"));
    REQUIRE(engine.setMiddleSettings(middle,true));
    REQUIRE(engine.prepare({48000.,7,2}));
    juce::AudioBuffer<float> actual(2,1031), expected(2,1031);
    const auto fill=[&]
    {
        for(int c=0;c<2;++c)for(int n=0;n<1031;++n)
            actual.setSample(c,n,static_cast<float>(.4*std::sin(n*.071)*(c?-1:1)));
        expected=actual;
    };
    const auto compare=[&]
    { for(int c=0;c<2;++c)for(int n=0;n<1031;++n)REQUIRE(actual.getSample(c,n)==expected.getSample(c,n)); };
    const TanhSaturationSettings isolatedTanh {-24.f,12.f,false};
    const WaveFolderSettings isolatedFolder {36.f,.05f,-3.f,true};
    REQUIRE(engine.selectTarget("tanh-saturation")); REQUIRE(engine.setTanhSaturationSettings(isolatedTanh));engine.reset();
    TanhSaturation tanh;REQUIRE(tanh.setParameters(isolatedTanh));REQUIRE(tanh.prepare({48000.,7,2}));
    fill();REQUIRE(engine.process(juce::dsp::AudioBlock<float>(actual),false));REQUIRE(tanh.process(juce::dsp::AudioBlock<float>(expected)));compare();
    REQUIRE(engine.selectTarget("wave-folder")); REQUIRE(engine.setWaveFolderSettings(isolatedFolder));engine.reset();
    WaveFolder folder;REQUIRE(folder.setParameters(isolatedFolder));REQUIRE(folder.prepare({48000.,7,2}));
    fill();REQUIRE(engine.process(juce::dsp::AudioBlock<float>(actual),false));REQUIRE(folder.process(juce::dsp::AudioBlock<float>(expected)));compare();
    REQUIRE(engine.selectTarget("middle-section"));
    MiddleSection core;auto effective=middle;effective.tanh.bypass=effective.folder.bypass=true;
    REQUIRE(core.setParameters(effective));REQUIRE(core.prepare({48000.,7,2},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
    fill();REQUIRE(engine.process(juce::dsp::AudioBlock<float>(actual),false));REQUIRE(core.process(juce::dsp::AudioBlock<float>(expected)));compare();
    engine.setBypass(false);REQUIRE(core.setParameters(middle));
    fill();REQUIRE(engine.process(juce::dsp::AudioBlock<float>(actual),false));REQUIRE(core.process(juce::dsp::AudioBlock<float>(expected)));compare();
    REQUIRE(engine.getLatencySamples()==92);
    // Editing the assembled stages likewise leaves each isolated target intact.
    REQUIRE(engine.setMiddleTanhDriveDb(36.f));REQUIRE(engine.setMiddleFolderDriveDb(-24.f));
    engine.setMiddleTanhBypass(false);engine.setMiddleFolderBypass(true);
    REQUIRE(engine.selectTarget("tanh-saturation"));tanh.reset();
    fill();REQUIRE(engine.process(juce::dsp::AudioBlock<float>(actual),false));REQUIRE(tanh.process(juce::dsp::AudioBlock<float>(expected)));compare();
    REQUIRE(engine.selectTarget("wave-folder"));folder.reset();
    fill();REQUIRE(engine.process(juce::dsp::AudioBlock<float>(actual),false));REQUIRE(folder.process(juce::dsp::AudioBlock<float>(expected)));compare();
    middle.tanh.driveDb=36.f;middle.folder.driveDb=-24.f;middle.tanh.bypass=false;middle.folder.bypass=true;
    REQUIRE(engine.selectTarget("middle-section"));REQUIRE(core.setParameters(middle));core.reset();
    fill();REQUIRE(engine.process(juce::dsp::AudioBlock<float>(actual),false));REQUIRE(core.process(juce::dsp::AudioBlock<float>(expected)));compare();
}
