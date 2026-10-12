#include "dsp/HighPass.h"
#include "dsp/LowPass.h"
#include "../tools/CutoffFilterAcceptance.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <complex>
#include <numbers>
#include <limits>
#include <vector>
#include <array>

using namespace disdorktion;
TEST_CASE("Filter CPU diagnostic rejects budget boundaries and incomplete measurements", "[cutoff-filter]")
{
    using measurement::cutoffCpuWithinBudget;
    REQUIRE(cutoffCpuWithinBudget(.049,.049,288));
    REQUIRE_FALSE(cutoffCpuWithinBudget(.05,.01,288));
    REQUIRE_FALSE(cutoffCpuWithinBudget(.01,.10,288));
    REQUIRE_FALSE(cutoffCpuWithinBudget(.01,.01,287));
    REQUIRE_FALSE(cutoffCpuWithinBudget(std::numeric_limits<double>::quiet_NaN(),.01,288));
    REQUIRE_FALSE(cutoffCpuWithinBudget(.01,-.01,288));
}
namespace
{
constexpr double pi = std::numbers::pi;
constexpr std::array responses {FilterResponse::butterworth, FilterResponse::chebyshev1, FilterResponse::chebyshev2};

// Independent analog transfer function evaluated through the bilinear mapping.
// Chebyshev expectations use polynomial magnitudes, not DSP prototype constants.
double magnitude(bool hp, FilterResponse mode, double rate, double cutoff, double frequency)
{
    const double ratio = std::tan(pi * frequency / rate) / std::tan(pi * std::min(cutoff, .45 * rate) / rate);
    const double u = hp ? 1.0 / ratio : ratio;
    if (mode == FilterResponse::butterworth) return 1.0 / std::sqrt(1.0 + u*u*u*u);
    if (mode == FilterResponse::chebyshev1)
    {
        const double e = std::sqrt(std::pow(10., .1) - 1.);
        const double w = u * std::sqrt((1. + 1./e)/2.);
        const double t = 2.*w*w-1.;
        return 1./std::sqrt(1.+e*e*t*t);
    }
    const double e = 1./std::sqrt(9999.);
    const double w = u * std::sqrt(2./(1.+1./e));
    const double t = 2./(w*w)-1.;
    return std::abs(e*t) / std::sqrt(1.+e*e*t*t);
}

struct Oracle
{
    double b0,b1,b2,a1,a2, x1=0,x2=0,y1=0,y2=0;
    double next(double x)
    {
        const double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;
        x2=x1; x1=x; y2=y1; y1=y; return y;
    }
    std::complex<double> response(double angle) const
    {
        const auto z=std::polar(1.,-angle);
        return (b0+b1*z+b2*z*z)/(1.+a1*z+a2*z*z);
    }
};
Oracle oracle(bool hp, FilterResponse mode, double rate, double cutoff)
{
    // Build s-plane coefficients from complex prototype poles/zeros, then BLT.
    // Deliberately a direct-form recurrence rather than a state-variable filter.
    double n2=0,n0=1,d1=std::sqrt(2.),d0=1;
    if (mode != FilterResponse::butterworth)
    {
        const double e=mode==FilterResponse::chebyshev1 ? std::sqrt(std::pow(10.,.1)-1.) : 1./std::sqrt(9999.);
        const double mu=std::asinh(1./e)/2.;
        auto p=-std::sinh(std::complex<double>(mu,pi/4.));
        if (mode==FilterResponse::chebyshev2) p=1./p;
        const double w3=mode==FilterResponse::chebyshev1 ? std::sqrt((1.+1./e)/2.) : std::sqrt(2./(1.+1./e));
        p/=w3; d1=-2.*p.real(); d0=std::norm(p);
        if (mode==FilterResponse::chebyshev1) n0=d0*std::pow(10.,-.05);
        else { n2=.01; n0=d0; }
    }
    // s -> 1/s swaps constant and quadratic coefficients in numerator/denominator.
    if(hp) std::swap(n2,n0);
    const double d2=hp ? d0 : 1.;
    const double dc=hp ? 1. : d0;
    const double g=std::tan(pi*std::min(cutoff,.45*rate)/rate);
    const double scale=d2+d1*g+dc*g*g;
    return {(n2+n0*g*g)/scale, 2.*(n0*g*g-n2)/scale,(n2+n0*g*g)/scale,
        2.*(dc*g*g-d2)/scale,(d2-d1*g+dc*g*g)/scale};
}

template<class F> void impulseCheck(bool hp)
{
    for(double rate : {8000.,44100.,48000.,96000.,192000.,384000.})
    for(float cutoff : {20.f,1000.f,20000.f})
    for(auto mode:responses)
    {
        CAPTURE(hp,rate,cutoff,static_cast<int>(mode));
        F dsp; REQUIRE(dsp.setParameters({cutoff,false,mode})); REQUIRE(dsp.prepare({rate,32,2}));
        juce::AudioBuffer<float> audio(2,131072); audio.clear(); audio.setSample(0,0,1.f);
        auto ref=oracle(hp,mode,rate,cutoff);
        REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
        double error=0.;
        for(int n=0;n<audio.getNumSamples();++n)
        {
            error=std::max(error,std::abs(audio.getSample(0,n)-ref.next(n==0?1.:0.)));
            REQUIRE(audio.getSample(1,n)==0.f);
        }
        REQUIRE(error<3e-6);
        const double effective=std::min(static_cast<double>(cutoff),.45*rate);
        for(double f:{effective*.1,effective*.5,effective,effective*2.,effective*8.,rate*.49})
        {
            if(f>=rate*.5) continue;
            std::complex<double> measured=0.;
            const auto step=std::polar(1.,-2.*pi*f/rate); std::complex<double> phase=1.;
            for(int n=0;n<audio.getNumSamples();++n) { measured+=static_cast<double>(audio.getSample(0,n))*phase; phase*=step; }
            const auto expected=ref.response(2.*pi*f/rate);
            REQUIRE(std::abs(measured-expected)<3e-5);
            REQUIRE(std::abs(std::abs(measured)-magnitude(hp,mode,rate,cutoff,f))<3e-5);
        }
        REQUIRE(dsp.getLatencySamples()==0);
    }
}
template<class F> void contractCheck()
{
    F dsp,split;
    REQUIRE_FALSE(dsp.setParameters({19.f,false,FilterResponse::butterworth}));
    REQUIRE_FALSE(dsp.setParameters({20001.f,false,FilterResponse::butterworth}));
    REQUIRE_FALSE(dsp.setParameters({std::numeric_limits<float>::quiet_NaN(),true,FilterResponse::chebyshev1}));
    REQUIRE_FALSE(dsp.setParameters({1000.f,false,static_cast<FilterResponse>(99)}));
    juce::AudioBuffer<float> audio(2,2051); audio.clear(); audio.setSample(0,0,1.f);
    REQUIRE_FALSE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
    REQUIRE(audio.getSample(0,0)==1.f);
    REQUIRE(dsp.setParameters({20000.f,false,FilterResponse::chebyshev2}));
    REQUIRE(dsp.prepare({8000,32,2}));
    REQUIRE(dsp.getEffectiveCutoffHz()==3600.);
    REQUIRE_FALSE(dsp.prepare({0,1,1}));
    REQUIRE(dsp.getEffectiveCutoffHz()==3600.);
    REQUIRE(dsp.prepare({48000,32,2})); REQUIRE(dsp.getEffectiveCutoffHz()==20000.);
    REQUIRE(split.setParameters({20000.f,false,FilterResponse::chebyshev2})); REQUIRE(split.prepare({48000,32,2}));
    const auto original=audio; auto divided=audio;
    REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio).getSubBlock(0,0)));
    juce::AudioBuffer<float> wrong(1,3); wrong.clear(); REQUIRE_FALSE(dsp.process(juce::dsp::AudioBlock<float>(wrong)));
    REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
    for(std::size_t n=0;n<2051;)
    { auto size=std::min<std::size_t>(17,2051-n); REQUIRE(split.process(juce::dsp::AudioBlock<float>(divided).getSubBlock(n,size))); n+=size; }
    for(int n=0;n<2051;++n) REQUIRE(audio.getSample(0,n)==divided.getSample(0,n));
    dsp.reset(); auto repeated=original; REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(repeated)));
    for(int n=0;n<2051;++n) REQUIRE(repeated.getSample(0,n)==audio.getSample(0,n));
    dsp.reset(); audio.clear(); REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(audio.getMagnitude(0,2051)==0.f);
}
template<class F> void automationCheck()
{
    for(double rate:{44100.,48000.,96000.,192000.})
    for(unsigned capacity:{32u,64u,128u,256u,512u,1024u})
    for(unsigned channels:{1u,2u})
    {
        CAPTURE(rate,capacity,channels);
        F dsp; REQUIRE(dsp.prepare({rate,capacity,channels}));
        juce::AudioBuffer<float> audio(static_cast<int>(channels),static_cast<int>(capacity));
        double peak=0.;
        for(int block=0;block<80;++block)
        {
            REQUIRE(dsp.setParameters({block%2 ? 20.f : 20000.f,block%5==0,responses[block%3]}));
            for(unsigned n=0;n<capacity;++n) for(unsigned c=0;c<channels;++c)
                audio.setSample(static_cast<int>(c),static_cast<int>(n),static_cast<float>(std::sin(2*pi*997*(block*capacity+n)/rate)));
            REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio)));
            for(unsigned n=0;n<capacity;++n)
            {
                const auto x=audio.getSample(0,static_cast<int>(n)); REQUIRE(std::isfinite(x)); peak=std::max(peak,static_cast<double>(std::abs(x)));
                if(channels==2) REQUIRE(x==audio.getSample(1,static_cast<int>(n)));
            }
        }
        REQUIRE(peak<8.);
    }
}
}
TEST_CASE("High pass impulse and warped response match independent biquads", "[cutoff-filter]") { impulseCheck<HighPass>(true); }
TEST_CASE("Low pass impulse and warped response match independent biquads", "[cutoff-filter]") { impulseCheck<LowPass>(false); }
TEST_CASE("Both cutoff modules preserve contract and clamp retained requests", "[cutoff-filter]") { contractCheck<HighPass>(); contractCheck<LowPass>(); }
TEST_CASE("All response modes tolerate rapid cutoff mode and bypass changes", "[cutoff-filter]") { automationCheck<HighPass>(); automationCheck<LowPass>(); }

