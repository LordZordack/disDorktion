#include "dsp/FrequencyDispersion.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

using namespace disdorktion;
static_assert(Module<FrequencyDispersion, FrequencyDispersionSettings>);
namespace
{
std::vector<float> response(float frequency, double rate, int count)
{
    FrequencyDispersion m; FrequencyDispersionSettings s; s.frequencyHz=frequency;
    REQUIRE(m.setParameters(s)); REQUIRE(m.prepare({rate,127,1}));
    juce::AudioBuffer<float> b(1,count); b.clear(); b.setSample(0,0,1);
    REQUIRE(m.process(juce::dsp::AudioBlock<float>(b)));
    return {b.getReadPointer(0),b.getReadPointer(0)+count};
}
}
TEST_CASE("Frequency dispersion validates transactionally and calibrates effective frequency", "[frequency-dispersion]")
{
    FrequencyDispersion m; juce::AudioBuffer<float> b(1,1); b.setSample(0,0,.25f);
    REQUIRE_FALSE(m.process(juce::dsp::AudioBlock<float>(b))); REQUIRE(b.getSample(0,0)==.25f);
    for(float f:{0.f,19.f,20001.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
    { FrequencyDispersionSettings s; s.frequencyHz=f; REQUIRE_FALSE(m.setParameters(s)); }
    for(float v:{-.1f,1.1f,std::numeric_limits<float>::quiet_NaN()})
    { FrequencyDispersionSettings s; s.mix=v; REQUIRE_FALSE(m.setParameters(s)); }
    REQUIRE(m.prepare({48000,32,1})); REQUIRE(m.getEffectiveFrequencyHz()==Catch::Approx(500).margin(1e-9));
    const double coefficient=m.getCoefficient();
    for(auto spec:{juce::dsp::ProcessSpec{7999,32,1},{384001,32,1},{48000,0,1},{48000,65537,1},{48000,1,3}})
        REQUIRE_FALSE(m.prepare(spec));
    REQUIRE(m.getCoefficient()==coefficient); REQUIRE(m.getLatencySamples()==0);
    REQUIRE(m.prepare({384000,1,2})); REQUIRE(m.getCoefficient()==Catch::Approx(-.99));
    REQUIRE(m.getEffectiveFrequencyHz()>500);
    FrequencyDispersionSettings s; s.frequencyHz=20000; REQUIRE(m.setParameters(s)); REQUIRE(m.prepare({8000,1,1}));
    REQUIRE(m.getEffectiveFrequencyHz()==Catch::Approx(3600).margin(1e-9));
}
TEST_CASE("Frequency dispersion exact delay and independent static recurrence", "[frequency-dispersion]")
{
    const auto delayed=response(12000,48000,128);
    for(int n=0;n<128;++n) REQUIRE(delayed[n]==Catch::Approx(n==32?1.:0.).margin(1e-12));
    for(float f:{20.f,500.f,20000.f})
    {
        const auto y=response(f,48000,16384);
        const double t=std::tan(3.14159265358979323846*std::min(double(f),.45*48000)/48000);
        const double a=std::clamp((t-1)/(t+1),-.99,.99);
        std::array<double,32> previousInput{},previousOutput{};
        double energy=0;
        for(std::size_t n=0;n<y.size();++n)
        {
            double x=n==0?1.:0.;
            for(int k=0;k<32;++k) { const double out=a*x+previousInput[k]-a*previousOutput[k]; previousInput[k]=x; previousOutput[k]=out; x=out; }
            REQUIRE(y[n]==Catch::Approx(x).margin(2e-7)); energy+=double(y[n])*y[n];
        }
        REQUIRE(energy==Catch::Approx(1.).margin(2e-6));
    }
}
TEST_CASE("Frequency dispersion matrix preserves reset silence channels and block partition", "[frequency-dispersion]")
{
    for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.})
    for(unsigned channels:{1u,2u})
    for(unsigned capacity:{1u,32u,64u,127u,256u,1024u})
    {
        FrequencyDispersion a,b; REQUIRE(a.prepare({rate,capacity,channels})); REQUIRE(b.prepare({rate,capacity,channels}));
        juce::AudioBuffer<float> x(int(channels),1103),y(int(channels),1103); x.clear();y.clear();
        REQUIRE(a.process(juce::dsp::AudioBlock<float>(x))); REQUIRE(x.getMagnitude(0,1103)==0);
        a.reset(); x.setSample(0,0,1);y.setSample(0,0,1);
        FrequencyDispersionSettings s; s.frequencyHz=20;s.mix=.5f; REQUIRE(a.setParameters(s));REQUIRE(b.setParameters(s));
        REQUIRE(a.process(juce::dsp::AudioBlock<float>(x).getSubBlock(0,0)));
        REQUIRE(a.process(juce::dsp::AudioBlock<float>(x)));
        for(int offset=0;offset<1103;offset+=17) REQUIRE(b.process(juce::dsp::AudioBlock<float>(y).getSubBlock(std::size_t(offset),std::size_t(std::min(17,1103-offset)))));
        for(unsigned c=0;c<channels;++c) for(int n=0;n<1103;++n)
        { REQUIRE(x.getSample(int(c),n)==y.getSample(int(c),n)); REQUIRE(std::isfinite(x.getSample(int(c),n))); if(c==1) REQUIRE(x.getSample(1,n)==0); }
        a.reset();b.reset();x.clear();y.clear();x.setSample(0,0,1);y.setSample(0,0,1);
        REQUIRE(a.process(juce::dsp::AudioBlock<float>(x))); REQUIRE(b.process(juce::dsp::AudioBlock<float>(y)));
        for(int n=0;n<1103;++n) REQUIRE(x.getSample(0,n)==y.getSample(0,n));
    }
}
TEST_CASE("Frequency dispersion bypass reaches exact dry and keeps history warm", "[frequency-dispersion]")
{
    FrequencyDispersion m; REQUIRE(m.prepare({8000,64,1})); FrequencyDispersionSettings s;s.bypass=true; REQUIRE(m.setParameters(s));
    juce::AudioBuffer<float> x(1,800); for(int n=0;n<800;++n)x.setSample(0,n,.25f);
    REQUIRE(m.process(juce::dsp::AudioBlock<float>(x))); for(int n=80;n<800;++n) REQUIRE(x.getSample(0,n)==.25f);
    s.bypass=false; REQUIRE(m.setParameters(s));x.clear(); REQUIRE(m.process(juce::dsp::AudioBlock<float>(x))); REQUIRE(x.getMagnitude(0,800)>0);
    s.mix=0; REQUIRE(m.setParameters(s));m.reset();for(int n=0;n<800;++n)x.setSample(0,n,.125f);
    REQUIRE(m.process(juce::dsp::AudioBlock<float>(x)));for(int n=0;n<800;++n)REQUIRE(x.getSample(0,n)==.125f);
}

