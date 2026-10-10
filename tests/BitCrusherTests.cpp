#include "dsp/BitCrusher.h"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <set>
#include <algorithm>
#include <vector>

using namespace disdorktion;
static_assert(Module<BitCrusher, BitCrusherSettings>);

TEST_CASE("Bit crusher enumerates signed PCM levels and preserves silence", "[crusher]")
{
    for (unsigned bits = 1; bits <= 8; ++bits)
    {
        BitCrusher crusher;
        REQUIRE(crusher.setParameters({bits, 384000.0f, false}));
        REQUIRE(crusher.prepare({48000.0, 32, 1}));
        const auto scale = 1 << (bits - 1);
        juce::AudioBuffer<float> audio(1, 2 * scale);
        for (int n = 0; n < audio.getNumSamples(); ++n)
            audio.setSample(0, n, static_cast<float>(n - scale) / scale);
        REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio)));
        std::set<float> levels;
        for (int n = 0; n < audio.getNumSamples(); ++n)
        {
            REQUIRE(audio.getSample(0, n) == static_cast<float>(n - scale) / scale);
            levels.insert(audio.getSample(0, n));
        }
        REQUIRE(levels.size() == (1u << bits));
        audio.clear();
        REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio)));
        REQUIRE(audio.getMagnitude(0, audio.getNumSamples()) == 0.0f);
    }
}

TEST_CASE("Bit crusher rounds ties saturates overload and supports high depths", "[crusher]")
{
    for (const auto bits : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 16u, 32u, 64u})
    {
        const double step = std::ldexp(1.0, 1 - static_cast<int>(bits));
        BitCrusher crusher;
        REQUIRE(crusher.setParameters({bits, 384000.0f, false}));
        REQUIRE(crusher.prepare({48000, 32, 1}));
        juce::AudioBuffer<float> audio(1, 8);
        const float input[] {0, static_cast<float>(step * 0.5), static_cast<float>(-step * 0.5),
            1, -1, std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
            static_cast<float>(step * 0.25)};
        const float expected[] {0, bits == 1 ? 0.0f : static_cast<float>(step),
            static_cast<float>(-step), static_cast<float>(1.0 - step), -1,
            static_cast<float>(1.0 - step), -1, 0};
        for (int n = 0; n < 8; ++n) audio.setSample(0, n, input[n]);
        REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio)));
        for (int n = 0; n < 8; ++n) REQUIRE(audio.getSample(0, n) == expected[n]);
    }
}

TEST_CASE("Bit crusher preserves capture timing across the full processing matrix", "[crusher]")
{
    for (const auto host : {8000.0,44100.0,48000.0,88200.0,96000.0,192000.0,384000.0})
        for (const auto channels : {1u,2u})
            for (const auto capacity : {1u,32u,64u,127u,256u,1024u})
            {
                CAPTURE(host, channels, capacity);
                BitCrusher whole, split;
                REQUIRE(whole.setParameters({64, 3000.0f, false}));
                REQUIRE(split.setParameters({64, 3000.0f, false}));
                REQUIRE(whole.prepare({host, capacity, channels}));
                REQUIRE(split.prepare({host, capacity, channels}));
                REQUIRE(whole.getLatencySamples() == 0);
                juce::AudioBuffer<float> a(static_cast<int>(channels), 2053), b(static_cast<int>(channels), 2053);
                for (int c = 0; c < a.getNumChannels(); ++c)
                    for (int n = 0; n < a.getNumSamples(); ++n)
                        a.setSample(c, n, (c ? -1.0f : 1.0f) * static_cast<float>(n % 1024) / 2048.0f);
                b.makeCopyOf(a);
                REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a)));
                auto view = juce::dsp::AudioBlock<float>(b);
                REQUIRE(split.process(view.getSubBlock(0, 0)));
                for (std::size_t offset = 0; offset < view.getNumSamples();)
                {
                    const auto count = std::min<std::size_t>(1 + (offset * 13) % 127, view.getNumSamples() - offset);
                    REQUIRE(split.process(view.getSubBlock(offset, count)));
                    offset += count;
                }
                std::uint64_t nextCapture = 0, k = 0, held = 0;
                bool match = true;
                for (int n = 0; n < a.getNumSamples(); ++n)
                {
                    if (static_cast<std::uint64_t>(n) == nextCapture)
                    {
                        held = n;
                        ++k;
                        nextCapture = (k * static_cast<std::uint64_t>(host) + 2999) / 3000;
                    }
                    for (int c = 0; c < a.getNumChannels(); ++c)
                    {
                        const auto expected = (c ? -1.0f : 1.0f) * static_cast<float>(held % 1024) / 2048.0f;
                        match &= a.getSample(c,n) == expected && a.getSample(c,n) == b.getSample(c,n);
                    }
                }
                REQUIRE(match);
            }
}