TEST_CASE("Cutoff bypass is exact and preserves warm wet histories", "[cutoff-filter]")
{
    for(bool hp:{false,true})
    {
        HighPass high,highWet; LowPass low,lowWet;
        auto run=[&](auto& dsp,auto& wet)
        {
            REQUIRE(dsp.setParameters({1000,true,FilterResponse::chebyshev1})); REQUIRE(wet.setParameters({1000,false,FilterResponse::chebyshev1}));
            REQUIRE(dsp.prepare({48000,1,1})); REQUIRE(wet.prepare({48000,1,1}));
            juce::AudioBuffer<float> audio(1,960); for(int n=0;n<960;++n) audio.setSample(0,n,static_cast<float>(std::sin(n*.1)));
            auto reference=audio; auto dry=audio;
            REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(wet.process(juce::dsp::AudioBlock<float>(reference)));
            for(int n=0;n<960;++n) REQUIRE(audio.getSample(0,n)==dry.getSample(0,n));
            REQUIRE(dsp.setParameters({1000,false,FilterResponse::chebyshev1}));
            audio=dry; reference=dry; REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(audio))); REQUIRE(wet.process(juce::dsp::AudioBlock<float>(reference)));
            for(int n=480;n<960;++n) REQUIRE(audio.getSample(0,n)==reference.getSample(0,n));
        };
        if(hp)run(high,highWet);else run(low,lowWet);
    }
}

