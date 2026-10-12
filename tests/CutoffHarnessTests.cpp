#include "harness/AuditionEngine.h"
#include "harness/OfflineRender.h"
#include "harness/ExperimentRecord.h"
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
#include <thread>

using namespace disdorktion;
using namespace disdorktion::harness;

TEST_CASE("Filter live atomic controls tolerate concurrent producer and callback", "[cutoff-filter][harness]")
{
    for(const char* id:{"high-pass","low-pass"})
    {
        AuditionEngine engine;REQUIRE(engine.selectTarget(id));REQUIRE(engine.prepare({48000,32,2}));
        std::atomic<bool> begin {false},producerOk {true};
        std::thread producer([&]
        {
            while(!begin.load(std::memory_order_acquire)) std::this_thread::yield();
            bool ok=true;
            for(unsigned n=0;n<10000;++n)
            {ok &= engine.setFilterCutoffHz(n%2?20.f:20000.f);ok &= engine.setFilterResponse(static_cast<FilterResponse>(n%3));engine.setBypass(n%5==0);}
            producerOk.store(ok,std::memory_order_release);
        });
        juce::AudioBuffer<float> audio(2,32);bool ok=true;begin.store(true,std::memory_order_release);
        for(unsigned n=0;n<1000;++n)
        {
            for(int c=0;c<2;++c)for(int frame=0;frame<32;++frame)audio.setSample(c,frame,.25f);
            ok &= engine.process(juce::dsp::AudioBlock<float>(audio),false);
            for(int frame=0;frame<32;++frame)ok &= std::isfinite(audio.getSample(0,frame));
        }
        producer.join();REQUIRE(ok);REQUIRE(producerOk.load(std::memory_order_acquire));
    }
}

TEST_CASE("Filter targets schema and offline renders retain response and requested cutoff", "[cutoff-filter][harness]")
{
    for(const char* id:{"high-pass","low-pass"})
    for(auto mode:{FilterResponse::butterworth,FilterResponse::chebyshev1,FilterResponse::chebyshev2})
    {
        ExperimentRecord record; record.version=7;record.targetId=id;record.cutoffHz=20000;record.response=mode;
        record.sampleRate=8000;record.source.frequency=440;record.source.frequency2=880;
        record.source.kind=SourceKind::impulse; record.renderDurationSamples=1000;record.source.durationSamples=1000;
        juce::String error; REQUIRE(record.validate(error));
        ExperimentRecord restored; REQUIRE(ExperimentRecord::fromJson(record.toJson(),restored,error));
        REQUIRE(restored.cutoffHz==20000); REQUIRE(restored.response==mode);REQUIRE(restored.targetId==id);
        for(const char* field:{"cutoffHz","response"})
        {
            auto missing=record.toJson();missing.getDynamicObject()->removeProperty(field);
            REQUIRE_FALSE(ExperimentRecord::fromJson(missing,restored,error)); REQUIRE(restored.response==mode);
        }
        for(juce::var bad:{juce::var("ideal"),juce::var(2),juce::var(false)})
        { auto invalid=record.toJson();invalid.getDynamicObject()->setProperty("response",bad);REQUIRE_FALSE(ExperimentRecord::fromJson(invalid,restored,error)); }
        for(double bad:{19.,20001.,20.-1e-7,20000.+1e-7})
        { auto invalid=record.toJson();invalid.getDynamicObject()->setProperty("cutoffHz",bad);REQUIRE_FALSE(ExperimentRecord::fromJson(invalid,restored,error)); }
        auto mixed=record.toJson();mixed.getDynamicObject()->setProperty("driveDb",0);REQUIRE_FALSE(ExperimentRecord::fromJson(mixed,restored,error));
        auto legacy=ExperimentRecord{}.toJson();legacy.getDynamicObject()->setProperty("response","butterworth");REQUIRE_FALSE(ExperimentRecord::fromJson(legacy,restored,error));
        AuditionEngine engine;REQUIRE(engine.selectTarget(id));REQUIRE(engine.setFilterSettings({20000,false,mode}));REQUIRE(engine.prepare({8000,32,2}));
        juce::AudioBuffer<float> expected(2,1000);expected.clear();expected.setSample(0,0,.25f);expected.setSample(1,0,.25f);
        REQUIRE(engine.process(juce::dsp::AudioBlock<float>(expected),false));
        const auto output=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("disdorktion-filter-"+juce::Uuid().toString());
        auto result=renderExperiment(record,output);INFO(result.message);REQUIRE(result.status==RenderStatus::success);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(output.getChildFile("rendered.wav").createInputStream().release(),true));
        REQUIRE(reader!=nullptr);juce::AudioBuffer<float> actual(2,1000);REQUIRE(reader->read(&actual,0,1000,0,true,true));
        for(int c=0;c<2;++c)for(int n=0;n<1000;++n)REQUIRE(actual.getSample(c,n)==expected.getSample(c,n));
        REQUIRE(output.deleteRecursively());
        REQUIRE(engine.getLatencySamples()==0); REQUIRE_FALSE(engine.setFilterCutoffHz(0));REQUIRE(engine.setFilterCutoffHz(20));
        REQUIRE_FALSE(engine.setFilterResponse(static_cast<FilterResponse>(99)));
        REQUIRE(engine.setFilterResponse(FilterResponse::chebyshev1));
        engine.setBypass(true);engine.reset();actual=expected;REQUIRE(engine.process(juce::dsp::AudioBlock<float>(actual),false));
        for(int n=0;n<1000;++n)REQUIRE(actual.getSample(0,n)==expected.getSample(0,n));
    }
}