TEST_CASE("Bit crusher fractional clock has no growing drift over ten minutes", "[crusher][clock]")
{
    // Independent integer rational clock: 12345.5 Hz at 48 kHz.
    BitCrusher crusher;
    REQUIRE(crusher.setParameters({64,12345.5f,false}));
    REQUIRE(crusher.prepare({48000,1024,1}));
    juce::AudioBuffer<float> audio(1,1024);
    std::uint64_t clock = 0, heldIndex = 0;
    constexpr std::uint64_t frames = 48000 * 600;
    bool match = true;
    for (std::uint64_t offset = 0; offset < frames; offset += 1024)
    {
        const auto count = static_cast<int>(std::min<std::uint64_t>(1024,frames-offset));
        for (int n=0;n<count;++n) audio.setSample(0,n,static_cast<float>((offset+n)%4093)/8192.0f);
        REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio).getSubBlock(0,count)));
        for (int n=0;n<count;++n)
        {
            const auto index=offset+n;
            if (index == 0) heldIndex = 0;
            else
            {
                clock += 24691;
                if (clock >= 96000) { clock -= 96000; heldIndex = index; }
            }
            match &= audio.getSample(0,n) == static_cast<float>(heldIndex%4093)/8192.0f;
        }
    }
    REQUIRE(match);
}

TEST_CASE("Bit crusher rejects controls and layouts without changing history", "[crusher]")
{
    BitCrusher a,b,unprepared;
    REQUIRE(a.setParameters({4,100,false})); REQUIRE(b.setParameters({4,100,false}));
    REQUIRE(a.prepare({48000,8,1})); REQUIRE(b.prepare({48000,8,1}));
    juce::AudioBuffer<float> x(1,101),y(1,101),wrong(2,101);
    for (const auto settings : {BitCrusherSettings{9,100,true},{64,99,true},{64,384001,true},
        {64,std::numeric_limits<float>::quiet_NaN(),true},{64,std::numeric_limits<float>::infinity(),true}})
        REQUIRE_FALSE(a.setParameters(settings));
    REQUIRE_FALSE(a.prepare({0,8,1})); REQUIRE_FALSE(a.prepare({48000,0,1}));
    REQUIRE_FALSE(a.prepare({48000,8,3}));
    wrong.clear(); REQUIRE_FALSE(a.process(juce::dsp::AudioBlock<float>(wrong)));
    x.clear(); REQUIRE_FALSE(unprepared.process(juce::dsp::AudioBlock<float>(x)));
    for(int n=0;n<101;++n) {x.setSample(0,n,.3f);y.setSample(0,n,.3f);}
    REQUIRE(a.process(juce::dsp::AudioBlock<float>(x))); REQUIRE(b.process(juce::dsp::AudioBlock<float>(y)));
    for(int n=0;n<101;++n) REQUIRE(x.getSample(0,n)==y.getSample(0,n));
    a.reset(); x.clear(); REQUIRE(a.process(juce::dsp::AudioBlock<float>(x)));
    REQUIRE(x.getMagnitude(0,101)==0);
}

TEST_CASE("Bit depth changes requantize held input without resetting capture phase", "[crusher]")
{
    BitCrusher crusher;
    REQUIRE(crusher.setParameters({64,100,false})); REQUIRE(crusher.prepare({48000,32,1}));
    juce::AudioBuffer<float> audio(1,1); audio.setSample(0,0,.3f);
    REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio)));
    REQUIRE(crusher.setParameters({2,100,false})); audio.setSample(0,0,-.9f);
    REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio).getSubBlock(0,0)));
    REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio)));
    REQUIRE(audio.getSample(0,0)==.5f);
    audio.setSize(1,479);audio.clear(); REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio)));
    for(int n=0;n<478;++n) REQUIRE(audio.getSample(0,n)==.5f);
    REQUIRE(audio.getSample(0,478)==0);
}

