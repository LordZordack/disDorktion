#include "dsp/ResonantComb.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

using namespace disdorktion;
static_assert(Module<ResonantComb, ResonantCombSettings>);
namespace
{
constexpr double pi = 3.14159265358979323846;
std::vector<float> impulse(ResonantCombSettings s, double rate, int count)
{
    ResonantComb comb;
    REQUIRE(comb.setParameters(s));
    REQUIRE(comb.prepare({rate, 127, 1}));
    juce::AudioBuffer<float> b(1, count); b.clear(); b.setSample(0, 0, 1);
    REQUIRE(comb.process(juce::dsp::AudioBlock<float>(b)));
    return {b.getReadPointer(0), b.getReadPointer(0) + count};
}
struct Ramp
{
    double value, target, step = 0; int remaining = 0;
    explicit Ramp(double v) : value(v), target(v) {}
    void set(double v, int n) { if (target != v) { target = v; remaining = n; step = (target-value)/n; } }
    double next() { if (remaining > 0) { value += step; if (--remaining == 0) value = target; } return value; }
};
}

TEST_CASE("Resonant comb validates controls and preserves rejected preparation", "[resonant-comb]")
{
    ResonantComb comb;
    ResonantCombSettings s;
    REQUIRE(isValidResonantCombSettings(s));
    juce::AudioBuffer<float> b(1, 1); b.setSample(0, 0, .25f);
    REQUIRE_FALSE(comb.process(juce::dsp::AudioBlock<float>(b)));
    REQUIRE(b.getSample(0, 0) == .25f);
    for (int field = 0; field < 5; ++field)
        for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -100.f, 30000.f})
        {
            auto bad = s;
            if (field == 0) bad.delayMs = invalid;
            if (field == 1) bad.feedforward = invalid;
            if (field == 2) bad.feedback = invalid;
            if (field == 3) { bad.dampingHz = invalid; bad.dampingEnabled = false; }
            if (field == 4) bad.mix = invalid;
            REQUIRE_FALSE(isValidResonantCombSettings(bad));
            REQUIRE_FALSE(comb.setParameters(bad));
        }
    s.feedback = 0; s.feedforward = 0; s.mix = 1;
    REQUIRE(comb.setParameters(s)); REQUIRE(comb.prepare({48000, 1, 1}));
    for (auto spec : {juce::dsp::ProcessSpec{7999, 32, 1}, {384001, 32, 1}, {48000, 0, 1}, {48000, 65537, 1}, {48000, 32, 3}, {std::numeric_limits<double>::quiet_NaN(), 32, 1}})
        REQUIRE_FALSE(comb.prepare(spec));
    REQUIRE(comb.process(juce::dsp::AudioBlock<float>(b)));
    REQUIRE(b.getSample(0, 0) == .25f);
    REQUIRE(comb.getLatencySamples() == 0);
    REQUIRE(comb.getEffectiveDampingHz() == 6000);
    REQUIRE(comb.prepare({8000, 1, 2}));
    REQUIRE(comb.getEffectiveDampingHz() == 3600);
    REQUIRE(comb.prepare({48000, 1, 1}));
    REQUIRE(comb.getEffectiveDampingHz() == 6000);
}

TEST_CASE("Resonant comb integer impulse trains match signed feedback exactly", "[resonant-comb]")
{
    for (float feedback : {-.95f, -.7f, 0.f, .7f, .95f})
        for (float ff : {-1.f, 0.f, 1.f})
        {
            ResonantCombSettings s; s.delayMs=1; s.feedback=feedback; s.feedforward=ff; s.mix=1; s.dampingEnabled=false;
            const auto y=impulse(s, 48000, 48*400+1);
            double term=1;
            for (std::size_t n=0; n<y.size(); ++n)
            {
                double expected=0;
                if(n==0) expected=1;
                else if(n%48==0) { expected=term*(feedback+ff); term*=feedback; }
                REQUIRE(y[n] == Catch::Approx(expected).margin(2e-7));
            }
        }
}

