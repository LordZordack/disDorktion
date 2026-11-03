#include "dsp/Tone.h"
#include <catch2/catch_test_macros.hpp>
#include <complex>
#include <limits>
using namespace disdorktion;
namespace {
constexpr double pi=3.14159265358979323846;
std::complex<double> reference(double rate,double db,double frequency)
{
    const double g=std::pow(10.,db/20.);
    const auto s=std::complex<double>(0.,std::tan(pi*frequency/rate)/std::tan(pi*1000./rate));
    return (g*s+1.)/(s+g);
}
}
TEST_CASE("Tone impulse matches independent reciprocal transfer", "[tone]")
{
    for(double rate:{8000.,44100.,48000.,88200.,96000.,192000.,384000.})
    for(float db:{-6.f,-3.f,0.f,3.f,6.f})
    {
        Tone tone; REQUIRE(tone.setParameters({db,false})); REQUIRE(tone.prepare({rate,127,1}));
        const int count=static_cast<int>(std::ceil(.1*rate));
        juce::AudioBuffer<float> b(1,count);b.clear();b.setSample(0,0,1);
        REQUIRE(tone.process(juce::dsp::AudioBlock<float>(b)));
        for(double f:{0.,20.,100.,1000.,rate*.2,rate*.45,rate*.5})
        {
            std::complex<double> actual{};
            for(int n=0;n<count;++n)actual+=double(b.getSample(0,n))*std::polar(1.,-2*pi*f*n/rate);
            auto expected=f==rate*.5?std::complex<double>(std::pow(10.,db/20.),0.):reference(rate,db,f);
            REQUIRE(std::abs(actual-expected)<1e-5);
            REQUIRE(std::abs(20*std::log10(std::abs(actual/expected)))<.05);
        }
        REQUIRE(tone.getLatencySamples()==0);
    }
}
TEST_CASE("Tone contract reset partition and identity survive automation", "[tone]")
{
    for(double rate:{8000.,44100.,48000.,88200.,96000.,192000.,384000.})for(unsigned channels:{1u,2u})
    {
        Tone a,b;REQUIRE(a.prepare({rate,1,channels}));REQUIRE(b.prepare({rate,127,channels}));
        juce::AudioBuffer<float> x(static_cast<int>(channels),4097),y;
        for(float db:{6.f,-6.f,0.f})
        {
            REQUIRE(a.setParameters({db,false}));REQUIRE(b.setParameters({db,false}));
            for(int c=0;c<x.getNumChannels();++c)for(int n=0;n<x.getNumSamples();++n)
                x.setSample(c,n,static_cast<float>(std::sin(n*.37)*(c?-1:1)));
            y=x;REQUIRE(a.process(juce::dsp::AudioBlock<float>(x)));
            auto block=juce::dsp::AudioBlock<float>(y);
            for(std::size_t offset=0;offset<4097;)
            {
                REQUIRE(b.setParameters({db,false})); // identical targets must not restart ramp
                const auto size=std::min<std::size_t>(32,4097-offset);
                REQUIRE(b.process(block.getSubBlock(offset,size)));offset+=size;
            }
            for(int c=0;c<x.getNumChannels();++c)for(int n=0;n<x.getNumSamples();++n)
            {REQUIRE(std::isfinite(x.getSample(c,n)));REQUIRE(std::abs(x.getSample(c,n))<8);REQUIRE(x.getSample(c,n)==y.getSample(c,n));}
        }
        a.reset();x.clear();REQUIRE(a.process(juce::dsp::AudioBlock<float>(x)));REQUIRE(x.getMagnitude(0,4097)==0);
        REQUIRE_FALSE(a.setParameters({std::numeric_limits<float>::quiet_NaN(),false}));
        REQUIRE_FALSE(a.prepare({7999,32,channels}));
        REQUIRE(a.process(juce::dsp::AudioBlock<float>(x).getSubBlock(0,0)));
        REQUIRE(a.setParameters({0,false}));a.reset();
        for(int c=0;c<x.getNumChannels();++c)for(int n=0;n<4097;++n)x.setSample(c,n,float(n%7)*.1f);
        y=x;REQUIRE(a.process(juce::dsp::AudioBlock<float>(x)));
        for(int c=0;c<x.getNumChannels();++c)for(int n=0;n<4097;++n)REQUIRE(x.getSample(c,n)==y.getSample(c,n));
    }
}
