#include "dsp/InputSection.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <array>
#include <limits>
using namespace disdorktion;
namespace {
juce::AudioBuffer<float> signal(int channels, int frames, int offset=0) {
    juce::AudioBuffer<float> b(channels,frames);
    for(int c=0;c<channels;++c) for(int n=0;n<frames;++n)
        b.setSample(c,n,static_cast<float>(.9*std::sin((n+offset)*.031+c*.7)));
    return b;
}
void equal(const juce::AudioBuffer<float>& a,const juce::AudioBuffer<float>& b) {
    REQUIRE(a.getNumChannels()==b.getNumChannels()); REQUIRE(a.getNumSamples()==b.getNumSamples());
    for(int c=0;c<a.getNumChannels();++c) for(int n=0;n<a.getNumSamples();++n)
        REQUIRE(a.getSample(c,n)==b.getSample(c,n));
}
}
static_assert(Module<InputSection,InputSettings>);
static_assert(noexcept(std::declval<InputSection&>().prepare(std::declval<const juce::dsp::ProcessSpec&>())));
TEST_CASE("Input section exact composition covers depth bypass expressive automation and reset", "[input-section]") {
    for(auto depth:bitCrusherDepths) for(int flags=0;flags<4;++flags) for(int channels:{1,2}) {
        InputSection section; Gain gain; BitCrusher crusher;
        juce::dsp::ProcessSpec spec {48000,127,static_cast<unsigned>(channels)};
        REQUIRE(section.prepare(spec)); REQUIRE(gain.prepare(spec)); REQUIRE(crusher.prepare(spec));
        for(int event=0;event<12;++event) {
            InputSettings s; s.gain={event%2 ? 24.f : -60.f,event==5,(flags&1)!=0};
            s.crusher={depth,static_cast<float>(1777.25+event*931),(flags&2)!=0,event%3*.5f,
                event%2*.7f,event%3*.8f-.8f,event%2*.9f,static_cast<std::uint32_t>(event)};
            REQUIRE(section.setParameters(s)); REQUIRE(gain.setParameters(s.gain)); REQUIRE(crusher.setParameters(s.crusher));
            REQUIRE(section.setParameters(s)); REQUIRE(gain.setParameters(s.gain)); REQUIRE(crusher.setParameters(s.crusher));
            if(event==6) {section.reset();gain.reset();crusher.reset(); REQUIRE(section.getPostGainPeak()==0);}
            auto a=signal(channels,event%2?4099:31,event*97); auto b=a;
            REQUIRE(gain.process(juce::dsp::AudioBlock<float>(b)));
            float peak=0; for(int c=0;c<channels;++c) for(int n=0;n<b.getNumSamples();++n) peak=std::max(peak,std::abs(b.getSample(c,n)));
            REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(b)));
            REQUIRE(section.process(juce::dsp::AudioBlock<float>(a))); equal(a,b);
            REQUIRE(section.getPostGainPeak()==peak); REQUIRE(section.getLatencySamples()==0);
        }
    }
}
TEST_CASE("Input section partition invariance across rate capacity and control events", "[input-section]") {
    for(double rate:{8000.,44100.,48000.,96000.,384000.}) for(unsigned capacity:{1u,32u,64u,127u,256u,1024u,65536u}) {
        InputSection whole,split; REQUIRE(whole.prepare({rate,capacity,2})); REQUIRE(split.prepare({rate,capacity,2}));
        for(int event=0;event<5;++event) {
            InputSettings s; s.gain={event%2?24.f:-17.f,event==3,event==1};
            s.crusher={4,777.25f,event==2,.63f,.42f,-.7f,.8f,static_cast<std::uint32_t>(event)};
            REQUIRE(whole.setParameters(s)); REQUIRE(split.setParameters(s));
            if(event==3) {whole.reset();split.reset();}
            auto a=signal(2,2053,event*2053); auto b=a;
            REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a)));
            for(std::size_t pos=0;pos<2053;) {auto n=std::min<std::size_t>((pos%149)+1,2053-pos);
                REQUIRE(split.process(juce::dsp::AudioBlock<float>(b).getSubBlock(pos,n))); pos+=n;}
            equal(a,b);
        }
    }
}
TEST_CASE("Input section rejected settings preparation layout and empty calls preserve hidden state", "[input-section]") {
    InputSection section,reference; auto untouched=signal(2,13); auto copy=untouched;
    REQUIRE_FALSE(section.process(juce::dsp::AudioBlock<float>(untouched))); equal(untouched,copy);
    InputSettings s; s.gain={12,false,false}; s.crusher={5,731.25f,false,.6f,.3f,-.4f,.8f,0};
    REQUIRE(section.setParameters(s)); REQUIRE(reference.setParameters(s));
    REQUIRE(section.prepare({48000,32,2})); REQUIRE(reference.prepare({48000,32,2}));
    auto warm=signal(2,77); auto r=warm; REQUIRE(section.process(juce::dsp::AudioBlock<float>(warm))); REQUIRE(reference.process(juce::dsp::AudioBlock<float>(r)));
    s.gain.gainDb=-27; s.crusher.captureRateHz=3111.75f; REQUIRE(section.setParameters(s)); REQUIRE(reference.setParameters(s));
    const auto peak=section.getPostGainPeak();
    for(int bad=0;bad<10;++bad) {auto invalid=s; invalid.gain.gainDb=24; if(bad>=6) {invalid.crusher.captureRateHz=384000;invalid.crusher.bitDepth=1;invalid.crusher.jitterSeed=987;}
        switch(bad) {case 0:invalid.crusher.bitDepth=9;break;case 1:invalid.crusher.captureRateHz=99;break;
            case 2:invalid.crusher.mix=1.01f;break;case 3:invalid.crusher.crush=-.1f;break;case 4:invalid.crusher.shape=2;break;
            case 5:invalid.crusher.jitter=std::numeric_limits<float>::quiet_NaN();break;
            case 6:invalid.gain.gainDb=-61;break;case 7:invalid.gain.gainDb=25;break;
            case 8:invalid.gain.gainDb=std::numeric_limits<float>::infinity();break;case 9:invalid.gain.gainDb=std::numeric_limits<float>::quiet_NaN();break;}
        REQUIRE_FALSE(isValidInputSettings(invalid)); REQUIRE_FALSE(section.setParameters(invalid));
    }
    for(auto spec:std::array<juce::dsp::ProcessSpec,9>{{{7999,32,2},{384001,32,2},{0,32,2},{48000,0,2},{48000,65537,2},{48000,32,0},{48000,32,3},{std::numeric_limits<double>::quiet_NaN(),32,2},{std::numeric_limits<double>::infinity(),32,2}}}) REQUIRE_FALSE(section.prepare(spec));
    auto wrong=signal(1,19); auto wrongCopy=wrong; REQUIRE_FALSE(section.process(juce::dsp::AudioBlock<float>(wrong))); equal(wrong,wrongCopy);
    REQUIRE(section.process(juce::dsp::AudioBlock<float>(warm).getSubBlock(0,0))); REQUIRE(section.getPostGainPeak()==peak);
    auto probe=signal(2,4099); auto ref=probe; REQUIRE(section.process(juce::dsp::AudioBlock<float>(probe))); REQUIRE(reference.process(juce::dsp::AudioBlock<float>(ref))); equal(probe,ref);
    section.reset(); reference.reset(); REQUIRE(section.getPostGainPeak()==0);
    probe=signal(2,997); ref=probe; REQUIRE(section.process(juce::dsp::AudioBlock<float>(probe))); REQUIRE(reference.process(juce::dsp::AudioBlock<float>(ref))); equal(probe,ref);
}
TEST_CASE("Input section settled PCM gain overload dry and wet expectations", "[input-section]") {
    for(auto depth:bitCrusherDepths) for(float gainDb:{-60.f,0.f,24.f}) for(float mix:{0.f,.25f,1.f}) {
        InputSection section; InputSettings s; s.gain.gainDb=gainDb; s.crusher.bitDepth=depth;s.crusher.mix=mix;
        REQUIRE(section.setParameters(s)); REQUIRE(section.prepare({48000,32,2}));
        juce::AudioBuffer<float> b(2,129); for(int n=0;n<129;++n) {b.setSample(0,n,.75f);b.setSample(1,n,-.75f);}
        REQUIRE(section.process(juce::dsp::AudioBlock<float>(b)));
        const float post=.75f*static_cast<float>(std::pow(10.,gainDb/20.));
        REQUIRE(section.getPostGainPeak()==post);
        const double scale=std::ldexp(1.,static_cast<int>(depth)-1);
        for(int c=0;c<2;++c) {const double dry=c==0?post:-post;
            const double wet=std::clamp(std::round(std::clamp(dry,-1.,1.)*scale)/scale,-1.,1.-1./scale);
            const float expected=static_cast<float>(mix*static_cast<float>(wet)+(1.-mix)*dry);
            for(int n=0;n<129;++n) REQUIRE(b.getSample(c,n)==Catch::Approx(expected).margin(2e-7));
        }
        REQUIRE(section.getLatencySamples()==0);
    }
}
TEST_CASE("Input section peak spans chunks and meter clears only for nonempty success or reset", "[input-section]") {
    InputSection section; InputSettings s;s.crusher.mix=0; REQUIRE(section.setParameters(s));REQUIRE(section.prepare({48000,32,2}));
    juce::AudioBuffer<float> b(2,99); b.clear(); b.setSample(1,4,-3);b.setSample(0,98,.1f);
    REQUIRE(section.process(juce::dsp::AudioBlock<float>(b))); REQUIRE(section.getPostGainPeak()==3);
    REQUIRE(section.process(juce::dsp::AudioBlock<float>(b).getSubBlock(0,0))); REQUIRE(section.getPostGainPeak()==3);
    b.clear(); REQUIRE(section.process(juce::dsp::AudioBlock<float>(b))); REQUIRE(section.getPostGainPeak()==0);
}
TEST_CASE("Input section gain mute keeps captured stereo tail until next capture", "[input-section]") {
    InputSection section; InputSettings s; s.crusher.captureRateHz=100;s.crusher.bitDepth=8;
    REQUIRE(section.setParameters(s)); REQUIRE(section.prepare({96000,32,2}));
    juce::AudioBuffer<float> b(2,1);b.setSample(0,0,.5f);b.setSample(1,0,-.25f);REQUIRE(section.process(juce::dsp::AudioBlock<float>(b)));
    s.gain.mute=true;REQUIRE(section.setParameters(s));
    b.setSize(2,960);for(int n=0;n<960;++n){b.setSample(0,n,.5f);b.setSample(1,n,-.25f);}
    REQUIRE(section.process(juce::dsp::AudioBlock<float>(b)));
    for(int n=0;n<959;++n){REQUIRE(b.getSample(0,n)==.5f);REQUIRE(b.getSample(1,n)==-.25f);}
    REQUIRE(b.getSample(0,959)==0);REQUIRE(b.getSample(1,959)==0);
}

TEST_CASE("Input section supported finite overload envelope remains finite and isolates instances", "[input-section]") {
    InputSection a,b;InputSettings s;s.gain.gainDb=24;s.crusher.mix=.5f;
    REQUIRE(a.setParameters(s));REQUIRE(a.prepare({384000,1,2}));REQUIRE(b.prepare({8000,65536,1}));
    juce::AudioBuffer<float> huge(2,79);const auto envelope=std::numeric_limits<float>::max()/16;
    for(int n=0;n<79;++n){huge.setSample(0,n,envelope);huge.setSample(1,n,-envelope);}
    REQUIRE(a.process(juce::dsp::AudioBlock<float>(huge)));REQUIRE(std::isfinite(a.getPostGainPeak()));
    REQUIRE(a.getPostGainPeak()>envelope);
    for(int c=0;c<2;++c)for(int n=0;n<79;++n)REQUIRE(std::isfinite(huge.getSample(c,n)));
    auto neutral=signal(1,57);auto original=neutral;REQUIRE(b.process(juce::dsp::AudioBlock<float>(neutral)));equal(neutral,original);
    REQUIRE(a.getLatencySamples()==0);REQUIRE(b.getLatencySamples()==0);
}
