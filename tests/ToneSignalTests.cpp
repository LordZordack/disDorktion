#include "dsp/Tone.h"
#include <catch2/catch_test_macros.hpp>
using namespace disdorktion;
TEST_CASE("Tone sweeps sine and transient signals agree with independent difference equation", "[tone]")
{
    constexpr double pi=3.14159265358979323846;
    for(double fs:{8000.,44100.,48000.,88200.,96000.,192000.,384000.})for(float db:{-6.f,0.f,6.f})
    {
        Tone dsp;REQUIRE(dsp.setParameters({db,false}));REQUIRE(dsp.prepare({fs,64,1}));
        const double g=std::pow(10.,db/20.),k=std::tan(pi*1000/fs);
        const double b0=(g+k)/(1+g*k),b1=(k-g)/(1+g*k),a1=(g*k-1)/(1+g*k);
        for(int signal=0;signal<3;++signal)
        {
            dsp.reset();const int count=static_cast<int>(fs*.05);
            juce::AudioBuffer<float> b(1,count);std::vector<double> expected;expected.reserve(count);
            double previousInput=0,previousOutput=0,phase=0;
            for(int n=0;n<count;++n)
            {
                phase+=2*pi*(20*std::pow(fs*.4/20.,double(n)/count))/fs;
                const float x=signal==0?float(.3*std::sin(2*pi*997*n/fs)):signal==1?float(.3*std::sin(phase)):float(n<count/3?.7:-.7);
                b.setSample(0,n,x);const double y=b0*x+b1*previousInput-a1*previousOutput;
                expected.push_back(y);previousInput=x;previousOutput=y;
            }
            REQUIRE(dsp.process(juce::dsp::AudioBlock<float>(b)));
            for(int n=0;n<count;++n)REQUIRE(std::abs(b.getSample(0,n)-expected[n])<1e-6);
        }
    }
}
