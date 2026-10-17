#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/FullChain.h"
#include <limits>

using namespace disdorktion;
namespace
{
void source(juce::AudioBuffer<float>& b)
{
    for (int c=0;c<b.getNumChannels();++c)
        for (int n=0;n<b.getNumSamples();++n)
            b.setSample(c,n,static_cast<float>(.2*std::sin(.071*n+.4*c)));
}
}
TEST_CASE("Full chain dry is delayed before drive and final gain affects both branches", "[full-chain]")
{
    for (double rate : {8000.,44100.,48000.,96000.,192000.,384000.})
        for (unsigned channels : {1u,2u})
        {
            FullChain chain; FullChainSettings s; s.mix=0; s.input.gain.gainDb=24;
            s.finalGain.gainDb=-6; REQUIRE(chain.setParameters(s)); REQUIRE(chain.prepare({rate,7,channels}));
            const auto delay=chain.getLatencySamples(); REQUIRE(delay>0);
            juce::AudioBuffer<float> audio(static_cast<int>(channels),1031), original(static_cast<int>(channels),1031);
            source(audio); original.makeCopyOf(audio); REQUIRE(chain.process(juce::dsp::AudioBlock<float>(audio)));
            const auto g=std::pow(10.,-6./20.);
            for(int c=0;c<audio.getNumChannels();++c) for(int n=0;n<audio.getNumSamples();++n)
                REQUIRE(audio.getSample(c,n)==Catch::Approx(n<static_cast<int>(delay)?0.:original.getSample(c,n-static_cast<int>(delay))*g).margin(2e-8));
            REQUIRE(chain.getPeaks().postInputGain>1.f);
        }
}
TEST_CASE("Full chain wet matches ordered accepted sections for every stage bypass combination", "[full-chain]")
{
    for(unsigned bits=0;bits<64;++bits)
    {
        FullChainSettings s; s.input.gain={6,false,(bits&1)!=0};
        s.input.crusher.bitDepth=8;s.input.crusher.captureRateHz=14000;s.input.crusher.bypass=(bits&2)!=0;
        s.middle.tanh={8,-2,(bits&4)!=0};s.middle.folder={6,.6f,-3,(bits&8)!=0};
        s.output.highPass={180,(bits&16)!=0,FilterResponse::chebyshev1};
        s.output.lowPass={8000,(bits&32)!=0,FilterResponse::chebyshev2};s.finalGain.gainDb=-3;
        FullChain chain;InputSection input;MiddleSection middle;OutputSection output;Gain gain;
        const juce::dsp::ProcessSpec spec{48000,31,2};
        REQUIRE(chain.setParameters(s));REQUIRE(chain.prepare(spec));
        REQUIRE(input.setParameters(s.input));REQUIRE(input.prepare(spec));
        REQUIRE(middle.setParameters(s.middle));REQUIRE(middle.prepare(spec,4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
        REQUIRE(output.setParameters(s.output));REQUIRE(output.prepare(spec));
        REQUIRE(gain.setParameters(s.finalGain));REQUIRE(gain.prepare(spec));
        juce::AudioBuffer<float> audio(2,1031), expected(2,1031);source(audio);expected.makeCopyOf(audio);
        auto reference=juce::dsp::AudioBlock<float>(expected);
        REQUIRE(input.process(reference));REQUIRE(middle.process(reference));REQUIRE(output.process(reference));REQUIRE(gain.process(reference));
        REQUIRE(chain.process(juce::dsp::AudioBlock<float>(audio)));
        REQUIRE(chain.getLatencySamples()==middle.getLatencySamples());
        for(int c=0;c<2;++c)for(int n=0;n<1031;++n) REQUIRE(audio.getSample(c,n)==expected.getSample(c,n));
    }
}
TEST_CASE("Full chain mix and bypass ramps are sample continuous across chunk partitions", "[full-chain]")
{
    FullChain whole,split,dry,wet;FullChainSettings s;
    REQUIRE(whole.prepare({48000,17,2}));REQUIRE(split.prepare({48000,1024,2}));
    auto d=s;d.mix=0;REQUIRE(dry.setParameters(d));REQUIRE(dry.prepare({48000,17,2}));
    REQUIRE(wet.prepare({48000,17,2}));
    s.mix=.25f;s.bypass=true;REQUIRE(whole.setParameters(s));REQUIRE(split.setParameters(s));
    juce::AudioBuffer<float>a(2,1031),b(2,1031),dr(2,1031),we(2,1031);source(a);b.makeCopyOf(a);dr.makeCopyOf(a);we.makeCopyOf(a);
    REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a)));
    auto block=juce::dsp::AudioBlock<float>(b);for(std::size_t n=0;n<1031;++n) REQUIRE(split.process(block.getSubBlock(n,1)));
    REQUIRE(dry.process(juce::dsp::AudioBlock<float>(dr)));REQUIRE(wet.process(juce::dsp::AudioBlock<float>(we)));
    for(int c=0;c<2;++c)for(int n=0;n<1031;++n)
    {
        const float t=std::min(1.f,(n+1)/480.f),weight=(1.f-.75f*t)*(1.f-t);
        REQUIRE(a.getSample(c,n)==b.getSample(c,n));
        REQUIRE(a.getSample(c,n)==Catch::Approx(dr.getSample(c,n)*(1.f-weight)+we.getSample(c,n)*weight).margin(1e-7));
    }
}
TEST_CASE("Full chain rejects settings and preparation transactionally and resets dry history", "[full-chain]")
{
    FullChain chain,reference;FullChainSettings s;s.mix=0;
    REQUIRE(chain.setParameters(s));REQUIRE(reference.setParameters(s));
    REQUIRE(chain.prepare({48000,31,1}));REQUIRE(reference.prepare({48000,31,1}));
    for(float bad : {-1.f,1.01f,std::numeric_limits<float>::quiet_NaN()})
    {auto invalid=s;invalid.mix=bad;invalid.input.gain.gainDb=24;REQUIRE_FALSE(chain.setParameters(invalid));}
    REQUIRE_FALSE(chain.prepare({std::numeric_limits<double>::quiet_NaN(),31,1}));
    REQUIRE_FALSE(chain.prepare({48000,0,1}));REQUIRE_FALSE(chain.prepare({48000,31,3}));
    juce::AudioBuffer<float>a(1,1031),b(1,1031);source(a);b.makeCopyOf(a);
    REQUIRE(chain.process(juce::dsp::AudioBlock<float>(a)));REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
    for(int n=0;n<1031;++n)REQUIRE(a.getSample(0,n)==b.getSample(0,n));
    const auto peaks=chain.getPeaks();REQUIRE(chain.process(juce::dsp::AudioBlock<float>(a).getSubBlock(0,0)));
    REQUIRE(chain.getPeaks().output==peaks.output);
    juce::AudioBuffer<float>wrong(2,10);source(wrong);const auto first=wrong.getSample(0,1);
    REQUIRE_FALSE(chain.process(juce::dsp::AudioBlock<float>(wrong)));REQUIRE(wrong.getSample(0,1)==first);
    chain.reset();a.clear();REQUIRE(chain.process(juce::dsp::AudioBlock<float>(a)));REQUIRE(a.getMagnitude(0,1031)==0);
    REQUIRE(chain.prepare({96000,65536,2}));REQUIRE(chain.prepare({8000,1,1}));
}
