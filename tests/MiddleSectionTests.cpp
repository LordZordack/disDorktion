#include "dsp/MiddleSection.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <array>
#include <limits>
#include <complex>
#include <bit>
#include <cstdint>

using namespace disdorktion;
static_assert(Module<MiddleSection, MiddleSettings>);

namespace
{
void signal(juce::AudioBuffer<float>& b)
{
    b.clear();
    for (int n = 0; n < b.getNumSamples(); ++n)
        b.setSample(0, n, .25f * std::sin(.13f * static_cast<float>(n)));
}
}

TEST_CASE("Middle native path equals the isolated serial modules", "[middle]")
{
    MiddleSection middle;
    TanhSaturation tanh; WaveFolder folder;
    const MiddleSettings p {{12, -3, false}, {12, .25f, -3, false}};
    REQUIRE(middle.prepare({48000, 127, 2}, 1));
    REQUIRE(tanh.prepare({48000, 127, 2})); REQUIRE(folder.prepare({48000, 127, 2}));
    REQUIRE(middle.setParameters(p)); REQUIRE(tanh.setParameters(p.tanh)); REQUIRE(folder.setParameters(p.folder));
    middle.reset(); tanh.reset(); folder.reset();
    juce::AudioBuffer<float> a(2, 2049), b(2, 2049); signal(a); b.makeCopyOf(a);
    REQUIRE(middle.process(juce::dsp::AudioBlock<float>(a)));
    REQUIRE(tanh.process(juce::dsp::AudioBlock<float>(b))); REQUIRE(folder.process(juce::dsp::AudioBlock<float>(b)));
    REQUIRE(middle.getLatencySamples() == 0);
    for (int c = 0; c < 2; ++c) for (int n = 0; n < 2049; ++n) REQUIRE(a.getSample(c,n) == b.getSample(c,n));
}

TEST_CASE("Middle FIR latency and filtering survive every bypass state", "[middle]")
{
    for (unsigned f : {2u,4u,8u,16u})
    {
        MiddleSection m; REQUIRE(m.prepare({48000, 32, 2}, f));
        REQUIRE(m.getFactor() == f); const auto latency = m.getLatencySamples(); REQUIRE(latency > 0);
        for (bool t : {false,true}) for (bool w : {false,true})
        {
            REQUIRE(m.setParameters({{12,0,t},{12,.25f,0,w}})); m.reset();
            juce::AudioBuffer<float> b(2,2049); b.clear(); b.setSample(0,0,.25f);
            REQUIRE(m.process(juce::dsp::AudioBlock<float>(b))); REQUIRE(m.getLatencySamples() == latency);
            for (int n=0;n<2049;++n) { REQUIRE(std::isfinite(b.getSample(0,n))); REQUIRE(b.getSample(1,n)==0); }
            if (t && w)
            {
                REQUIRE(std::abs(b.getSample(0,0)) < .001f);
                REQUIRE(std::abs(b.getSample(0,static_cast<int>(latency))) > .1f);
            }
        }
    }
}

