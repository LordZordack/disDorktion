#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "plugin/PluginProcessor.h"
#include <limits>
#include "AllocationTracking.h"
#include <thread>
using disdorktion::PluginProcessor;
TEST_CASE("Issue38 exposes Driver and removes obsolete prototype controls", "[adapter][issue38]")
{
    PluginProcessor plugin;
    auto* driver = plugin.parameterState().getParameter("driver");
    REQUIRE(driver != nullptr);
    REQUIRE(driver->convertFrom0to1(0.0f) == 1.0f);
    REQUIRE(driver->convertFrom0to1(1.0f) == 4.0f);
    REQUIRE(driver->convertFrom0to1(driver->getDefaultValue()) == 1.0f);
    for (const auto* id : {"referenceGain", "referenceBypass", "inputMute", "inputBypass",
         "outputMute", "outputBypass", "chainBypass", "tanhOutputDb", "folderOutputDb",
         "tanhBypass", "folderBypass", "hpBypass", "lpBypass", "combDampingEnabled"})
        REQUIRE(plugin.parameterState().getParameter(id) == nullptr);
    REQUIRE(plugin.parameterState().getParameter("waveshaperMix") != nullptr);
    REQUIRE(plugin.parameterState().getParameter("waveshaperTrimDb") != nullptr);
}

namespace {
juce::AudioProcessor::BusesLayout layout(juce::AudioChannelSet input, juce::AudioChannelSet output)
{
    juce::AudioProcessor::BusesLayout result; result.inputBuses.add(input); result.outputBuses.add(output); return result;
}
void fill(juce::AudioBuffer<float>& audio, float value)
{
    for(int c=0;c<audio.getNumChannels();++c) for(int n=0;n<audio.getNumSamples();++n) audio.setSample(c,n,value);
}
}
TEST_CASE("Adapter admits matching mono stereo and rejects other layouts", "[adapter]")
{
    PluginProcessor p;
    REQUIRE(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::mono(),juce::AudioChannelSet::mono())));
    REQUIRE(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::stereo(),juce::AudioChannelSet::stereo())));
    REQUIRE_FALSE(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::mono(),juce::AudioChannelSet::stereo())));
    REQUIRE_FALSE(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::disabled(),juce::AudioChannelSet::stereo())));
    REQUIRE_FALSE(p.isBusesLayoutSupported(layout(juce::AudioChannelSet::create5point1(),juce::AudioChannelSet::create5point1())));
}
TEST_CASE("Adapter invalid lifecycle buffers silence output and surplus channels", "[adapter]")
{
    PluginProcessor p; juce::MidiBuffer midi; juce::AudioBuffer<float> audio(2,2051);
    fill(audio,0.25f); p.processBlock(audio,midi); REQUIRE(audio.getMagnitude(0,2051)==0);
    p.prepareToPlay(48000,32); REQUIRE(p.getLatencySamples()>0);
    fill(audio,0.25f); p.processBlock(audio,midi); REQUIRE(std::isfinite(audio.getSample(0,2050)));
    juce::AudioBuffer<float> wrong(1,7); fill(wrong,0.25f); p.processBlock(wrong,midi); REQUIRE(wrong.getMagnitude(0,7)==0);
    for(const auto rate : {0.0,7999.0,384001.0,std::numeric_limits<double>::quiet_NaN()})
    {
        p.prepareToPlay(rate,32); fill(audio,0.25f); p.processBlock(audio,midi); REQUIRE(audio.getMagnitude(0,2051)==0);
    }
    p.prepareToPlay(48000,-1); fill(audio,0.25f); p.processBlock(audio,midi); REQUIRE(audio.getMagnitude(0,2051)==0);
    p.prepareToPlay(48000,32); p.releaseResources(); fill(audio,0.25f); p.processBlock(audio,midi); REQUIRE(audio.getMagnitude(0,2051)==0);
    REQUIRE(p.setBusesLayout(layout(juce::AudioChannelSet::mono(),juce::AudioChannelSet::mono())));
    p.prepareToPlay(96000,1); fill(audio,0.125f); p.processBlock(audio,midi);
    REQUIRE(audio.getMagnitude(1,0,2051)==0);
}
TEST_CASE("Adapter production callbacks and reset allocate no storage", "[adapter][realtime]")
{
    PluginProcessor p; p.prepareToPlay(48000,32); juce::MidiBuffer midi;
    for(const int frames : {0,1,32,127,2051})
    {
        juce::AudioBuffer<float> audio(2,frames); fill(audio,0.25f);
        { disdorktion::test::ScopedAllocationTracking tracking; p.processBlock(audio,midi); p.processBlockBypassed(audio,midi); p.reset(); }
        const auto counts=disdorktion::test::getAllocationCounts(); REQUIRE(counts.allocations==0); REQUIRE(counts.deallocations==0);
    }
}