TEST_CASE("Bit crusher bypass ramp follows reference and wet clock continues", "[crusher]")
{
    BitCrusher crusher;
    REQUIRE(crusher.setParameters({2,100,false})); REQUIRE(crusher.prepare({48000,32,2}));
    REQUIRE(crusher.setParameters({2,100,true}));
    juce::AudioBuffer<float> audio(2,480);
    for(int n=0;n<480;++n) {audio.setSample(0,n,.3f);audio.setSample(1,n,-.3f);}
    REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio)));
    for(int n=0;n<480;++n)
    {
        const double blend=static_cast<double>(n+1)/480;
        const double expected=(1-blend)*.5+blend*static_cast<double>(.3f);
        REQUIRE(std::abs(audio.getSample(0,n)-expected)<=2e-6);
        REQUIRE(std::abs(audio.getSample(1,n)+expected)<=2e-6);
    }
    REQUIRE(audio.getSample(0,479)==.3f);
    REQUIRE(crusher.setParameters({2,100,false}));
    for(int n=0;n<480;++n) {audio.setSample(0,n,-.3f);audio.setSample(1,n,.3f);}
    REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio)));
    REQUIRE(audio.getSample(0,479)==-.5f); REQUIRE(audio.getSample(1,479)==.5f);
}

TEST_CASE("Bit crusher rate ramp and interruptions preserve integrated capture phase", "[crusher]")
{
    BitCrusher crusher;
    REQUIRE(crusher.setParameters({64,100,false})); REQUIRE(crusher.prepare({48000,32,1}));
    REQUIRE(crusher.setParameters({64,24000,false}));
    juce::AudioBuffer<float> audio(1,720);
    double phase=0,rate=100,target=24000,start=100;
    int remaining=480;
    float held=0;
    bool match=true;
    for(int n=0;n<720;++n)
    {
        if(n==240) { REQUIRE(crusher.setParameters({64,6000,false}));start=rate;target=6000;remaining=480; }
        if(n%19==0) REQUIRE(crusher.setParameters({64,static_cast<float>(target),false}));
        const auto t=static_cast<double>(481-remaining)/480;
        rate=start+(target-start)*t;--remaining;
        const float input=static_cast<float>(n)/1024;
        audio.setSample(0,n,input);
        auto one=juce::dsp::AudioBlock<float>(audio).getSubBlock(n,1);
        REQUIRE(crusher.process(one));
        if(n==0) held=input;
        else { phase+=rate;if(phase>=48000) {phase-=48000;held=input;} }
        match &= audio.getSample(0,n)==held;
    }
    REQUIRE(match);
}

TEST_CASE("Bit crusher simultaneous controls are identical across block partitions and independent instances", "[crusher]")
{
    BitCrusher whole,split,independent;
    for(auto* module : {&whole,&split,&independent})
    {
        REQUIRE(module->setParameters({64,12345.5f,false}));
        REQUIRE(module->prepare({96000,32,2}));
    }
    juce::AudioBuffer<float> a(2,2201),b(2,2201),other(2,2201);
    for(int c=0;c<2;++c) for(int n=0;n<2201;++n)
        a.setSample(c,n,static_cast<float>(((n*37+c*101)%2048)-1024)/1024);
    b.makeCopyOf(a);other.makeCopyOf(a);
    const int positions[] {0,127,509,1017,2201};
    const BitCrusherSettings events[] {{64,12345.5f,false},{4,100,true},{1,384000,false},{32,8000,true}};
    for(int e=0;e<4;++e)
    {
        REQUIRE(whole.setParameters(events[e])); REQUIRE(split.setParameters(events[e]));
        REQUIRE(whole.process(juce::dsp::AudioBlock<float>(a).getSubBlock(positions[e],positions[e+1]-positions[e])));
        for(int n=positions[e];n<positions[e+1];)
        {
            const auto count=std::min(positions[e+1]-n,1+(n*17)%64);
            REQUIRE(split.process(juce::dsp::AudioBlock<float>(b).getSubBlock(n,count)));
            n+=count;
        }
    }
    REQUIRE(independent.process(juce::dsp::AudioBlock<float>(other)));
    independent.reset();
    bool same=true,different=false;
    for(int c=0;c<2;++c) for(int n=0;n<2201;++n)
    {
        same &= a.getSample(c,n)==b.getSample(c,n);
        different |= a.getSample(c,n)!=other.getSample(c,n);
        REQUIRE(std::isfinite(a.getSample(c,n)));
    }
    REQUIRE(same); REQUIRE(different);
}