TEST_CASE("Middle preparation rejection preserves running history and targets", "[middle]")
{
    MiddleSection m, reference, unprepared;
    REQUIRE(m.prepare({96000,127,1},4)); REQUIRE(reference.prepare({96000,127,1},4));
    const MiddleSettings p {{12,-6,false},{6,.5f,-6,false}};
    REQUIRE(m.setParameters(p)); REQUIRE(reference.setParameters(p));
    juce::AudioBuffer<float> a(1,1025),b(1,1025); signal(a); b.makeCopyOf(a);
    REQUIRE(m.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
    for (unsigned f : {0u,3u,5u,24u}) REQUIRE_FALSE(m.prepare({48000,32,1},f));
    for (const auto s : {juce::dsp::ProcessSpec{4000,32,1},{500000,32,1},{48000,0,1},{48000,65537,1},{48000,32,3}})
        REQUIRE_FALSE(m.prepare(s,4));
    REQUIRE_FALSE(m.setParameters({{12,0,true},{6,std::numeric_limits<float>::quiet_NaN(),0,false}}));
    juce::AudioBuffer<float> wrong(2,10); signal(wrong); const auto original=wrong.getSample(0,1);
    REQUIRE_FALSE(m.process(juce::dsp::AudioBlock<float>(wrong))); REQUIRE(wrong.getSample(0,1)==original);
    REQUIRE_FALSE(unprepared.process(juce::dsp::AudioBlock<float>(wrong)));
    REQUIRE(m.process(juce::dsp::AudioBlock<float>(a).getSubBlock(0,0)));
    signal(a); b.makeCopyOf(a);
    REQUIRE(m.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
    for(int n=0;n<1025;++n) REQUIRE(a.getSample(0,n)==b.getSample(0,n));
}

TEST_CASE("Middle chunking reset and interrupted controls are deterministic", "[middle]")
{
    for (double rate : {8000.,44100.,48000.,96000.,192000.,384000.})
        for (unsigned f : {1u,4u,16u})
    {
        MiddleSection whole, split; REQUIRE(whole.prepare({rate,127,2},f)); REQUIRE(split.prepare({rate,127,2},f));
        juce::AudioBuffer<float> a(2,4099),b(2,4099),first(2,4099);
        const MiddleSettings p {{36,-12,false},{18,.25f,-12,false}};
        REQUIRE(whole.setParameters(p)); REQUIRE(split.setParameters(p));
        signal(a); b.makeCopyOf(a);
        REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a)));
        for(std::size_t n=0;n<4099;n+=31) REQUIRE(split.process(juce::dsp::AudioBlock<float>(b).getSubBlock(n,std::min<std::size_t>(31,4099-n))));
        for(int n=0;n<4099;++n) REQUIRE(a.getSample(0,n)==b.getSample(0,n));
        const MiddleSettings q {{-24,0,true},{-24,1,0,true}};
        REQUIRE(whole.setParameters(q)); REQUIRE(split.setParameters(q));
        signal(a); b.makeCopyOf(a);
        REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(split.process(juce::dsp::AudioBlock<float>(b)));
        for(int n=0;n<4099;++n) REQUIRE(a.getSample(0,n)==b.getSample(0,n));
        whole.reset(); signal(a); REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a))); first.makeCopyOf(a);
        whole.reset(); signal(a); REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a)));
        for(int n=0;n<4099;++n) REQUIRE(a.getSample(0,n)==first.getSample(0,n));
    }
}