TEST_CASE("Resonant comb failed reprepare retains a running tail and quiet state", "[resonant-comb]")
{
    ResonantComb a,b; ResonantCombSettings s; s.delayMs=1; s.mix=1; s.feedback=.95f; s.dampingEnabled=false;
    REQUIRE(a.setParameters(s)); REQUIRE(b.setParameters(s)); REQUIRE(a.prepare({48000,32,2})); REQUIRE(b.prepare({48000,32,2}));
    juce::AudioBuffer<float> x(2,31), y(2,31); x.clear(); y.clear(); x.setSample(0,0,1e-20f); y.setSample(0,0,1e-20f);
    REQUIRE(a.process(juce::dsp::AudioBlock<float>(x))); REQUIRE(b.process(juce::dsp::AudioBlock<float>(y)));
    auto target=s; target.feedback=-.95f; target.delayMs=40;
    REQUIRE(a.setParameters(target)); REQUIRE(b.setParameters(target));
    REQUIRE_FALSE(a.prepare({48000,0,1}));
    bool quietTail=false;
    for(int pass=0;pass<100;++pass)
    {
        x.clear(); y.clear(); REQUIRE(a.process(juce::dsp::AudioBlock<float>(x))); REQUIRE(b.process(juce::dsp::AudioBlock<float>(y)));
        for(int n=0;n<31;++n) { REQUIRE(x.getSample(0,n)==y.getSample(0,n)); quietTail|=x.getSample(0,n)!=0; }
    }
    REQUIRE(quietTail);
    a.reset(); x.clear(); REQUIRE(a.process(juce::dsp::AudioBlock<float>(x))); REQUIRE(x.getMagnitude(0,31)==0);
}

TEST_CASE("Resonant comb bypass keeps histories warm and fades back to the running tail", "[resonant-comb]")
{
    ResonantComb wet,bypassed; ResonantCombSettings s;
    s.delayMs=40; s.feedback=.95f; s.feedforward=-1; s.dampingHz=20; s.mix=1;
    REQUIRE(wet.setParameters(s)); REQUIRE(wet.prepare({8000,32,1}));
    const auto staticTail=wet.getTailSeconds();
    s.bypass=true; REQUIRE(bypassed.setParameters(s)); REQUIRE(bypassed.prepare({8000,32,1}));
    REQUIRE(bypassed.getTailSeconds()==staticTail);
    juce::AudioBuffer<float> x(1,1600),y(1,1600); x.clear(); y.clear(); x.setSample(0,0,1); y.setSample(0,0,1);
    REQUIRE(wet.process(juce::dsp::AudioBlock<float>(x))); REQUIRE(bypassed.process(juce::dsp::AudioBlock<float>(y)));
    REQUIRE(y.getSample(0,0)==1);
    for(int n=1;n<1600;++n) REQUIRE(y.getSample(0,n)==0);
    s.bypass=false; REQUIRE(bypassed.setParameters(s));
    x.clear(); y.clear(); REQUIRE(wet.process(juce::dsp::AudioBlock<float>(x))); REQUIRE(bypassed.process(juce::dsp::AudioBlock<float>(y)));
    bool nonzeroTail=false;
    for(int n=0;n<1600;++n)
    {
        const double weight=std::min(1.,static_cast<double>(n+1)/80.);
        REQUIRE(y.getSample(0,n)==Catch::Approx(weight*x.getSample(0,n)).margin(2e-7));
        if(n>=79) { REQUIRE(y.getSample(0,n)==x.getSample(0,n)); nonzeroTail|=y.getSample(0,n)!=0; }
    }
    REQUIRE(nonzeroTail);
    s.mix=0; REQUIRE(bypassed.setParameters(s)); REQUIRE(bypassed.getTailSeconds()==staticTail);
}

