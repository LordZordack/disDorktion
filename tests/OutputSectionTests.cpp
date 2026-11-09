#include "dsp/OutputSection.h"
#include "dsp/ResonantComb.h"
#include "dsp/FrequencyDispersion.h"
#include "dsp/Tone.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include <limits>
using namespace disdorktion;

TEST_CASE("Output active extreme resonance remains finite and decays after excitation", "[output-section][qualification]")
{
    // This qualifies the driven cascade empirically; it does not extend the
    // individual dispersion stage's conservative analytical input bound.
    for(double rate:{8000.,48000.})for(float feedback:{-.95f,.95f})for(float db:{-6.f,6.f})for(bool damped:{false,true})
    {
        CAPTURE(rate,feedback,db,damped);
        OutputSettings s; s.comb={40.f,1.f,feedback,20.f,1.f,damped,false};
        s.dispersion={20.f,1.f,false}; s.tone={db,false};
        s.highPass={20.f,false,FilterResponse::chebyshev1};
        s.lowPass={20000.f,false,FilterResponse::chebyshev2};
        OutputSection section; REQUIRE(section.setParameters(s)); REQUIRE(section.prepare({rate,127,2}));
        juce::AudioBuffer<float> a(2,257); double peak=0.; float finalPeak=0.f;
        const auto driveFrames=static_cast<unsigned>(rate);
        // The comb's standalone unit-impulse bound does not include the larger
        // state produced by a second of resonant drive or downstream histories.
        // Qualify the production host's static 20-second decay allowance.
        const auto totalFrames=static_cast<unsigned>(rate*21.);
        const double frequency=feedback>0.f?25.:12.5;
        for(unsigned frame=0;frame<totalFrames;frame+=257)
        {
            const auto count=std::min(257u,totalFrames-frame);
            a.clear();
            for(unsigned n=0;n<count;++n)if(frame+n<driveFrames)
                a.setSample(0,int(n),float(std::sin(6.283185307179586*frequency*(frame+n)/rate)));
            REQUIRE(section.process(juce::dsp::AudioBlock<float>(a).getSubBlock(0,count)));
            for(unsigned n=0;n<count;++n)
            {
                const auto value=a.getSample(0,int(n)); REQUIRE(std::isfinite(value));
                REQUIRE(std::fpclassify(value)!=FP_SUBNORMAL); peak=std::max(peak,std::abs(double(value)));
                REQUIRE(a.getSample(1,int(n))==0.f);
            }
            finalPeak=a.getMagnitude(0,0,int(count));
        }
        std::printf("Output resonance: rate=%.0f feedback=%.2f tone=%.0f damped=%d peak=%.9g tailAfter20s=%.9g\n",
            rate,double(feedback),double(db),int(damped),peak,double(finalPeak));
        REQUIRE(peak>0.01); REQUIRE(peak<512.); REQUIRE(finalPeak<1e-6f);
    }
}

TEST_CASE("Output expanded defaults preserve legacy HP LP aggregate and exact baseline", "[output-section]")
{
    OutputSettings s {{250.f,false,FilterResponse::chebyshev1},{5000.f,false,FilterResponse::chebyshev2}};
    REQUIRE(s.comb.bypass); REQUIRE(s.dispersion.bypass); REQUIRE(s.tone.bypass);
    OutputSection section; HighPass hp; LowPass lp;
    REQUIRE(section.setParameters(s)); REQUIRE(hp.setParameters(s.highPass)); REQUIRE(lp.setParameters(s.lowPass));
    REQUIRE(section.prepare({48000,7,2})); REQUIRE(hp.prepare({48000,7,2})); REQUIRE(lp.prepare({48000,7,2}));
    juce::AudioBuffer<float> a(2,1031);
    for(int c=0;c<2;++c)for(int n=0;n<1031;++n)a.setSample(c,n,float((c?-.6:1.)*std::sin(n*.37)));
    auto b=a; REQUIRE(section.process(juce::dsp::AudioBlock<float>(a)));
    REQUIRE(hp.process(juce::dsp::AudioBlock<float>(b))); REQUIRE(lp.process(juce::dsp::AudioBlock<float>(b)));
    for(int c=0;c<2;++c)for(int n=0;n<1031;++n)REQUIRE(a.getSample(c,n)==b.getSample(c,n));
}

TEST_CASE("Output construction bypasses new stages before any settings call", "[output-section]")
{
    OutputSection section; HighPass hp; LowPass lp;
    REQUIRE(section.prepare({48000,7,1})); REQUIRE(hp.prepare({48000,7,1})); REQUIRE(lp.prepare({48000,7,1}));
    juce::AudioBuffer<float> a(1,1031); a.clear(); a.setSample(0,0,1.f); auto b=a;
    REQUIRE(section.process(juce::dsp::AudioBlock<float>(a)));
    REQUIRE(hp.process(juce::dsp::AudioBlock<float>(b))); REQUIRE(lp.process(juce::dsp::AudioBlock<float>(b)));
    for(int n=0;n<1031;++n) REQUIRE(a.getSample(0,n)==b.getSample(0,n));
}