TEST_CASE("Waveform histories preserve signed stereo envelopes and bound stalled readers", "[adapter][waveform]")
{
    disdorktion::WaveformHistory history;
    history.configure(48000.0);
    std::array<float,48> left, right;
    left.fill(0.75f); right.fill(-0.5f);
    const float* channels[] {left.data(),right.data()};
    for (std::size_t i=0;i<disdorktion::WaveformHistory::capacity+7;++i) history.push(channels,2,48);
    std::array<disdorktion::WaveformHistory::Frame,512> frames;
    REQUIRE(history.drain(frames.data(),frames.size())==512);
    REQUIRE(frames.front().minimum==-0.5f);
    REQUIRE(frames.front().maximum==0.75f);
    const auto previous=frames.back().sequence;
    history.push(channels,2,48);
    REQUIRE(history.drain(frames.data(),frames.size())==1);
    REQUIRE(frames[0].sequence==previous+8);
    for (int i=0;i<1200;++i) { history.push(channels,2,48); REQUIRE(history.drain(frames.data(),1)==1); }
    history.push(channels,2,48); history.discardPending();
    REQUIRE(history.drain(frames.data(),frames.size())==0);
    left.fill(0); right.fill(0); history.push(channels,2,48);
    REQUIRE(history.drain(frames.data(),frames.size())==1);
    REQUIRE(frames[0].minimum==0); REQUIRE(frames[0].maximum==0);
    for (int i=0;i<4;++i) history.push(channels,2,48);
    REQUIRE(history.drain(frames.data(),2)==2);
    REQUIRE(frames[1].sequence==frames[0].sequence+1);
    REQUIRE(history.drain(frames.data(),frames.size())==0);
    history.push(channels,2,48);
    const auto oldEpoch=history.epoch(); history.resetEpoch();
    REQUIRE(history.epoch()!=oldEpoch);
    REQUIRE(history.drain(frames.data(),frames.size())==0);
    history.push(channels,2,48);
    REQUIRE(history.drain(frames.data(),frames.size())==1);
    REQUIRE(frames[0].epoch==history.epoch());
}

TEST_CASE("Waveform lifecycle epochs clear queued and partial data without a callback", "[adapter][waveform]")
{
    PluginProcessor p;
    auto& input=p.getInputWaveformHistory(); auto& output=p.getOutputWaveformHistory();
    auto epoch=input.epoch();
    p.prepareToPlay(48000,64); REQUIRE(input.epoch()!=epoch); epoch=input.epoch();
    juce::MidiBuffer midi; juce::AudioBuffer<float> audio(2,24); fill(audio,0.8f); p.processBlock(audio,midi);
    p.reset(); REQUIRE(input.epoch()!=epoch); REQUIRE(output.epoch()==input.epoch()); epoch=input.epoch();
    fill(audio,0); p.processBlock(audio,midi);
    std::array<disdorktion::WaveformHistory::Frame,512> frames;
    REQUIRE(input.drain(frames.data(),frames.size())==0);
    p.releaseResources(); REQUIRE(input.epoch()!=epoch); epoch=input.epoch();
    p.prepareToPlay(0,64); REQUIRE(input.epoch()!=epoch); epoch=input.epoch();
    juce::MemoryBlock state; p.getStateInformation(state);
    p.setStateInformation(state.getData(),static_cast<int>(state.getSize())); REQUIRE(input.epoch()!=epoch);
    REQUIRE(output.epoch()==input.epoch());
    REQUIRE(input.drain(frames.data(),frames.size())==0);
}