TEST_CASE("Expressive crusher defaults and continuous quantization match reference", "[crusher][expressive]")
{
    const BitCrusherSettings defaults {8, 8000, false};
    REQUIRE(defaults.mix == 1.0f); REQUIRE(defaults.crush == 0.0f);
    REQUIRE(defaults.shape == 0.0f); REQUIRE(defaults.jitter == 0.0f); REQUIRE(defaults.jitterSeed == 1);
    for (auto bits : {1u,4u,8u,64u})
        for (auto amount : {0.0f,0.001f,0.25f,0.5f,1.0f})
            for (auto shape : {-1.0f,0.0f,1.0f})
            {
                BitCrusher crusher;
                BitCrusherSettings settings {bits,384000,false}; settings.crush=amount; settings.shape=shape;
                REQUIRE(crusher.setParameters(settings)); REQUIRE(crusher.prepare({48000,32,1}));
                const float inputs[] {-1,-0.75f,-0.01f,0,0.01f,0.75f,1,std::numeric_limits<float>::max()};
                juce::AudioBuffer<float> audio(1,8);
                const double base=std::ldexp(1.0,1-static_cast<int>(bits));
                const double step=base+(1-base)*amount*amount*amount;
                const double exponent=std::exp2(2.0*shape),weight=std::max(double(amount),std::abs(double(shape)));
                for(int n=0;n<8;++n) audio.setSample(0,n,inputs[n]);
                REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio)));
                for(int n=0;n<8;++n)
                {
                    const double raw=std::clamp(double(inputs[n]),-1.0,1.0);
                    const double legacy=std::clamp(std::round(raw/base)*base,-1.0,1-base);
                    const double shaped=std::copysign(std::pow(std::abs(raw),exponent),raw);
                    const double q=std::clamp(std::round(shaped/step)*step,-1.0,1.0);
                    const double enhanced=std::copysign(std::pow(std::abs(q),1/exponent),q);
                    const float expected=static_cast<float>((1-weight)*legacy+weight*enhanced);
                    REQUIRE(std::abs(audio.getSample(0,n)-expected)<=2e-6f);
                    REQUIRE(std::isfinite(audio.getSample(0,n)));
                }
            }
}