TEST_CASE("Resonant comb fractional complex response includes damping and exact notches", "[resonant-comb]")
{
    for (float delay : {1.f, 1.03125f})
        for (float feedback : {-.7f, 0.f, .7f})
            for (float ff : {-1.f, .3f, 1.f})
                for (bool damped : {false,true})
                {
                    ResonantCombSettings s; s.delayMs=delay; s.feedback=feedback; s.feedforward=ff; s.dampingEnabled=damped; s.dampingHz=200; s.mix=.8f;
                    const double rate=48000;
                    ResonantComb tail; REQUIRE(tail.setParameters(s)); REQUIRE(tail.prepare({rate,127,1}));
                    // Add another -120 dB interval: residual sum then lies far below the comparison tolerance.
                    const int count=std::max(4096, static_cast<int>(std::ceil(2*tail.getTailSeconds()*rate)));
                    const auto y=impulse(s,rate,count);
                    const double d=delay*rate*.001, frac=d-std::floor(d);
                    const double a=damped ? std::exp(-2*pi*200/rate) : 0;
                    for(double frequency : {0., 500., 1000., 3900., 12000., 23900.})
                    {
                        const auto z=std::polar(1.,-2*pi*frequency/rate);
                        const auto zd=std::pow(z,static_cast<int>(std::floor(d)))*(1.-frac+frac*z);
                        const auto low=(1.-a)/(1.-a*z);
                        const auto wet=(1.+static_cast<double>(ff)*zd)/(1.-static_cast<double>(feedback)*low*zd);
                        const auto expected=1.-static_cast<double>(s.mix)+static_cast<double>(s.mix)*wet;
                        std::complex<double> measured=0, phase=1;
                        for(float sample:y) { measured+=static_cast<double>(sample)*phase; phase*=z; }
                        const double error=std::abs(measured-expected);
                        REQUIRE(error <= (std::abs(expected)>=1e-4 ? 1e-4*std::abs(expected) : 1e-5));
                    }
                }
    ResonantCombSettings notch; notch.delayMs=1; notch.feedback=.7f; notch.mix=1; notch.dampingEnabled=false;
    for(float ff:{-1.f,1.f})
    {
        notch.feedforward=ff; const auto y=impulse(notch,48000,48000);
        std::complex<double> h=0;
        const double w=ff<0 ? 2*pi*1000/48000 : 2*pi*500/48000;
        for(std::size_t n=0;n<y.size();++n) h+=static_cast<double>(y[n])*std::polar(1.,-w*n);
        REQUIRE(std::abs(h)<1e-5);
    }
}

TEST_CASE("Resonant comb matrix preserves isolation silence reset and block invariance", "[resonant-comb]")
{
    for(double rate:{8000.,44100.,48000.,96000.,192000.,384000.})
        for(unsigned channels:{1u,2u})
            for(unsigned capacity:{1u,32u,64u,127u,256u,1024u})
            {
                CAPTURE(rate,channels,capacity);
                ResonantComb a,b; ResonantCombSettings s; s.delayMs=.73125f;
                REQUIRE(a.setParameters(s)); REQUIRE(b.setParameters(s));
                REQUIRE(a.prepare({rate,capacity,channels})); REQUIRE(b.prepare({rate,capacity,channels}));
                juce::AudioBuffer<float> x(static_cast<int>(channels),2051),y(static_cast<int>(channels),2051);
                x.clear(); y.clear();
                REQUIRE(a.process(juce::dsp::AudioBlock<float>(x)));
                REQUIRE(x.getMagnitude(0,2051)==0);
                for(int n=0;n<2051;++n) x.setSample(0,n,static_cast<float>(.1*std::sin(.017*n)));
                y.makeCopyOf(x);
                s.delayMs=40; s.feedback=-.95f; s.feedforward=-1; s.dampingEnabled=false; s.mix=1;
                REQUIRE(a.setParameters(s)); REQUIRE(b.setParameters(s));
                REQUIRE(a.process(juce::dsp::AudioBlock<float>(x).getSubBlock(0,0)));
                juce::AudioBuffer<float> wrong(channels==1?2:1,3); wrong.clear();
                REQUIRE_FALSE(a.process(juce::dsp::AudioBlock<float>(wrong)));
                REQUIRE(a.process(juce::dsp::AudioBlock<float>(x)));
                auto block=juce::dsp::AudioBlock<float>(y);
                for(std::size_t offset=0;offset<2051;offset+=17) REQUIRE(b.process(block.getSubBlock(offset,std::min<std::size_t>(17,2051-offset))));
                for(unsigned c=0;c<channels;++c) for(int n=0;n<2051;++n)
                {
                    REQUIRE(x.getSample(static_cast<int>(c),n)==Catch::Approx(y.getSample(static_cast<int>(c),n)).margin(2e-6));
                    if(c==1) REQUIRE(x.getSample(1,n)==0);
                }
                a.reset(); b.reset(); x.clear(); y.clear(); x.setSample(0,0,1); y.setSample(0,0,1);
                REQUIRE(a.process(juce::dsp::AudioBlock<float>(x))); REQUIRE(b.process(juce::dsp::AudioBlock<float>(y)));
                for(int n=0;n<2051;++n) REQUIRE(x.getSample(0,n)==y.getSample(0,n));
                REQUIRE(a.prepare({rate,capacity,1}));
                juce::AudioBuffer<float> mono(1,2051); mono.clear(); REQUIRE(a.process(juce::dsp::AudioBlock<float>(mono))); REQUIRE(mono.getMagnitude(0,2051)==0);
            }
}

