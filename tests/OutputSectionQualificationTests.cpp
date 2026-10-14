#include "dsp/HighPass.h"
#include "dsp/LowPass.h"
#include "../tools/OutputSectionReference.h"
#include <catch2/catch_test_macros.hpp>

using namespace disdorktion;
using namespace disdorktion::measurement::output;

TEST_CASE("Output section impulse and complex response match independent BLT cascade", "[output-section][qualification]")
{
    for(double rate:rates) for(auto hp:responses) for(auto lp:responses) for(const auto& pair:cutoffs)
    {
        CAPTURE(rate,static_cast<unsigned>(hp),static_cast<unsigned>(lp),pair[0],pair[1]);
        const OutputSettings settings {{pair[0],false,hp},{pair[1],false,lp}};
        OutputSection section;REQUIRE(section.setParameters(settings));REQUIRE(section.prepare({rate,127,2}));
        juce::AudioBuffer<float> audio(2,impulseFrames);audio.clear();audio.setSample(0,0,1.f);
        REQUIRE(section.process(juce::dsp::AudioBlock<float>(audio)));
        auto reference=cascade(settings,rate);double maximum=0;
        for(int n=0;n<audio.getNumSamples();++n)
            maximum=std::max(maximum,std::abs(double(audio.getSample(0,n))-reference.next(n==0?1.:0.)));
        REQUIRE(maximum<=6e-6);REQUIRE(audio.getMagnitude(1,0,audio.getNumSamples())==0.f);
        for(double f:{std::min(double(pair[0]),rate*.45),std::min(double(pair[1]),rate*.45),rate*.1})
            REQUIRE(std::abs(projection(audio,rate,f)-reference.response(2*pi*f/rate))<=6e-5);
        REQUIRE(section.getLatencySamples()==0);REQUIRE(section.getPostHighPassPeak()<=4.f);
    }
}

TEST_CASE("Output qualification covers all stage bypasses with ordered module agreement", "[output-section][qualification]")
{
    for(double rate:rates) for(auto hp:responses) for(auto lp:responses) for(const auto& pair:cutoffs) for(unsigned bypass=0;bypass<4;++bypass)
    {
        CAPTURE(rate,static_cast<unsigned>(hp),static_cast<unsigned>(lp),pair[0],pair[1],bypass);
        const OutputSettings settings {{pair[0],bool(bypass&1),hp},{pair[1],bool(bypass&2),lp}};
        OutputSection section;HighPass high;LowPass low;
        REQUIRE(section.setParameters(settings));REQUIRE(high.setParameters(settings.highPass));REQUIRE(low.setParameters(settings.lowPass));
        REQUIRE(section.prepare({rate,32,2}));REQUIRE(high.prepare({rate,32,2}));REQUIRE(low.prepare({rate,32,2}));
        juce::AudioBuffer<float> audio(2,2051);
        for(int n=0;n<audio.getNumSamples();++n){audio.setSample(0,n,source(0,n,rate));audio.setSample(1,n,source(2,n,rate));}
        auto reference=audio;const auto dry=audio;
        REQUIRE(section.process(juce::dsp::AudioBlock<float>(audio)));
        REQUIRE(high.process(juce::dsp::AudioBlock<float>(reference)));const float peak=reference.getMagnitude(0,reference.getNumSamples());
        REQUIRE(low.process(juce::dsp::AudioBlock<float>(reference)));
        REQUIRE(section.getPostHighPassPeak()==peak);
        double maximum=0;for(int c=0;c<2;++c)for(int n=0;n<audio.getNumSamples();++n)
        {maximum=std::max(maximum,std::abs(double(audio.getSample(c,n))-reference.getSample(c,n)));if(bypass==3)REQUIRE(audio.getSample(c,n)==dry.getSample(c,n));}
        REQUIRE(maximum<=1e-6);REQUIRE(peak<=4.f);
    }
}

TEST_CASE("Output unit peak sine step burst and noise remain bounded during simultaneous automation", "[output-section][qualification]")
{
    for(double rate:rates)for(unsigned kind=0;kind<4;++kind)
    {
        CAPTURE(rate,kind);
        OutputSection section;HighPass high;LowPass low;
        REQUIRE(section.prepare({rate,127,2}));REQUIRE(high.prepare({rate,127,2}));REQUIRE(low.prepare({rate,127,2}));
        juce::AudioBuffer<float> audio(2,257);double peak=0,interstage=0,error=0;
        for(unsigned block=0;block<160;++block)
        {
            OutputSettings s {{block%2?20.f:20000.f,block%5==0,responses[block%3]},
                              {block%2?20000.f:20.f,block%7==0,responses[(block/3)%3]}};
            REQUIRE(section.setParameters(s));REQUIRE(high.setParameters(s.highPass));REQUIRE(low.setParameters(s.lowPass));
            for(int n=0;n<257;++n)for(int c=0;c<2;++c)audio.setSample(c,n,(c?-.7f:1.f)*source(kind,block*257+n,rate));
            auto ref=audio;REQUIRE(section.process(juce::dsp::AudioBlock<float>(audio)));
            REQUIRE(high.process(juce::dsp::AudioBlock<float>(ref)));interstage=std::max(interstage,double(ref.getMagnitude(0,257)));
            REQUIRE(low.process(juce::dsp::AudioBlock<float>(ref)));
            for(int c=0;c<2;++c)for(int n=0;n<257;++n)
            {const double v=audio.getSample(c,n);REQUIRE(std::isfinite(v));peak=std::max(peak,std::abs(v));error=std::max(error,std::abs(v-ref.getSample(c,n)));}
        }
        REQUIRE(peak<8.);REQUIRE(interstage<=4.);REQUIRE(error<=1e-6);
    }
}

TEST_CASE("Output minimum cutoff and bypass histories decay after two seconds", "[output-section][qualification]")
{
    for(auto hp:responses)for(auto lp:responses)for(unsigned bypass=0;bypass<4;++bypass)
    {
        CAPTURE(static_cast<unsigned>(hp),static_cast<unsigned>(lp),bypass);
        OutputSection section;REQUIRE(section.setParameters({{20,bool(bypass&1),hp},{20,bool(bypass&2),lp}}));
        REQUIRE(section.prepare({384000,32,1}));juce::AudioBuffer<float> audio(1,768000);audio.clear();audio.setSample(0,0,1);
        REQUIRE(section.process(juce::dsp::AudioBlock<float>(audio)));
        REQUIRE(section.setParameters({{20,false,hp},{20,false,lp}}));
        juce::AudioBuffer<float> after(1,3840);after.clear();REQUIRE(section.process(juce::dsp::AudioBlock<float>(after)));
        REQUIRE(after.getMagnitude(0,3840)<1e-6f);
    }
}