TEST_CASE("Expressive crusher mix is exact and invalid settings are rejected", "[crusher][expressive]")
{
    BitCrusher crusher; BitCrusherSettings settings {1,100,false}; settings.mix=0;settings.crush=1;settings.shape=1;settings.jitter=1;
    REQUIRE(crusher.setParameters(settings)); REQUIRE(crusher.prepare({48000,32,1}));
    juce::AudioBuffer<float> audio(1,4);
    const float inputs[] {std::numeric_limits<float>::max(),-std::numeric_limits<float>::max(),std::numeric_limits<float>::denorm_min(),-0.25f};
    for(int n=0;n<4;++n) audio.setSample(0,n,inputs[n]);
    REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio)));
    for(int n=0;n<4;++n) REQUIRE(audio.getSample(0,n)==inputs[n]);
    for(int field=0;field<4;++field)
        for(float bad : {-2.0f,2.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        {
            auto invalid=settings;
            (field==0?invalid.mix:field==1?invalid.crush:field==2?invalid.shape:invalid.jitter)=bad;
            REQUIRE_FALSE(crusher.setParameters(invalid));
        }
}

TEST_CASE("Expressive crusher jitter resets and partitions deterministically", "[crusher][expressive]")
{
    for(auto seed : {0u,1u,0xffffffffu})
    {
        BitCrusher a,b; BitCrusherSettings settings {64,8000,false};settings.jitter=1;settings.jitterSeed=seed;
        REQUIRE(a.setParameters(settings));REQUIRE(b.setParameters(settings));
        REQUIRE(a.prepare({48000,127,2}));REQUIRE(b.prepare({48000,32,2}));
        juce::AudioBuffer<float> x(2,48000),y(2,48000);
        for(int n=0;n<48000;++n) { x.setSample(0,n,float(n)/96000);x.setSample(1,n,-float(n)/96000); }
        y.makeCopyOf(x); auto original=x;
        REQUIRE(a.process(juce::dsp::AudioBlock<float>(x)));
        auto view=juce::dsp::AudioBlock<float>(y);
        for(std::size_t off=0;off<48000;off+=17) REQUIRE(b.process(view.getSubBlock(off,std::min<std::size_t>(17,48000-off))));
        int captures=1,last=0; std::set<int> gaps;
        for(int n=0;n<48000;++n)
        {
            REQUIRE(x.getSample(0,n)==y.getSample(0,n));
            REQUIRE(x.getSample(1,n)==-x.getSample(0,n));
            if(n>0&&x.getSample(0,n)!=x.getSample(0,n-1)) {++captures;REQUIRE(n-last<=11);gaps.insert(n-last);last=n;}
        }
        REQUIRE(captures>7600);REQUIRE(captures<8400);
        REQUIRE(gaps.size()>1);
        a.reset(); REQUIRE(a.process(juce::dsp::AudioBlock<float>(original)));
        for(int n=0;n<48000;++n) REQUIRE(original.getSample(0,n)==x.getSample(0,n));
    }
}

TEST_CASE("Expressive crusher automation is partition invariant and full rate ignores jitter", "[crusher][expressive]")
{
    BitCrusher a,b; REQUIRE(a.prepare({48000,127,2}));REQUIRE(b.prepare({48000,32,2}));
    juce::AudioBuffer<float> x(2,4000),y(2,4000);
    for(int c=0;c<2;++c) for(int n=0;n<4000;++n) x.setSample(c,n,float(std::sin(n*.13+c))*.9f);
    y.makeCopyOf(x); auto av=juce::dsp::AudioBlock<float>(x),bv=juce::dsp::AudioBlock<float>(y);
    for(std::size_t offset=0;offset<4000;offset+=1000)
    {
        BitCrusherSettings p {offset?4u:64u,offset?12345.5f:384000.0f,offset==2000};
        p.mix=offset==1000?.2f:1;p.crush=offset?.4f:0;p.shape=offset==3000?-.7f:.6f;p.jitter=1;p.jitterSeed=unsigned(offset+1);
        REQUIRE(a.setParameters(p));REQUIRE(b.setParameters(p));
        REQUIRE(a.process(av.getSubBlock(offset,1000)));
        for(std::size_t k=0;k<1000;k+=31) REQUIRE(b.process(bv.getSubBlock(offset+k,std::min<std::size_t>(31,1000-k))));
    }
    for(int c=0;c<2;++c) for(int n=0;n<4000;++n) REQUIRE(x.getSample(c,n)==y.getSample(c,n));
    BitCrusher full;BitCrusherSettings p {64,384000,false};p.jitter=1;
    REQUIRE(full.setParameters(p)); REQUIRE(full.prepare({48000,32,1}));
    juce::AudioBuffer<float> input(1,100);for(int n=0;n<100;++n) input.setSample(0,n,float(n)/200);
    REQUIRE(full.process(juce::dsp::AudioBlock<float>(input)));
    for(int n=0;n<100;++n) REQUIRE(input.getSample(0,n)==float(n)/200);
}

TEST_CASE("Expressive crusher mix ramp follows ten millisecond reference", "[crusher][expressive]")
{
    BitCrusher crusher; BitCrusherSettings p {4,384000,false};
    REQUIRE(crusher.setParameters(p)); REQUIRE(crusher.prepare({48000,32,1}));p.mix=0;
    REQUIRE(crusher.setParameters(p));juce::AudioBuffer<float> audio(1,481);
    for(int n=0;n<481;++n) audio.setSample(0,n,.2f);
    REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio)));
    for(int n=0;n<481;++n)
    {
        const double wet=1-std::min(n+1,480)/480.0;
        REQUIRE(std::abs(audio.getSample(0,n)-float(wet*.25+(1-wet)*double(.2f)))<2e-6f);
    }
}

TEST_CASE("Expressive crusher seeds differ and neutral enabling is continuous", "[crusher][expressive]")
{
    BitCrusher a,b;BitCrusherSettings p {64,8000,false};p.jitter=1;
    REQUIRE(a.setParameters(p));p.jitterSeed=2;REQUIRE(b.setParameters(p));
    REQUIRE(a.prepare({48000,32,1})); REQUIRE(b.prepare({48000,32,1}));
    juce::AudioBuffer<float> x(1,1000),y(1,1000);
    for(int n=0;n<1000;++n) {x.setSample(0,n,float(n)/2000);y.setSample(0,n,float(n)/2000);}
    REQUIRE(a.process(juce::dsp::AudioBlock<float>(x)));REQUIRE(b.process(juce::dsp::AudioBlock<float>(y)));
    bool different=false;for(int n=0;n<1000;++n) different|=x.getSample(0,n)!=y.getSample(0,n);REQUIRE(different);
    BitCrusher neutral,tiny;p={1,384000,false};REQUIRE(neutral.setParameters(p));p.crush=1e-5f;p.shape=1e-5f;
    REQUIRE(tiny.setParameters(p));REQUIRE(neutral.prepare({48000,32,1}));REQUIRE(tiny.prepare({48000,32,1}));
    for(int n=0;n<1000;++n) {x.setSample(0,n,float(n)/500-1);y.setSample(0,n,float(n)/500-1);}
    REQUIRE(neutral.process(juce::dsp::AudioBlock<float>(x)));REQUIRE(tiny.process(juce::dsp::AudioBlock<float>(y)));
    for(int n=0;n<1000;++n) REQUIRE(std::abs(x.getSample(0,n)-y.getSample(0,n))<2e-5f);
}