TEST_CASE("Interrupted response fades match a unified partition of unity", "[cutoff-filter]")
{
    auto check=[]<class F>()
    {
        F changing; std::array<F,3> reference;
        REQUIRE(changing.setParameters({1000,false,FilterResponse::butterworth})); REQUIRE(changing.prepare({48000,1,1}));
        for(unsigned i=0;i<3;++i) {REQUIRE(reference[i].setParameters({1000,false,responses[i]}));REQUIRE(reference[i].prepare({48000,1,1}));}
        std::array<double,3> weights {1,0,0},start=weights;
        unsigned elapsed=480,target=0;
        juce::AudioBuffer<float> sample(1,1),other(1,1);
        for(unsigned n=0;n<4000;++n)
        {
            if(n%173==0) {target=(target+1)%3;start=weights;elapsed=0;REQUIRE(changing.setParameters({1000,false,responses[target]}));}
            elapsed=std::min(elapsed+1,480u);double sum=0;
            for(unsigned i=0;i<3;++i) {weights[i]=start[i]+static_cast<double>(elapsed)/480.*((i==target?1.:0.)-start[i]);sum+=weights[i];}
            REQUIRE(std::abs(sum-1.)<1e-12);
            const auto dry=static_cast<float>(std::sin(n*.13));sample.setSample(0,0,dry);
            REQUIRE(changing.process(juce::dsp::AudioBlock<float>(sample)));
            double expected=0;
            for(unsigned i=0;i<3;++i) {other.setSample(0,0,dry);REQUIRE(reference[i].process(juce::dsp::AudioBlock<float>(other)));expected+=weights[i]*other.getSample(0,0);}
            REQUIRE(std::abs(sample.getSample(0,0)-expected)<2e-7);
        }
    };
    check.operator()<HighPass>();check.operator()<LowPass>();
}

