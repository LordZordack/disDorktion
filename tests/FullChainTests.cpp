#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/FullChain.h"
#include <limits>

using namespace disdorktion;
TEST_CASE("Driver automation cancels through aligned neutral global branches and retains input gain", "[full-chain][driver]")
{
    for (unsigned channels : {1u,2u})
    for (float globalMix : {0.f,.5f,1.f})
    {
        FullChain chain;
        FullChainSettings s; s.mix=globalMix; s.input.gain.gainDb=6; s.driver=1;
        s.input.crusher.mix=0; s.middle.mix=0;
        REQUIRE(chain.setParameters(s)); REQUIRE(chain.prepare({48000,31,channels}));
        const auto latency=chain.getLatencySamples();
        juce::AudioBuffer<float> audio(static_cast<int>(channels),4096);
        for(int c=0;c<audio.getNumChannels();++c)
            for(int n=0;n<audio.getNumSamples();++n) audio.setSample(c,n,.1f*(c+1));
        auto block=juce::dsp::AudioBlock<float>(audio);
        for(std::size_t n=0;n<4096;)
        {
            s.driver=(n/37)%2 ? 4.f : 1.f;
            REQUIRE(chain.setParameters(s));
            const auto count=std::min<std::size_t>(37,4096-n);
            REQUIRE(chain.process(block.getSubBlock(n,count))); n+=count;
        }
        for(int c=0;c<audio.getNumChannels();++c)
            for(int n=0;n<audio.getNumSamples();++n)
                REQUIRE(audio.getSample(c,n)==Catch::Approx(n<static_cast<int>(latency)?0.f:.1f*(c+1)*std::pow(10.f,.3f)).margin(1e-7));
        s.driver=.99f; REQUIRE_FALSE(chain.setParameters(s));
        s.driver=4.01f; REQUIRE_FALSE(chain.setParameters(s));
        s.driver=std::numeric_limits<float>::quiet_NaN(); REQUIRE_FALSE(chain.setParameters(s));
    }
}
namespace
{
void source(juce::AudioBuffer<float>& b)
{
    for (int c=0;c<b.getNumChannels();++c)
        for (int n=0;n<b.getNumSamples();++n)
            b.setSample(c,n,static_cast<float>(.2*std::sin(.071*n+.4*c)));
}
}
TEST_CASE("Full chain dry includes input gain and final gain affects both branches", "[full-chain]")
{
    for (double rate : {8000.,44100.,48000.,96000.,192000.,384000.})
        for (unsigned channels : {1u,2u})
        {
            FullChain chain; FullChainSettings s; s.mix=0; s.input.gain.gainDb=24;
            s.finalGain.gainDb=-6; REQUIRE(chain.setParameters(s)); REQUIRE(chain.prepare({rate,7,channels}));
            const auto delay=chain.getLatencySamples(); REQUIRE(delay>0);
            juce::AudioBuffer<float> audio(static_cast<int>(channels),1031), original(static_cast<int>(channels),1031);
            source(audio); original.makeCopyOf(audio); REQUIRE(chain.process(juce::dsp::AudioBlock<float>(audio)));
            const auto g=std::pow(10.,18./20.);
            for(int c=0;c<audio.getNumChannels();++c) for(int n=0;n<audio.getNumSamples();++n)
                REQUIRE(audio.getSample(c,n)==Catch::Approx(n<static_cast<int>(delay)?0.:original.getSample(c,n-static_cast<int>(delay))*g).margin(2e-7));
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
    for(bool expanded:{false,true})
    {
    CAPTURE(expanded);
    FullChain whole,split,dry,wet;FullChainSettings s;
    if(expanded)
    {
        s.output.comb={5.f,.6f,.9f,1200.f,.8f,true,false};
        s.output.dispersion={350.f,.7f,false}; s.output.tone={6.f,false};
    }
    REQUIRE(whole.setParameters(s)); REQUIRE(split.setParameters(s)); REQUIRE(wet.setParameters(s));
    REQUIRE(whole.prepare({48000,17,2}));REQUIRE(split.prepare({48000,1024,2}));
    auto d=s;d.mix=0;REQUIRE(dry.setParameters(d));REQUIRE(dry.prepare({48000,17,2}));
    REQUIRE(wet.prepare({48000,17,2}));
    REQUIRE(whole.getLatencySamples()==92); REQUIRE(split.getLatencySamples()==92);
    REQUIRE(dry.getLatencySamples()==92); REQUIRE(wet.getLatencySamples()==92);
    s.mix=.25f;s.bypass=true;REQUIRE(whole.setParameters(s));REQUIRE(split.setParameters(s));
    juce::AudioBuffer<float>a(2,1031),b(2,1031),dr(2,1031),we(2,1031);source(a);b.makeCopyOf(a);dr.makeCopyOf(a);we.makeCopyOf(a);
    REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a)));
    auto block=juce::dsp::AudioBlock<float>(b);for(std::size_t n=0;n<1031;++n) REQUIRE(split.process(block.getSubBlock(n,1)));
    REQUIRE(dry.process(juce::dsp::AudioBlock<float>(dr)));REQUIRE(wet.process(juce::dsp::AudioBlock<float>(we)));
    for(int c=0;c<2;++c)for(int n=0;n<1031;++n)
    {
        const float t=std::min(1.f,(n+1)/480.f),weight=(1.f-.75f*t)*(1.f-t);
        REQUIRE(a.getSample(c,n)==b.getSample(c,n));
        // The independent ideal ramp differs from JUCE's repeated float step
        // accumulation by at most 3.13e-6 in blend weight over these 480 frames.
        // Its audio error scales with wet-dry separation, which active output
        // effects increase. Keep the partition comparison above bit-exact.
        const auto drySample=dr.getSample(c,n),wetSample=we.getSample(c,n);
        const auto roundingAllowance=1e-7f+(expanded?4e-6f*std::abs(wetSample-drySample):0.f);
        REQUIRE(a.getSample(c,n)==Catch::Approx(drySample*(1.f-weight)+wetSample*weight).margin(roundingAllowance));
    }
    }
}