TEST_CASE("Frequency dispersion rejects layout and settings without disturbing history", "[frequency-dispersion]")
{
    FrequencyDispersion a,b;REQUIRE(a.prepare({48000,32,1}));REQUIRE(b.prepare({48000,32,1}));
    juce::AudioBuffer<float> x(1,128),y(1,128),wrong(2,16);x.clear();y.clear();wrong.clear();x.setSample(0,0,1);y.setSample(0,0,1);
    REQUIRE(a.process(juce::dsp::AudioBlock<float>(x)));REQUIRE(b.process(juce::dsp::AudioBlock<float>(y)));
    REQUIRE_FALSE(a.prepare({48000,0,1}));REQUIRE_FALSE(a.process(juce::dsp::AudioBlock<float>(wrong)));
    FrequencyDispersionSettings invalid;invalid.frequencyHz=20;invalid.mix=2;REQUIRE_FALSE(a.setParameters(invalid));
    x.clear();y.clear();REQUIRE(a.process(juce::dsp::AudioBlock<float>(x)));REQUIRE(b.process(juce::dsp::AudioBlock<float>(y)));
    for(int n=0;n<128;++n)REQUIRE(x.getSample(0,n)==y.getSample(0,n));
}

TEST_CASE("Frequency dispersion automated lattice cannot create impulse energy", "[frequency-dispersion]")
{
    for(double rate:{8000.,48000.,384000.})
    {
        FrequencyDispersion m;REQUIRE(m.prepare({rate,127,1}));
        juce::AudioBuffer<float> x(1,127);double energy=0;
        for(int block=0;block<256;++block)
        {
            FrequencyDispersionSettings s;s.frequencyHz=block%2?20.f:20000.f;REQUIRE(m.setParameters(s));
            x.clear();if(block==0)x.setSample(0,0,1);REQUIRE(m.process(juce::dsp::AudioBlock<float>(x)));
            for(int n=0;n<127;++n)energy+=double(x.getSample(0,n))*x.getSample(0,n);
            REQUIRE(energy<=1.+2e-6);
        }
        REQUIRE(energy>0.999);
    }
}

