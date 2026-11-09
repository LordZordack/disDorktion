#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/FullChain.h"
#include "dsp/DriverCompensation.h"
#include <cmath>
#include <limits>

using namespace disdorktion;

TEST_CASE("Waveshaper Driver leaves crusher and section dry unaffected", "[driver-adjustment]")
{
    for (bool stagesOff : {false,true})
    {
        FullChainSettings settings;
        settings.input.crusher.bitDepth=4;
        settings.input.crusher.captureRateHz=13001.f;
        settings.input.crusher.crush=.6f;
        if (!stagesOff)
        {
            settings.middle.tanh={12.f,0.f,false};
            settings.middle.mix=0.f;
        }
        FullChain unity,driven;
        REQUIRE(unity.setParameters(settings));
        settings.driver=4.f;
        REQUIRE(driven.setParameters(settings));
        REQUIRE(unity.prepare({48000,31,2})); REQUIRE(driven.prepare({48000,31,2}));
        juce::AudioBuffer<float> a(2,4096),b(2,4096);
        for(int c=0;c<2;++c) for(int n=0;n<4096;++n)
            a.setSample(c,n,.23f*std::sin(.071f*n+.3f*c));
        b.makeCopyOf(a);
        REQUIRE(unity.process(juce::dsp::AudioBlock<float>(a)));
        REQUIRE(driven.process(juce::dsp::AudioBlock<float>(b)));
        REQUIRE(unity.getLatencySamples()==driven.getLatencySamples());
        for(int c=0;c<2;++c) for(int n=0;n<4096;++n) REQUIRE(a.getSample(c,n)==b.getSample(c,n));
    }
}

TEST_CASE("Driver compensation stays fixed bounded and unity at one", "[driver-adjustment]")
{
    REQUIRE(driverCompensationGain(1.f)==1.f);
    for(int n=0;n<=3000;++n)
    {
        const auto db=driverCompensationDb(1.f+n*.001f);
        REQUIRE(std::isfinite(db)); REQUIRE(db>=-6.f); REQUIRE(db<=6.f);
    }
    REQUIRE(driverCompensationDb(4.f)==Catch::Approx(-3.976905193).margin(1e-6));
}

TEST_CASE("Waveshaper Driver compensates only nonlinear audio before shared trim and mix", "[driver-adjustment]")
{
    MiddleSettings settings {{12.f,0.f,false},{-24.f,1.f,0.f,true},.35f,6.f};
    MiddleSection actual,reference;
    REQUIRE(actual.setParameters(settings)); REQUIRE(actual.setDriver(4.f));
    settings.mix=1.f; settings.trimDb=0.f; REQUIRE(reference.setParameters(settings));
    REQUIRE(actual.prepare({48000,31,2},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
    REQUIRE(reference.prepare({48000,31,2},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
    juce::AudioBuffer<float> a(2,4096),expected(2,4096),original(2,4096);
    for(int c=0;c<2;++c) for(int n=0;n<4096;++n) a.setSample(c,n,.13f*std::sin(.071f*n+.3f*c));
    original.makeCopyOf(a); expected.makeCopyOf(a); expected.applyGain(4.f);
    REQUIRE(actual.process(juce::dsp::AudioBlock<float>(a)));
    REQUIRE(reference.process(juce::dsp::AudioBlock<float>(expected)));
    const auto latency=static_cast<int>(actual.getLatencySamples());
    const auto compensation=driverCompensationGain(4.f),trim=std::pow(10.f,.3f);
    for(int c=0;c<2;++c) for(int n=0;n<4096;++n)
    {
        const auto dry=n<latency ? 0.f : original.getSample(c,n-latency);
        const auto wet=expected.getSample(c,n)*compensation*trim;
        REQUIRE(a.getSample(c,n)==Catch::Approx(dry+.35f*(wet-dry)).margin(2e-6));
    }
    REQUIRE_FALSE(actual.setDriver(.99f)); REQUIRE_FALSE(actual.setDriver(4.01f));
    REQUIRE_FALSE(actual.setDriver(std::numeric_limits<float>::quiet_NaN()));
}

TEST_CASE("Waveshaper Driver automation is independent of callback partition", "[driver-adjustment]")
{
    MiddleSection whole,split;
    MiddleSettings settings {{9.f,0.f,false},{6.f,.5f,0.f,false},.6f,-3.f};
    REQUIRE(whole.setParameters(settings)); REQUIRE(split.setParameters(settings));
    REQUIRE(whole.prepare({48000,127,2},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
    REQUIRE(split.prepare({48000,7,2},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
    for(int event=0;event<12;++event)
    {
        REQUIRE(whole.setDriver(event%2 ? 4.f : 1.f)); REQUIRE(split.setDriver(event%2 ? 4.f : 1.f));
        settings.mix=event%3 ? .6f : 0.f;
        REQUIRE(whole.setParameters(settings)); REQUIRE(split.setParameters(settings));
        juce::AudioBuffer<float> a(2,1031),b(2,1031);
        for(int c=0;c<2;++c) for(int n=0;n<1031;++n) a.setSample(c,n,.13f*std::sin(.071f*(event*1031+n)+c));
        b.makeCopyOf(a);
        REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a)));
        auto block=juce::dsp::AudioBlock<float>(b);
        for(std::size_t n=0;n<1031;++n) REQUIRE(split.process(block.getSubBlock(n,1)));
        for(int c=0;c<2;++c) for(int n=0;n<1031;++n)
            REQUIRE(a.getSample(c,n)==Catch::Approx(b.getSample(c,n)).margin(2e-6));
        REQUIRE(whole.getLatencySamples()==92); REQUIRE(split.getLatencySamples()==92);
    }
}

TEST_CASE("Driver compensation follows the delayed input trajectory", "[driver-adjustment]")
{
    MiddleSettings settings {{12.f,0.f,false},{-24.f,1.f,0.f,true},1.f,0.f};
    MiddleSection actual,reference;
    REQUIRE(actual.setParameters(settings)); REQUIRE(reference.setParameters(settings));
    REQUIRE(actual.prepare({48000,31,1},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
    REQUIRE(reference.prepare({48000,31,1},4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
    REQUIRE(actual.setDriver(4.f));
    constexpr int frames=2048;
    juce::AudioBuffer<float> audio(1,frames),expected(1,frames);
    const auto ramp=[](int n) { return n<0 ? 1.f : 1.f+3.f*std::min(1.f,(n+1)/480.f); };
    for(int n=0;n<frames;++n)
    {
        const auto input=.16f*std::sin(.071f*n);
        audio.setSample(0,n,input); expected.setSample(0,n,input*ramp(n));
    }
    REQUIRE(actual.process(juce::dsp::AudioBlock<float>(audio)));
    REQUIRE(reference.process(juce::dsp::AudioBlock<float>(expected)));
    const auto latency=static_cast<int>(actual.getLatencySamples());
    for(int n=0;n<frames;++n)
        REQUIRE(audio.getSample(0,n)==Catch::Approx(expected.getSample(0,n)*driverCompensationGain(ramp(n-latency))).margin(2e-5));
}
