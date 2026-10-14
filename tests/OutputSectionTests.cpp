#include "dsp/OutputSection.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
using namespace disdorktion;

TEST_CASE("Output section dynamic order has a discriminating inverted control", "[output-section]")
{
    OutputSection section;
    HighPass hp, reversedHp;
    LowPass lp, reversedLp;
    OutputSettings settings {{200.f,false,FilterResponse::chebyshev1}, {8000.f,false,FilterResponse::butterworth}};
    REQUIRE(section.setParameters(settings));
    REQUIRE(hp.setParameters(settings.highPass)); REQUIRE(lp.setParameters(settings.lowPass));
    REQUIRE(reversedHp.setParameters(settings.highPass)); REQUIRE(reversedLp.setParameters(settings.lowPass));
    for (auto* f : {&hp, &reversedHp}) REQUIRE(f->prepare({48000,32,2}));
    for (auto* f : {&lp, &reversedLp}) REQUIRE(f->prepare({48000,32,2}));
    REQUIRE(section.prepare({48000,32,2}));
    juce::AudioBuffer<float> a(2,127), b(2,127), r(2,127);
    double error=0., discrimination=0.;
    for (unsigned event=0;event<30;++event)
    {
        if (event%3==0)
        {
            settings.highPass={event%2?20.f:12000.f,event%5==0,static_cast<FilterResponse>(event%3)};
            settings.lowPass={event%2?18000.f:300.f,event%7==0,static_cast<FilterResponse>((event+1)%3)};
            REQUIRE(section.setParameters(settings));
            REQUIRE(hp.setParameters(settings.highPass)); REQUIRE(lp.setParameters(settings.lowPass));
            REQUIRE(reversedHp.setParameters(settings.highPass)); REQUIRE(reversedLp.setParameters(settings.lowPass));
        }
        for(int c=0;c<2;++c) for(int n=0;n<127;++n)
            a.setSample(c,n,static_cast<float>((c?-1:1)*(.6*std::sin((event*127+n)*.37)+.3*std::sin((event*127+n)*.017))));
        b=a; r=a;
        REQUIRE(section.process(juce::dsp::AudioBlock<float>(a)));
        REQUIRE(hp.process(juce::dsp::AudioBlock<float>(b))); REQUIRE(lp.process(juce::dsp::AudioBlock<float>(b)));
        REQUIRE(reversedLp.process(juce::dsp::AudioBlock<float>(r))); REQUIRE(reversedHp.process(juce::dsp::AudioBlock<float>(r)));
        for(int c=0;c<2;++c) for(int n=0;n<127;++n)
        {
            error=std::max(error,std::abs(static_cast<double>(a.getSample(c,n))-b.getSample(c,n)));
            discrimination=std::max(discrimination,std::abs(static_cast<double>(r.getSample(c,n))-b.getSample(c,n)));
        }
    }
    REQUIRE(error<=1e-6); REQUIRE(discrimination>1e-3); REQUIRE(section.getLatencySamples()==0);
}

TEST_CASE("Output section rejects calls transactionally and preserves channel history", "[output-section]")
{
    OutputSection subject, reference;
    juce::AudioBuffer<float> a(2,1031), b(2,1031), wrong(1,8);
    a.clear(); a.setSample(0,0,1.f); b=a;
    REQUIRE_FALSE(subject.process(juce::dsp::AudioBlock<float>(a)));
    REQUIRE(a.getSample(0,0)==1.f);
    for (auto* s : {&subject,&reference}) REQUIRE(s->prepare({48000,7,2}));
    REQUIRE(subject.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
    REQUIRE(subject.getPostHighPassPeak()>0.f);
    const auto peak=subject.getPostHighPassPeak();
    REQUIRE(subject.process(juce::dsp::AudioBlock<float>(a).getSubBlock(0,0)));
    REQUIRE(subject.getPostHighPassPeak()==peak);
    REQUIRE_FALSE(subject.prepare({7999,7,2}));
    OutputSettings invalid; invalid.highPass.cutoffHz=4000; invalid.lowPass.cutoffHz=19;
    REQUIRE_FALSE(subject.setParameters(invalid));
    invalid.lowPass.cutoffHz=std::numeric_limits<float>::quiet_NaN(); REQUIRE_FALSE(subject.setParameters(invalid));
    wrong.clear(); wrong.setSample(0,0,.5f); REQUIRE_FALSE(subject.process(juce::dsp::AudioBlock<float>(wrong)));
    REQUIRE(wrong.getSample(0,0)==.5f);
    a.clear(); b.clear(); REQUIRE(subject.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
    for(int c=0;c<2;++c)for(int n=0;n<1031;++n) REQUIRE(a.getSample(c,n)==b.getSample(c,n));
    REQUIRE(a.getMagnitude(1,0,1031)==0.f);
    subject.reset(); a.clear(); REQUIRE(subject.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(a.getMagnitude(0,1031)==0.f);
}

TEST_CASE("Output section partition reset reconfiguration and bypass match modules", "[output-section]")
{
    for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.})
    for(unsigned channels:{1u,2u})for(unsigned flags=0;flags<4;++flags)
    {
        OutputSection section, partition;
        OutputSettings settings {{20000.f,(flags&1)!=0,FilterResponse::chebyshev2},{20.f,(flags&2)!=0,FilterResponse::chebyshev1}};
        REQUIRE(section.setParameters(settings)); REQUIRE(partition.setParameters(settings));
        REQUIRE(section.prepare({rate,32,channels})); REQUIRE(partition.prepare({rate,7,channels}));
        REQUIRE(section.getEffectiveHighPassCutoffHz()==std::min(20000.,rate*.45));
        REQUIRE(section.getEffectiveLowPassCutoffHz()==20.);
        juce::AudioBuffer<float> a(static_cast<int>(channels),1031), b(static_cast<int>(channels),1031);
        for(int c=0;c<a.getNumChannels();++c)for(int n=0;n<1031;++n)a.setSample(c,n,static_cast<float>(.5*std::sin(n*.11)*(c?-1:1)));
        b=a; REQUIRE(section.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(partition.process(juce::dsp::AudioBlock<float>(b)));
        for(int c=0;c<a.getNumChannels();++c)for(int n=0;n<1031;++n)REQUIRE(a.getSample(c,n)==b.getSample(c,n));
        settings.highPass.bypass=settings.lowPass.bypass=true;
        REQUIRE(section.setParameters(settings));section.reset(); b=a;
        REQUIRE(section.process(juce::dsp::AudioBlock<float>(a)));
        for(int c=0;c<a.getNumChannels();++c)for(int n=0;n<1031;++n)REQUIRE(a.getSample(c,n)==b.getSample(c,n));
        REQUIRE(section.prepare({48000,1,channels})); REQUIRE(section.getEffectiveHighPassCutoffHz()==20000.);
    }
}