TEST_CASE("Waveform processor taps raw input and final normal or bypass output", "[adapter][waveform]")
{
    for (const bool mono : {false,true})
    for (const bool bypassed : {false,true})
    {
        PluginProcessor p;
        if(mono) REQUIRE(p.setBusesLayout(layout(juce::AudioChannelSet::mono(),juce::AudioChannelSet::mono())));
        auto* inputGain=p.parameterState().getParameter("inputGainDb");
        REQUIRE(inputGain!=nullptr); inputGain->setValueNotifyingHost(inputGain->convertTo0to1(-12.0f));
        p.prepareToPlay(48000,96);
        juce::MidiBuffer midi; juce::AudioBuffer<float> audio(mono?1:2,96); fill(audio,0.25f);
        if(!mono) for(int n=0;n<96;++n) audio.setSample(1,n,-0.5f);
        if(bypassed) p.processBlockBypassed(audio,midi); else p.processBlock(audio,midi);
        std::array<disdorktion::WaveformHistory::Frame,512> raw, final;
        REQUIRE(p.getInputWaveformHistory().drain(raw.data(),raw.size())==2);
        REQUIRE(p.getOutputWaveformHistory().drain(final.data(),final.size())==2);
        for(int frame=0;frame<2;++frame)
        {
            REQUIRE(raw[frame].maximum==0.25f); REQUIRE(raw[frame].minimum==(mono?0.25f:-0.5f));
            float minimum=audio.getSample(0,frame*48), maximum=minimum;
            for(int c=0;c<audio.getNumChannels();++c) for(int n=frame*48;n<(frame+1)*48;++n)
            { minimum=std::min(minimum,audio.getSample(c,n)); maximum=std::max(maximum,audio.getSample(c,n)); }
            REQUIRE(final[frame].minimum==minimum); REQUIRE(final[frame].maximum==maximum);
        }
    }
}

TEST_CASE("Waveform consumer can drain and reopen during active publication", "[adapter][waveform]")
{
    disdorktion::WaveformHistory history;
    history.configure(48000);
    std::atomic<bool> started {false}, finished {false};
    std::thread producer([&]
    {
        std::array<float,48> samples;
        const float* channels[] {samples.data()};
        started.store(true,std::memory_order_release);
        for (std::uint64_t i=0;i<20000;++i)
        {
            samples.fill(static_cast<float>(i%17)/16.0f);
            history.push(channels,1,48);
        }
        finished.store(true,std::memory_order_release);
    });
    std::array<disdorktion::WaveformHistory::Frame,64> frames;
    std::uint64_t previous=0;
    bool havePrevious=false, valid=true;
    std::size_t observed=0, polls=0;
    while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
    const auto consume=[&]
    {
        const auto count=history.drain(frames.data(),frames.size());
        for (std::size_t i=0;i<count;++i)
        {
            const auto& frame=frames[i];
            valid &= frame.minimum==frame.maximum
                && frame.minimum==static_cast<float>(frame.sequence%17)/16.0f
                && frame.epoch==history.epoch()
                && (!havePrevious || frame.sequence>previous);
            previous=frame.sequence; havePrevious=true; ++observed;
        }
    };
    while (!finished.load(std::memory_order_acquire))
    {
        consume();
        // Reopening uses only the consumer cursor while audio keeps publishing.
        if (++polls%17==0) history.discardPending();
        std::this_thread::yield();
    }
    producer.join();
    consume();
    REQUIRE(valid);
    REQUIRE(observed>0);
}