TEST_CASE("Resonant comb automation matches independent absolute-time recurrence", "[resonant-comb]")
{
    constexpr double rate=8000; constexpr int count=6000, size=324;
    ResonantComb comb; ResonantCombSettings s; s.delayMs=.25f; s.feedback=.2f; s.feedforward=.3f; s.mix=.4f; s.dampingHz=20000;
    REQUIRE(comb.setParameters(s)); REQUIRE(comb.prepare({rate,32,2}));
    Ramp d(2), g(s.feedback), ff(s.feedforward), mix(s.mix), damping(std::exp(-2*pi*3600/rate)), bypass(0);
    std::vector<double> memory(size,0); double low=0; int write=0;
    juce::AudioBuffer<float> sample(2,1);
    for(int n=0;n<count;++n)
    {
        if(n%997==0)
        {
            const bool phase=(n/997)%2==0;
            s.delayMs=phase?40.f:.25f; s.feedback=phase?-.95f:.95f; s.feedforward=phase?-1.f:1.f;
            s.dampingEnabled=!phase; s.dampingHz=20; s.mix=phase?1.f:0.f; s.bypass=n>=2991;
            REQUIRE(comb.setParameters(s)); REQUIRE(comb.setParameters(s));
            d.set(s.delayMs*rate*.001,800); g.set(s.feedback,80); ff.set(s.feedforward,80); mix.set(s.mix,80);
            damping.set(s.dampingEnabled?std::exp(-2*pi*20/rate):0,80); bypass.set(s.bypass?1:0,80);
        }
        const float input=static_cast<float>(.25*std::sin(.021*n));
        const double delay=d.next(), gain=g.next(), forward=ff.next(), m=mix.next(), a=damping.next(), b=bypass.next();
        double read=write-delay; if(read<0) read+=size;
        const int i=static_cast<int>(std::floor(read)); const double fraction=read-i;
        const double delayed=memory[static_cast<std::size_t>(i)]*(1-fraction)+memory[static_cast<std::size_t>((i+1)%size)]*fraction;
        low=(1-a)*delayed+a*low;
        const double v=input+gain*low; memory[static_cast<std::size_t>(write)]=v; write=(write+1)%size;
        const double weight=m*(1-b), expected=weight==0?input:input+weight*(v+forward*delayed-input);
        sample.setSample(0,0,input); sample.setSample(1,0,input); REQUIRE(comb.process(juce::dsp::AudioBlock<float>(sample)));
        REQUIRE(sample.getSample(0,0)==Catch::Approx(expected).margin(2e-5)); REQUIRE(sample.getSample(1,0)==sample.getSample(0,0));
        if(n>=4000) REQUIRE(sample.getSample(0,0)==input);
    }
}

TEST_CASE("Resonant comb extremes and conservative static tail remain finite", "[resonant-comb]")
{
    constexpr double rate=8000;
    for(float delay:{.25f,40.f}) for(float g:{-.95f,0.f,.95f}) for(float hz:{20.f,20000.f}) for(bool damped:{false,true})
    {
        CAPTURE(delay,g,hz,damped);
        ResonantCombSettings s; s.delayMs=delay; s.feedback=g; s.feedforward=1; s.mix=1; s.dampingHz=hz; s.dampingEnabled=damped;
        ResonantComb comb; REQUIRE(comb.setParameters(s)); REQUIRE(comb.prepare({rate,127,1}));
        juce::AudioBuffer<float> b(1,1024);
        const float envelope=std::numeric_limits<float>::max()/128;
        for(int pass=0;pass<10;++pass)
        {
            for(int n=0;n<1024;++n) b.setSample(0,n,pass%2?envelope:-envelope);
            REQUIRE(comb.process(juce::dsp::AudioBlock<float>(b)));
            for(int n=0;n<1024;++n) { REQUIRE(std::isfinite(b.getSample(0,n))); REQUIRE(std::abs(static_cast<double>(b.getSample(0,n)))<=40.*envelope); }
        }
        comb.reset(); b.clear(); b.setSample(0,0,1);
        const double tail=comb.getTailSeconds(); REQUIRE(std::isfinite(tail)); REQUIRE(tail>=0); REQUIRE(tail<120);
        const auto frames=static_cast<std::size_t>(std::ceil((tail+.2)*rate));
        std::size_t offset=0;
        while(offset<frames)
        {
            REQUIRE(comb.process(juce::dsp::AudioBlock<float>(b)));
            for(int n=0;n<1024;++n) if(offset+static_cast<std::size_t>(n)>static_cast<std::size_t>(std::ceil(tail*rate))) REQUIRE(std::abs(b.getSample(0,n))<=1e-6f);
            offset+=1024; b.clear();
        }
    }
}