TEST_CASE("Neutral expressive settings preserve legacy bypass expression exactly", "[crusher][expressive]")
{
    BitCrusher crusher;BitCrusherSettings p {64,100,false};
    REQUIRE(crusher.setParameters(p)); REQUIRE(crusher.prepare({48000,32,1}));
    juce::AudioBuffer<float> initial(1,1);initial.setSample(0,0,.3f);REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(initial)));
    p.bypass=true;REQUIRE(crusher.setParameters(p));
    juce::AudioBuffer<float> audio(1,480);for(int n=0;n<480;++n)audio.setSample(0,n,-.3f);
    juce::SmoothedValue<double,juce::ValueSmoothingTypes::Linear> ramp {0};ramp.reset(48000,.010);ramp.setTargetValue(1);
    REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio)));
    for(int n=0;n<480;++n)
    {
        const auto blend=ramp.getNextValue();const auto held=n<479?.3f:-.3f;
        const float expected=blend==1?-0.3f:static_cast<float>((1-blend)*held+blend*double(-.3f));
        REQUIRE(audio.getSample(0,n)==expected);
    }
}

TEST_CASE("Expressive crusher crush shape and mix follow interrupted ramps", "[crusher][expressive]")
{
    BitCrusher crusher;BitCrusherSettings p {8,384000,false};
    REQUIRE(crusher.setParameters(p));REQUIRE(crusher.prepare({48000,32,1}));
    juce::SmoothedValue<double,juce::ValueSmoothingTypes::Linear> c{0},s{0},m{1};
    c.reset(48000,.010);s.reset(48000,.010);m.reset(48000,.010);
    for(int segment=0;segment<8;++segment)
    {
        if(segment==0) {p.crush=.7f;p.shape=.8f;p.mix=.2f;}
        if(segment==2) {p.crush=.3f;p.shape=-.9f;p.mix=.8f;}
        REQUIRE(crusher.setParameters(p)); c.setTargetValue(p.crush);s.setTargetValue(p.shape);m.setTargetValue(p.mix);
        juce::AudioBuffer<float> audio(1,137);for(int n=0;n<137;++n)audio.setSample(0,n,.2f);
        REQUIRE(crusher.process(juce::dsp::AudioBlock<float>(audio)));
        for(int n=0;n<137;++n)
        {
            const auto amount=c.getNextValue(),shape=s.getNextValue(),mix=m.getNextValue();
            const auto base=1.0/128,step=base+(1-base)*amount*amount*amount,exponent=std::exp2(2*shape);
            const auto raw=double(.2f),shaped=std::pow(raw,exponent);
            const auto quantized=std::clamp(std::round(shaped/step)*step,-1.0,1.0);
            const auto decoded=std::copysign(std::pow(std::abs(quantized),1/exponent),quantized);
            const auto weight=std::max(amount,std::abs(shape));
            const auto legacy=std::round(raw/base)*base;
            const auto wet=(1-weight)*legacy+weight*decoded;
            const auto expected=float(mix*float(wet)+(1-mix)*raw);
            REQUIRE(std::abs(audio.getSample(0,n)-expected)<=2e-6f);
        }
    }
}

#include "../benchmarks/TimingWindow.h"
TEST_CASE("Slow expressive benchmarks extend bounded timing windows", "[benchmark]")
{
    using disdorktion::benchmark::timingWindowSeconds;
    REQUIRE(timingWindowSeconds(1500000)==10);
    REQUIRE(timingWindowSeconds(150000)==10);
    REQUIRE(timingWindowSeconds(100000)==15);
    REQUIRE(timingWindowSeconds(50000)==30);
    REQUIRE(timingWindowSeconds(10000)==120);
    REQUIRE(timingWindowSeconds(0)==120);
    REQUIRE(timingWindowSeconds(-1)==120);
}