TEST_CASE("Full chain active output automation matches independent modules in accepted order", "[full-chain][expanded-output]")
{
    FullChain chain; InputSection input; MiddleSection middle; Gain gain;
    ResonantComb comb, reversedComb; FrequencyDispersion dispersion, reversedDispersion;
    Tone tone, reversedTone; HighPass hp, reversedHp; LowPass lp, reversedLp;
    FullChainSettings s; s.input.gain.gainDb=6.f; s.middle.tanh.driveDb=8.f; s.finalGain.gainDb=-3.f;
    const juce::dsp::ProcessSpec spec{48000,31,2};
    REQUIRE(chain.setParameters(s)); REQUIRE(chain.prepare(spec)); REQUIRE(chain.getLatencySamples()==92);
    REQUIRE(input.setParameters(s.input)); REQUIRE(input.prepare(spec));
    REQUIRE(middle.setParameters(s.middle)); REQUIRE(middle.prepare(spec,4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
    REQUIRE(gain.setParameters(s.finalGain)); REQUIRE(gain.prepare(spec));
    for(auto* f:{&comb,&reversedComb}) { REQUIRE(f->setParameters(s.output.comb)); REQUIRE(f->prepare(spec)); }
    for(auto* f:{&dispersion,&reversedDispersion}) { REQUIRE(f->setParameters(s.output.dispersion)); REQUIRE(f->prepare(spec)); }
    for(auto* f:{&tone,&reversedTone}) { REQUIRE(f->setParameters(s.output.tone)); REQUIRE(f->prepare(spec)); }
    for(auto* f:{&hp,&reversedHp}) { REQUIRE(f->setParameters(s.output.highPass)); REQUIRE(f->prepare(spec)); }
    for(auto* f:{&lp,&reversedLp}) { REQUIRE(f->setParameters(s.output.lowPass)); REQUIRE(f->prepare(spec)); }
    double error=0.,discrimination=0.; juce::AudioBuffer<float> a(2,127);
    for(unsigned event=0;event<64;++event)
    {
        s.output.comb={event%2?.25f:40.f,event%2?-.8f:.8f,event%2?-.9f:.9f,1200.f,.8f,true,event%7==0};
        s.output.dispersion={event%2?20.f:18000.f,.7f,event%5==0};
        s.output.tone={event%2?-6.f:6.f,event%3==0};
        s.output.highPass={event%2?20.f:12000.f,false,FilterResponse::chebyshev1};
        s.output.lowPass={event%2?19000.f:300.f,false,FilterResponse::chebyshev2};
        REQUIRE(chain.setParameters(s));
        for(auto* f:{&comb,&reversedComb}) REQUIRE(f->setParameters(s.output.comb));
        for(auto* f:{&dispersion,&reversedDispersion}) REQUIRE(f->setParameters(s.output.dispersion));
        for(auto* f:{&tone,&reversedTone}) REQUIRE(f->setParameters(s.output.tone));
        for(auto* f:{&hp,&reversedHp}) REQUIRE(f->setParameters(s.output.highPass));
        for(auto* f:{&lp,&reversedLp}) REQUIRE(f->setParameters(s.output.lowPass));
        for(int c=0;c<2;++c)for(int n=0;n<127;++n)
            a.setSample(c,n,float((c?-.7:1.)*(.6*std::sin((event*127+n)*.37)+.3*std::sin((event*127+n)*.013))));
        auto expected=a; auto reference=juce::dsp::AudioBlock<float>(expected);
        REQUIRE(input.process(reference)); REQUIRE(middle.process(reference));
        auto reversed=expected; auto wrong=juce::dsp::AudioBlock<float>(reversed);
        REQUIRE(reversedLp.process(wrong)); REQUIRE(reversedHp.process(wrong)); REQUIRE(reversedTone.process(wrong));
        REQUIRE(reversedDispersion.process(wrong)); REQUIRE(reversedComb.process(wrong));
        REQUIRE(comb.process(reference)); REQUIRE(dispersion.process(reference)); REQUIRE(tone.process(reference));
        REQUIRE(hp.process(reference)); REQUIRE(lp.process(reference));
        for(int c=0;c<2;++c)for(int n=0;n<127;++n)
            discrimination=std::max(discrimination,std::abs(double(expected.getSample(c,n))-reversed.getSample(c,n)));
        REQUIRE(gain.process(reference)); REQUIRE(chain.process(juce::dsp::AudioBlock<float>(a)));
        for(int c=0;c<2;++c)for(int n=0;n<127;++n)
            error=std::max(error,std::abs(double(a.getSample(c,n))-expected.getSample(c,n)));
    }
    REQUIRE(error<=1e-6); REQUIRE(discrimination>1e-3); REQUIRE(chain.getLatencySamples()==92);
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

#include "dsp/FullChainControls.h"
TEST_CASE("Simplified controls expose section mixes and compensated Driver", "[full-chain][expanded-output]")
{
    FullChainSettings settings;
    REQUIRE(fullChainControls.size() == 27);
    REQUIRE(getFullChainControl(settings,"driver") == 1.f);
    REQUIRE(getFullChainControl(settings,"combMix") == 0.f);
    REQUIRE(getFullChainControl(settings,"dispersionMix") == 0.f);
    for (const auto& control : fullChainControls)
        REQUIRE(getFullChainControl(settings,control.id) == control.defaultValue);
    REQUIRE(setFullChainControl(settings, "combFeedback", -.95f));
    REQUIRE(getFullChainControl(settings, "combFeedback") == -.95f);
    REQUIRE_FALSE(setFullChainControl(settings, "combFeedback", 1.f));
    REQUIRE(getFullChainControl(settings, "combFeedback") == -.95f);
    for (const auto& control : fullChainControls)
    {
        REQUIRE(setFullChainControl(settings, control.id, control.minimum));
        REQUIRE(getFullChainControl(settings, control.id) == control.minimum);
        REQUIRE(setFullChainControl(settings, control.id, control.maximum));
        REQUIRE(getFullChainControl(settings, control.id) == control.maximum);
    }
}