TEST_CASE("Frequency dispersion static impulse obeys declared tail bound", "[frequency-dispersion]")
{
    FrequencyDispersion unprepared;REQUIRE(unprepared.getTailSeconds()==0);
    for(double rate:{8000.,48000.,384000.})for(float frequency:{20.f,500.f,20000.f})
    {
        FrequencyDispersion m;FrequencyDispersionSettings s;s.frequencyHz=frequency;REQUIRE(m.setParameters(s));REQUIRE(m.prepare({rate,127,1}));
        const int tail=int(std::ceil(m.getTailSeconds()*rate));
        const auto y=response(frequency,rate,tail+128);
        for(int n=tail;n<tail+128;++n)REQUIRE(std::abs(y[std::size_t(n)])<1e-6f);
    }
}

TEST_CASE("Frequency dispersion follows independent interrupted frame ramp oracle", "[frequency-dispersion]")
{
    // Closed-form envelopes deliberately avoid JUCE's smoothing implementation.
    struct Ramp
    {
        double start, target, current;
        int elapsed, length;
        void set(double value)
        {
            if(value==target) return;
            start=current; target=value; elapsed=0;
        }
        double next()
        {
            if(elapsed<length) ++elapsed;
            current=elapsed==length?target:start+(target-start)*double(elapsed)/double(length);
            return current;
        }
    };
    for(const double rate:{8000.,44100.})
    {
        const int coefficientFrames=int(std::floor(rate*.100));
        const int blendFrames=int(std::floor(rate*.010));
        const auto coefficientFor=[rate](float frequency)
        {
            const double angle=3.14159265358979323846*std::min(double(frequency),.45*rate)/rate;
            const double sine=std::sin(angle),cosine=std::cos(angle);
            return std::clamp((sine-cosine)/(sine+cosine),-.99,.99);
        };
        FrequencyDispersion module;
        REQUIRE(module.prepare({rate,17,2}));
        const double initial=coefficientFor(500.f);
        Ramp a{initial,initial,initial,coefficientFrames,coefficientFrames};
        Ramp mix{1.,1.,1.,blendFrames,blendFrames};
        Ramp bypass{0.,0.,0.,blendFrames,blendFrames};
        std::array<std::array<double,32>,2> state{};
        juce::AudioBuffer<float> frame(2,1);
        FrequencyDispersionSettings settings;
        const int finalChange=coefficientFrames+151;
        for(int n=0;n<finalChange+coefficientFrames+19;++n)
        {
            bool changed=false;
            if(n==0) { settings.frequencyHz=20000;settings.mix=.2f;changed=true; }
            if(n==37) { settings.frequencyHz=2000;settings.mix=.8f;settings.bypass=true;changed=true; }
            if(n==101) { settings.frequencyHz=20;settings.mix=.65f;settings.bypass=false;changed=true; }
            if(n==finalChange) { settings.frequencyHz=20000;settings.mix=1;changed=true; }
            // Repeated identical targets must preserve each in-flight ramp.
            if(changed || n%17==0)
            {
                REQUIRE(module.setParameters(settings));
                a.set(coefficientFor(settings.frequencyHz));
                mix.set(settings.mix);bypass.set(settings.bypass?1.:0.);
            }
            // Advance before application, once for the shared stereo frame.
            const double coefficient=a.next();
            const double g=std::sqrt(1.-coefficient*coefficient);
            const double weight=mix.next()*(1.-bypass.next());
            std::array<double,2> expected{};
            for(int c=0;c<2;++c)
            {
                const float dry=float(.3*std::sin(.37*double(n)+double(c))+.2*std::cos(.113*double(n)*(c+1)));
                frame.setSample(c,0,dry);
                double wet=dry;
                for(auto& memory:state[std::size_t(c)])
                {
                    const double output=coefficient*wet+g*memory;
                    memory=g*wet-coefficient*memory;wet=output;
                }
                expected[std::size_t(c)]=(1.-weight)*double(dry)+weight*wet;
            }
            REQUIRE(module.process(juce::dsp::AudioBlock<float>(frame)));
            for(int c=0;c<2;++c)
                REQUIRE(frame.getSample(c,0)==Catch::Approx(expected[std::size_t(c)]).margin(2e-6));
        }
    }
}
