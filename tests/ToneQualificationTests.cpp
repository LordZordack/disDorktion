#include "dsp/Tone.h"
#include "harness/OfflineRender.h"
#include "harness/ToneTarget.h"
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>
using namespace disdorktion;
TEST_CASE("Tone exact neutral bypass and reset retain warm independent histories", "[tone]")
{
    for(double fs:{8000.,44100.,48000.,88200.,96000.,192000.,384000.})
    for(unsigned capacity:{1u,32u,64u,127u,256u,1024u})
    {
        Tone tone,reference;REQUIRE(tone.setParameters({6,false}));REQUIRE(tone.prepare({fs,capacity,2}));
        REQUIRE(reference.setParameters({6,false}));REQUIRE(reference.prepare({fs,capacity,2}));
        const int ramp=static_cast<int>(fs*.010);
        juce::AudioBuffer<float> b(2,ramp+17),expected;b.clear();b.setSample(0,0,1);expected=b;
        REQUIRE(tone.process(juce::dsp::AudioBlock<float>(b)));
        for(int n=0;n<b.getNumSamples();++n)REQUIRE(b.getSample(1,n)==0);
        tone.reset();REQUIRE(reference.process(juce::dsp::AudioBlock<float>(expected)));
        b.clear();b.setSample(0,0,1);REQUIRE(tone.process(juce::dsp::AudioBlock<float>(b)));
        for(int n=0;n<b.getNumSamples();++n)REQUIRE(b.getSample(0,n)==expected.getSample(0,n));
        for(const auto settings:{ToneSettings{0,false},ToneSettings{-6,true}})
        {
            REQUIRE(tone.setParameters(settings));
            for(int n=0;n<b.getNumSamples();++n){b.setSample(0,n,float(std::sin(n*.13)));b.setSample(1,n,-.3f);}
            expected=b;REQUIRE(tone.process(juce::dsp::AudioBlock<float>(b)));
            for(int c=0;c<2;++c)for(int n=ramp-1;n<b.getNumSamples();++n)REQUIRE(b.getSample(c,n)==expected.getSample(c,n));
        }
    }
}
TEST_CASE("Tone offline records reproduce isolated samples", "[tone][harness][render]")
{
    using namespace harness;
    ExperimentRecord record;record.version=14;record.targetId="tone";record.tone={6,false};
    record.renderDurationSamples=4096;record.blockSize=127;record.channels=1;
    record.source.kind=SourceKind::impulse;record.source.amplitude=1;
    Tone reference;REQUIRE(reference.setParameters(record.tone));REQUIRE(reference.prepare({48000,7,1}));
    juce::AudioBuffer<float> expected(1,4096);expected.clear();expected.setSample(0,0,1);
    REQUIRE(reference.process(juce::dsp::AudioBlock<float>(expected)));
    const auto dir=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("tone-offline",{},false);
    const auto result=renderExperiment(record,dir);REQUIRE(result.status==RenderStatus::success);
    juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(dir.getChildFile("rendered.wav").createInputStream().release(),true));
    REQUIRE(reader);juce::AudioBuffer<float> actual(1,4096);REQUIRE(reader->read(&actual,0,4096,0,true,true));
    for(int n=0;n<4096;++n)REQUIRE(actual.getSample(0,n)==expected.getSample(0,n));
    REQUIRE(dir.deleteRecursively());
}
