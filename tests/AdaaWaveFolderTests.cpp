#include "dsp/AdaaWaveFolder.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <vector>

namespace
{
double triangleCell(double q)
{
    const auto magnitude=std::abs(q),cell=std::floor((magnitude+1.)/2.);
    return (std::signbit(q)?-1.:1.)*(magnitude-2*cell)*(std::fmod(cell,2.)==0?1.:-1.);
}
// Independent exact trapezoids between unwrapped triangle corners.
double intervalAverage(double x,double previous,double drive,double threshold)
{
    const auto a=drive*x/threshold,b=drive*previous/threshold;
    if(a==b) return threshold*triangleCell(a);
    const auto right=std::max(a,b); auto left=std::min(a,b); double integral=0;
    while(left<right)
    {
        const auto corner=2*std::floor((left+1)/2)+1;
        const auto next=std::min(right,corner);
        if(next<=left) return std::numeric_limits<double>::quiet_NaN();
        integral+=.5*(triangleCell(left)+triangleCell(next))*(next-left);
        left=next;
    }
    return threshold*integral/std::abs(a-b);
}
}

TEST_CASE("ADAA folder matches an independent corner-split integration oracle", "[adaa]")
{
    const std::vector<float> input {0,.1f,.2f,.25f,.2500001f,.75f,1.f,1.0000001f,-1.f,-.75f,-.25f,-.2500001f,0,2,-2,2,2,2};
    for(float driveDb : {0.f,12.f,36.f}) for(float threshold : {1.f,.25f,.05f})
    {
        disdorktion::AdaaWaveFolder folder;
        REQUIRE(folder.setParameters({driveDb,threshold,0,false})); REQUIRE(folder.prepareOversampled({48000,3,2},4));
        juce::AudioBuffer<float> b(2,static_cast<int>(input.size()));
        for(std::size_t n=0;n<input.size();++n) { b.setSample(0,static_cast<int>(n),input[n]); b.setSample(1,static_cast<int>(n),-input[n]); }
        REQUIRE(folder.process(juce::dsp::AudioBlock<float>(b)));
        double previous=0,r1=0,r2=0;
        for(std::size_t n=0;n<input.size();++n)
        {
            const auto raw=intervalAverage(input[n],previous,std::pow(10.,static_cast<double>(driveDb)/20),threshold);
            const auto expected=-.125*raw+1.25*r1-.125*r2;
            REQUIRE(std::isfinite(expected));
            REQUIRE(b.getSample(0,static_cast<int>(n))==Catch::Approx(expected).margin(2e-6));
            REQUIRE(b.getSample(1,static_cast<int>(n))==Catch::Approx(-expected).margin(2e-6));
            previous=input[n]; r2=r1; r1=raw;
        }
    }
}

TEST_CASE("ADAA bypass has the declared compensated moving-average response", "[adaa]")
{
    disdorktion::AdaaWaveFolder folder; REQUIRE(folder.setParameters({36,.05f,12,true}));
    REQUIRE(folder.prepareOversampled({48000,1,1},4));
    juce::AudioBuffer<float> b(1,8); b.clear(); b.setSample(0,0,1);
    REQUIRE(folder.process(juce::dsp::AudioBlock<float>(b)));
    for(int n=0;n<8;++n)
    {
        const auto expected=n==0 || n==3 ? -.0625 : n==1 || n==2 ? .5625 : 0.;
        REQUIRE(b.getSample(0,n)==expected);
    }
    REQUIRE(folder.getLatencyInInternalSamples()==1.5);
}

TEST_CASE("ADAA reset chunks smoothing and failed preparation preserve state", "[adaa]")
{
    disdorktion::AdaaWaveFolder whole,split;
    REQUIRE(whole.prepareOversampled({48000,127,2},4)); REQUIRE(split.prepareOversampled({48000,127,2},4));
    REQUIRE(whole.setParameters({12,.25f,-6,false})); REQUIRE(split.setParameters({12,.25f,-6,false}));
    juce::AudioBuffer<float> a(2,4001),b(2,4001);
    for(int n=0;n<4001;++n) { a.setSample(0,n,.25f*std::sin(.1f*n)); a.setSample(1,n,0); }
    b.makeCopyOf(a);
    REQUIRE_FALSE(whole.prepareOversampled({4000,127,2},4)); REQUIRE_FALSE(whole.setParameters({37,1,0,false}));
    REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a)));
    for(std::size_t n=0;n<4001;n+=31) REQUIRE(split.process(juce::dsp::AudioBlock<float>(b).getSubBlock(n,std::min<std::size_t>(31,4001-n))));
    for(int c=0;c<2;++c) for(int n=0;n<4001;++n) { REQUIRE(a.getSample(c,n)==b.getSample(c,n)); REQUIRE(std::isfinite(a.getSample(c,n))); }
    whole.reset(); split.reset();
    for(int n=0;n<4001;++n) { a.setSample(0,n,.1f); a.setSample(1,n,0); }
    b.makeCopyOf(a); REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(split.process(juce::dsp::AudioBlock<float>(b)));
    for(int n=0;n<4001;++n) REQUIRE(a.getSample(0,n)==b.getSample(0,n));
}

TEST_CASE("ADAA scaled smoothing and compensation follow an independent control oracle", "[adaa]")
{
    const int ramp=1920;
    disdorktion::AdaaWaveFolder folder; REQUIRE(folder.prepareOversampled({48000,127,1},4));
    REQUIRE(folder.setParameters({12,.25f,-6,true}));
    juce::AudioBuffer<float> b(1,ramp+3);
    for(int n=0;n<ramp+3;++n) b.setSample(0,n,.25f);
    REQUIRE(folder.process(juce::dsp::AudioBlock<float>(b)));
    double previous=0,r1=0,r2=0;
    for(int n=0;n<ramp+3;++n)
    {
        const auto u=std::min(1.,static_cast<double>(n+1)/ramp);
        const auto a=1.+(std::pow(10.,12./20)-1.)*u,t=1.-.75*u,g=1.+(std::pow(10.,-6./20)-1.)*u;
        const auto dry=.5*(.25+previous),wet=g*intervalAverage(.25,previous,a,t);
        const auto raw=u==1 ? dry : u*dry+(1.-u)*wet;
        const auto expected=-.125*raw+1.25*r1-.125*r2;
        REQUIRE(b.getSample(0,n)==Catch::Approx(expected).margin(2e-6));
        previous=.25; r2=r1; r1=raw;
    }
    REQUIRE(b.getSample(0,ramp+1)==.25f);
}

TEST_CASE("ADAA handles large complete-cycle jumps tiny steps and silent channels", "[adaa]")
{
    disdorktion::AdaaWaveFolder folder; REQUIRE(folder.setParameters({36,.05f,12,false}));
    REQUIRE(folder.prepareOversampled({384000,65536,2},16));
    juce::AudioBuffer<float> b(2,10); b.clear();
    const std::array<float,10> input {1,-1,1000,1001,1e20f,-1e20f,std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),.05f,std::nextafter(.05f,1.f)};
    for(int n=0;n<10;++n) b.setSample(0,n,input[static_cast<std::size_t>(n)]);
    REQUIRE(folder.process(juce::dsp::AudioBlock<float>(b)));
    for(int n=0;n<10;++n) { REQUIRE(std::isfinite(b.getSample(0,n))); REQUIRE(b.getSample(1,n)==0); REQUIRE(std::abs(b.getSample(0,n))<=.3f); }
}