TEST_CASE("Internal preparation keeps 10 ms smoothing and the host validation domain", "[middle]")
{
    for (unsigned f : {1u,4u,16u}) for (double rate : {48000.,384000.})
    {
        const auto frames=static_cast<int>(std::floor(rate*f*.010));
        TanhSaturation tanh, tReference; WaveFolder fold, fReference;
        REQUIRE(tanh.prepareOversampled({rate,65536,1},f)); REQUIRE(tReference.prepareOversampled({rate,65536,1},f));
        REQUIRE(fold.prepareOversampled({rate,65536,1},f)); REQUIRE(fReference.prepareOversampled({rate,65536,1},f));
        REQUIRE(tanh.setParameters({0,-6,false})); REQUIRE(tReference.setParameters({0,-6,false}));
        REQUIRE(fold.setParameters({0,1,-6,false})); REQUIRE(fReference.setParameters({0,1,-6,false}));
        REQUIRE_FALSE(tanh.prepare({rate*16,127,1})); REQUIRE_FALSE(fold.prepare({rate*16,127,1}));
        REQUIRE_FALSE(tanh.prepareOversampled({48000,127,1},3)); REQUIRE_FALSE(fold.prepareOversampled({4000,127,1},4));
        juce::AudioBuffer<float> a(1,frames+1),b(1,frames+1),c(1,frames+1),d(1,frames+1);
        for(int n=0;n<=frames;++n) { a.setSample(0,n,.25f); b.setSample(0,n,.25f); c.setSample(0,n,.25f); d.setSample(0,n,.25f); }
        REQUIRE(tanh.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(tReference.process(juce::dsp::AudioBlock<float>(b)));
        REQUIRE(fold.process(juce::dsp::AudioBlock<float>(c))); REQUIRE(fReference.process(juce::dsp::AudioBlock<float>(d)));
        for(int n=0;n<=frames;++n)
        {
            const auto g=1.0+(std::pow(10.,-6./20.)-1.)*std::min(1.,static_cast<double>(n+1)/frames);
            REQUIRE(a.getSample(0,n)==b.getSample(0,n)); REQUIRE(c.getSample(0,n)==d.getSample(0,n));
            REQUIRE(a.getSample(0,n)==Catch::Approx(std::tanh(.25)*g).margin(2e-7));
            REQUIRE(c.getSample(0,n)==Catch::Approx(.25*g).margin(2e-7));
        }
        REQUIRE(a.getSample(0,frames-2)!=a.getSample(0,frames-1));
        REQUIRE(c.getSample(0,frames-2)!=c.getSample(0,frames-1));
        REQUIRE(a.getSample(0,frames-1)==a.getSample(0,frames));
        REQUIRE(c.getSample(0,frames-1)==c.getSample(0,frames));
    }
    BlockDispatcher dispatcher;
    REQUIRE(dispatcher.prepareOversampled({384000,65536,2},16));
    juce::AudioBuffer<float> b(2,1); bool visited=false;
    REQUIRE(dispatcher.process(juce::dsp::AudioBlock<float>(b),[&](auto) noexcept { visited=true; }));
    REQUIRE(visited);
}

TEST_CASE("Middle FIR integer latency preserves dry blend phase in the passband", "[middle][phase]")
{
    for(unsigned f : {2u,4u,8u,16u})
    {
        MiddleSection m; REQUIRE(m.setParameters({{0,0,true},{0,1,0,true}})); REQUIRE(m.prepare({48000,127,1},f));
        juce::AudioBuffer<float> b(1,2048); b.clear(); b.setSample(0,0,1);
        REQUIRE(m.process(juce::dsp::AudioBlock<float>(b)));
        for(double frequency : {.01,.1,.2,.3,.4})
        {
            const auto omega=2*juce::MathConstants<double>::pi*frequency;
            std::complex<double> h=0;
            for(int n=0;n<2048;++n) h+=static_cast<double>(b.getSample(0,n))*std::polar(1.,-omega*n);
            const auto aligned=h*std::polar(1.,omega*m.getLatencySamples());
            REQUIRE(std::abs(20*std::log10(std::abs(h)))<=.1);
            REQUIRE(std::abs(std::arg(aligned))*180/juce::MathConstants<double>::pi<=5);
            REQUIRE(std::abs(20*std::log10(std::abs(.5+.5*aligned)))<=.2);
        }
    }
}

TEST_CASE("Middle covers the actual minimum and maximum JUCE buffer capacities", "[middle][boundary]")
{
    for(unsigned capacity : {1u,65536u})
    {
        MiddleSection m, split;
        REQUIRE(m.setParameters({{0,0,true},{0,1,0,true}}));
        REQUIRE(split.setParameters({{0,0,true},{0,1,0,true}}));
        REQUIRE(m.prepare({384000,capacity,1},16)); REQUIRE(split.prepare({384000,capacity,1},16));
        REQUIRE(m.getLatencySamples()==99);
        juce::AudioBuffer<float> a(1,65537),b(1,65537); a.clear(); a.setSample(0,0,.25f); b.makeCopyOf(a);
        REQUIRE(m.process(juce::dsp::AudioBlock<float>(a)));
        for(std::size_t n=0;n<65537;n+=127) REQUIRE(split.process(juce::dsp::AudioBlock<float>(b).getSubBlock(n,std::min<std::size_t>(127,65537-n))));
        for(int n=0;n<65537;++n) { REQUIRE(std::isfinite(a.getSample(0,n))); REQUIRE(a.getSample(0,n)==b.getSample(0,n)); }
        REQUIRE(std::abs(a.getSample(0,99))>.1f);
    }
}

TEST_CASE("Middle comparison correction mode and invalid mode preserve the contract", "[middle]")
{
    MiddleSection m,reference;
    const MiddleSettings p {{12,0,false},{12,.25f,0,false}};
    REQUIRE(m.setParameters(p)); REQUIRE(reference.setParameters(p));
    REQUIRE(m.prepare({48000,127,1},4,MiddleLatencyCorrection::juceThiran));
    REQUIRE(reference.prepare({48000,127,1},4,MiddleLatencyCorrection::juceThiran));
    REQUIRE(m.getLatencySamples()==61);
    juce::AudioBuffer<float> a(1,1025),b(1,1025); signal(a); b.makeCopyOf(a);
    REQUIRE(m.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
    REQUIRE_FALSE(m.prepare({48000,127,1},4,static_cast<MiddleLatencyCorrection>(99)));
    REQUIRE(m.getLatencySamples()==61);
    signal(a); b.makeCopyOf(a);
    REQUIRE(m.process(juce::dsp::AudioBlock<float>(a))); REQUIRE(reference.process(juce::dsp::AudioBlock<float>(b)));
    for(int n=0;n<1025;++n) REQUIRE(a.getSample(0,n)==b.getSample(0,n));
    REQUIRE(m.prepare({48000,127,1},4)); REQUIRE(m.getLatencySamples()==92);
}

TEST_CASE("ADAA middle path preserves passband phase latency bypass and partitions", "[middle][adaa]")
{
    for(unsigned f : {4u,8u,16u})
    {
        MiddleSection m,split;
        REQUIRE(m.prepare({48000,127,2},f,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
        REQUIRE(split.prepare({48000,127,2},f,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle));
        const auto latency=m.getLatencySamples();
        for(bool t : {false,true}) for(bool w : {false,true})
        {
            const MiddleSettings p {{12,-6,t},{12,.25f,-6,w}};
            REQUIRE(m.setParameters(p)); REQUIRE(split.setParameters(p)); m.reset(); split.reset();
            REQUIRE(m.getLatencySamples()==latency);
            juce::AudioBuffer<float> a(2,2048),b(2,2048); a.clear(); a.setSample(0,0,.25f); b.makeCopyOf(a);
            REQUIRE(m.process(juce::dsp::AudioBlock<float>(a)));
            for(std::size_t n=0;n<2048;n+=31) REQUIRE(split.process(juce::dsp::AudioBlock<float>(b).getSubBlock(n,std::min<std::size_t>(31,2048-n))));
            for(int n=0;n<2048;++n) { REQUIRE(a.getSample(0,n)==b.getSample(0,n)); REQUIRE(a.getSample(1,n)==0); }
            if(t && w)
                for(double frequency : {.01,.1,.2,.3,.4})
                {
                    const auto omega=2*juce::MathConstants<double>::pi*frequency;
                    std::complex<double> h=0;
                    for(int n=0;n<2048;++n) h+=4.*static_cast<double>(a.getSample(0,n))*std::polar(1.,-omega*n);
                    const auto aligned=h*std::polar(1.,omega*latency);
                    REQUIRE(std::abs(20*std::log10(std::abs(h)))<=.1);
                    REQUIRE(std::abs(std::arg(aligned))*180/juce::MathConstants<double>::pi<=5);
                    REQUIRE(std::abs(20*std::log10(std::abs(.5+.5*aligned)))<=.2);
                }
        }
        const auto retained=m.getLatencySamples();
        REQUIRE_FALSE(m.prepare({48000,127,2},f,MiddleLatencyCorrection::fir,static_cast<MiddleFoldAlgorithm>(99)));
        REQUIRE_FALSE(m.prepare({48000,127,2},f,MiddleLatencyCorrection::juceThiran,MiddleFoldAlgorithm::adaaTriangle));
        REQUIRE(m.getLatencySamples()==retained);
    }
}


TEST_CASE("Middle correction equals the original scalar ring across wraps and controls", "[middle][fir-reference]")
{
    for (unsigned factor : {1u,2u,4u,8u,16u})
        for (auto algorithm : {MiddleFoldAlgorithm::triangle,MiddleFoldAlgorithm::adaaTriangle})
    {
        const juce::dsp::ProcessSpec spec {48000,127,2};
        MiddleSection middle;
        TanhSaturation tanh;
        WaveFolder folder;
        AdaaWaveFolder adaa;
        juce::dsp::Oversampling<float> os(2,static_cast<std::size_t>(std::countr_zero(factor)),
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,true,false);
        os.initProcessing(127);
        REQUIRE(middle.prepare(spec,factor,MiddleLatencyCorrection::fir,algorithm));
        REQUIRE(tanh.prepareOversampled(spec,factor));
        REQUIRE(folder.prepareOversampled(spec,factor));
        REQUIRE(adaa.prepareOversampled(spec,factor));
        const bool useAdaa=algorithm==MiddleFoldAlgorithm::adaaTriangle;
        const auto rawLatency=os.getLatencyInSamples()+(useAdaa ? 1.5/factor : 0.0);
        const auto fraction=std::ceil(rawLatency)-rawLatency;
        std::array<double,65> taps{};
        if(fraction>1e-6)
        {
            juce::dsp::WindowingFunction<double>::fillWindowingTables(taps.data(),taps.size(),
                juce::dsp::WindowingFunction<double>::kaiser,false,8.6);
            double sum=0;
            for(std::size_t n=0;n<taps.size();++n)
            {
                const auto x=static_cast<double>(n)-(32.0+fraction);
                taps[n]*=std::abs(x)<1e-12 ? 1.0 : std::sin(juce::MathConstants<double>::pi*x)/(juce::MathConstants<double>::pi*x);
                sum+=taps[n];
            }
            for(auto& tap:taps) tap/=sum;
        }
        std::array<std::array<float,65>,2> history{};
        std::size_t position=0;
        std::uint32_t seed=90210;
        for(int epoch=0;epoch<4;++epoch)
        {
            const MiddleSettings p {{static_cast<float>(12*epoch),-6,epoch==2},
                                    {static_cast<float>(6*epoch),.25f,-3,epoch==1}};
            REQUIRE(middle.setParameters(p)); REQUIRE(tanh.setParameters(p.tanh));
            REQUIRE(folder.setParameters(p.folder)); REQUIRE(adaa.setParameters(p.folder));
            if(epoch==3)
            {
                middle.reset(); tanh.reset(); folder.reset(); adaa.reset(); os.reset();
                for(auto& channel:history) channel.fill(0);
                position=0;
            }
            juce::AudioBuffer<float> actual(2,1031),expected(2,1031);
            for(int c=0;c<2;++c) for(int n=0;n<1031;++n)
            {
                seed=1664525u*seed+1013904223u;
                actual.setSample(c,n,static_cast<float>(static_cast<double>(seed)/4294967296.0-.5));
            }
            expected.makeCopyOf(actual);
            REQUIRE(middle.process(juce::dsp::AudioBlock<float>(actual)));
            for(std::size_t offset=0;offset<1031;offset+=127)
            {
                auto chunk=juce::dsp::AudioBlock<float>(expected).getSubBlock(offset,std::min<std::size_t>(127,1031-offset));
                auto internal=factor==1 ? chunk : os.processSamplesUp(chunk);
                REQUIRE(tanh.process(internal));
                REQUIRE((useAdaa ? adaa.process(internal) : folder.process(internal)));
                if(factor!=1) os.processSamplesDown(chunk);
                if(fraction>1e-6) for(std::size_t n=0;n<chunk.getNumSamples();++n)
                {
                    for(std::size_t c=0;c<2;++c)
                    {
                        history[c][position]=chunk.getChannelPointer(c)[n];
                        double y=0; auto index=position;
                        for(auto tap:taps)
                        {
                            y+=tap*history[c][index];
                            index=index==0 ? 64 : index-1;
                        }
                        chunk.getChannelPointer(c)[n]=static_cast<float>(y);
                    }
                    position=(position+1)%65;
                }
            }
            for(int c=0;c<2;++c) for(int n=0;n<1031;++n)
                REQUIRE(actual.getSample(c,n)==expected.getSample(c,n));
        }
    }
}