TEST_CASE("Output expanded stages follow dynamic native rate module composition", "[output-section]")
{
    OutputSection section; ResonantComb comb, reversedComb; FrequencyDispersion dispersion, reversedDispersion;
    Tone tone, reversedTone; HighPass hp, reversedHp; LowPass lp, reversedLp;
    OutputSettings s;
    REQUIRE(section.prepare({48000,7,2}));
    for(auto* f:{&comb,&reversedComb}) REQUIRE(f->prepare({48000,7,2}));
    for(auto* f:{&dispersion,&reversedDispersion}) REQUIRE(f->prepare({48000,7,2}));
    for(auto* f:{&tone,&reversedTone}) REQUIRE(f->prepare({48000,7,2}));
    for(auto* f:{&hp,&reversedHp}) REQUIRE(f->prepare({48000,7,2}));
    for(auto* f:{&lp,&reversedLp}) REQUIRE(f->prepare({48000,7,2}));
    // Match the composite defaults before reset: standalone modules default active.
    for(auto* f:{&comb,&reversedComb}) { REQUIRE(f->setParameters(s.comb)); f->reset(); }
    for(auto* f:{&dispersion,&reversedDispersion}) { REQUIRE(f->setParameters(s.dispersion)); f->reset(); }
    for(auto* f:{&tone,&reversedTone}) { REQUIRE(f->setParameters(s.tone)); f->reset(); }
    double error=0., discrimination=0.; juce::AudioBuffer<float> a(2,127);
    for(unsigned event=0;event<80;++event)
    {
        s.comb={event%2?.25f:40.f,event%2?-1.f:1.f,event%2?-.95f:.95f,20000.f,1.f,false,event%7==0};
        s.dispersion={event%2?20.f:20000.f,1.f,event%5==0}; s.tone={event%2?-6.f:6.f,event%3==0};
        s.highPass={event%2?20.f:12000.f,false,FilterResponse::chebyshev1};
        s.lowPass={event%2?19000.f:300.f,false,FilterResponse::chebyshev2};
        REQUIRE(section.setParameters(s));
        // The composite always enables damping, even for standalone settings
        // that request the historical undamped comb reference mode.
        auto dampedComb=s.comb; dampedComb.dampingEnabled=true;
        for(auto* f:{&comb,&reversedComb}) REQUIRE(f->setParameters(dampedComb));
        for(auto* f:{&dispersion,&reversedDispersion}) REQUIRE(f->setParameters(s.dispersion));
        for(auto* f:{&tone,&reversedTone}) REQUIRE(f->setParameters(s.tone));
        for(auto* f:{&hp,&reversedHp}) REQUIRE(f->setParameters(s.highPass));
        for(auto* f:{&lp,&reversedLp}) REQUIRE(f->setParameters(s.lowPass));
        for(int c=0;c<2;++c)for(int n=0;n<127;++n)a.setSample(c,n,float((c?-.7:1.)*(.7*std::sin((event*127+n)*.37)+.3*std::sin((event*127+n)*.013))));
        auto b=a,r=a; REQUIRE(section.process(juce::dsp::AudioBlock<float>(a)));
        auto block=juce::dsp::AudioBlock<float>(b);
        REQUIRE(comb.process(block)); REQUIRE(dispersion.process(block)); REQUIRE(tone.process(block)); REQUIRE(hp.process(block));
        const auto postHp=b.getMagnitude(0,127); REQUIRE(lp.process(block));
        REQUIRE(section.getPostHighPassPeak()==postHp);
        auto reversed=juce::dsp::AudioBlock<float>(r);
        REQUIRE(reversedLp.process(reversed)); REQUIRE(reversedHp.process(reversed)); REQUIRE(reversedTone.process(reversed));
        REQUIRE(reversedDispersion.process(reversed)); REQUIRE(reversedComb.process(reversed));
        for(int c=0;c<2;++c)for(int n=0;n<127;++n)
        { error=std::max(error,std::abs(double(a.getSample(c,n))-b.getSample(c,n))); discrimination=std::max(discrimination,std::abs(double(r.getSample(c,n))-b.getSample(c,n))); }
    }
    REQUIRE(error<=1e-6); REQUIRE(discrimination>1e-3); REQUIRE(section.getLatencySamples()==0);
}

TEST_CASE("Output invalid expanded settings preserve every active stage history and target", "[output-section]")
{
    OutputSection subject, reference; OutputSettings s;
    s.comb.bypass=false; s.comb.feedback=.95f; s.dispersion={20.f,1.f,false}; s.tone={6.f,false};
    for(auto* f:{&subject,&reference}) { REQUIRE(f->setParameters(s)); REQUIRE(f->prepare({48000,7,2})); }
    juce::AudioBuffer<float> a(2,1031); a.clear(); a.setSample(0,0,1.f); auto b=a;
    REQUIRE(subject.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
    for(unsigned field=0;field<5;++field)
    {
        auto invalid=s; invalid.comb.feedback=-.9f; invalid.dispersion.frequencyHz=17000.f; invalid.tone.toneDb=-6.f;
        invalid.highPass.cutoffHz=10000.f; invalid.lowPass.cutoffHz=200.f;
        switch(field) { case 0:invalid.comb.delayMs=0.f;break;case 1:invalid.dispersion.mix=2.f;break;case 2:invalid.tone.toneDb=7.f;break;case 3:invalid.highPass.cutoffHz=0.f;break;default:invalid.lowPass.cutoffHz=std::numeric_limits<float>::quiet_NaN(); }
        REQUIRE_FALSE(subject.setParameters(invalid)); REQUIRE_FALSE(subject.prepare({7999,7,2}));
        a.clear(); b.clear(); REQUIRE(subject.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
        for(int c=0;c<2;++c)for(int n=0;n<1031;++n) REQUIRE(a.getSample(c,n)==b.getSample(c,n));
    }
    subject.reset(); a.clear(); REQUIRE(subject.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(a.getMagnitude(0,1031)==0.f);
}

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