TEST_CASE("Slow cutoff tails decay and block partition preserves moving controls", "[cutoff-filter]")
{
    auto check=[]<class F>()
    {
        for(auto mode:responses)
        {
            F dsp;REQUIRE(dsp.setParameters({20,false,mode}));REQUIRE(dsp.prepare({384000,32,1}));
            juce::AudioBuffer<float> tail(1,768000);tail.clear();tail.setSample(0,0,1);
            REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(tail)));REQUIRE(tail.getMagnitude(767000,1000)<1e-6f);
        }
        F whole,parts;REQUIRE(whole.prepare({48000,32,1}));REQUIRE(parts.prepare({48000,32,1}));
        juce::AudioBuffer<float> a(1,1027),b(1,1027);
        for(int n=0;n<1027;++n)a.setSample(0,n,static_cast<float>(std::sin(n*.2)));b=a;
        REQUIRE(whole.setParameters({125,false,FilterResponse::chebyshev1}));REQUIRE(parts.setParameters({125,false,FilterResponse::chebyshev1}));
        REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a)));
        for(std::size_t n=0;n<1027;) {auto size=std::min<std::size_t>(13,1027-n);REQUIRE(parts.process(juce::dsp::AudioBlock<float>(b).getSubBlock(n,size)));n+=size;}
        for(int n=0;n<1027;++n) REQUIRE(a.getSample(0,n)==b.getSample(0,n));
        static_assert(Module<F,CutoffSettings>);
    };
    check.operator()<HighPass>();check.operator()<LowPass>();
}

TEST_CASE("Cutoff smoothing lasts ten milliseconds and unchanged targets do not restart it", "[cutoff-filter]")
{
    auto check=[]<class F>(bool high)
    {
        for(double rate:{48000.,96000.})
        {
            F filter;REQUIRE(filter.prepare({rate,1,1}));
            juce::dsp::StateVariableTPTFilter<double> reference;
            reference.prepare({rate,1,1});reference.setType(high ? juce::dsp::StateVariableTPTFilterType::highpass : juce::dsp::StateVariableTPTFilterType::lowpass);
            reference.setResonance(1./std::sqrt(2.));
            const double start=std::tan(pi*(high?20.:20000.)/rate),target=std::tan(pi*125./rate);
            const int frames=static_cast<int>(rate*.010);
            juce::AudioBuffer<float> audio(1,1);
            for(int n=0;n<frames+256;++n)
            {
                REQUIRE(filter.setParameters({125,false,FilterResponse::butterworth}));
                const double g=n+1>=frames ? target : start*std::pow(target/start,static_cast<double>(n+1)/frames);
                reference.setCutoffFrequency(rate/pi*std::atan(g));
                const auto dry=static_cast<float>(.5*std::sin(n*.131));audio.setSample(0,0,dry);
                const double expected=reference.processSample(0,dry);
                REQUIRE(filter.process(juce::dsp::AudioBlock<float>(audio)));
                REQUIRE(std::abs(audio.getSample(0,0)-expected)<1e-6);
            }
        }
    };
    check.operator()<HighPass>(true);check.operator()<LowPass>(false);
}
