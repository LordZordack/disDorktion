#include "FullChain.h"
#include <algorithm>

namespace disdorktion
{
struct FullChain::State
{
    BlockDispatcher dispatcher;
    InputSection input;
    MiddleSection middle;
    OutputSection output;
    Gain gain;
    juce::AudioBuffer<float> dry, delay;
    juce::SmoothedValue<float> mix {1.f}, bypass {0.f};
    std::size_t position = 0;
    unsigned latency = 0;
};
FullChain::FullChain() = default;
FullChain::~FullChain() = default;

bool FullChain::prepare(const juce::dsp::ProcessSpec& spec)
{
    if (!isValidProcessSpec(spec)) return false;
    auto replacement = std::make_unique<State>();
    auto internal = spec;
    internal.maximumBlockSize = std::min(spec.maximumBlockSize, juce::uint32{1024});
    (void) replacement->input.setParameters(retained.input);
    (void) replacement->middle.setParameters(retained.middle);
    (void) replacement->output.setParameters(retained.output);
    (void) replacement->gain.setParameters(retained.finalGain);
    if (!replacement->dispatcher.prepare(internal) || !replacement->input.prepare(internal)
        || !replacement->middle.prepare(internal,4,MiddleLatencyCorrection::fir,MiddleFoldAlgorithm::adaaTriangle)
        || !replacement->output.prepare(internal) || !replacement->gain.prepare(internal)) return false;
    replacement->latency = replacement->middle.getLatencySamples();
    replacement->dry.setSize(static_cast<int>(spec.numChannels),static_cast<int>(internal.maximumBlockSize));
    replacement->delay.setSize(static_cast<int>(spec.numChannels),static_cast<int>(std::max(1u,replacement->latency)));
    replacement->mix.reset(spec.sampleRate,.010);
    replacement->bypass.reset(spec.sampleRate,.010);
    state = std::move(replacement);
    reset();
    return true;
}
void FullChain::reset() noexcept
{
    peaks = {};
    if (!state) return;
    state->input.reset(); state->middle.reset(); state->output.reset(); state->gain.reset();
    state->dry.clear(); state->delay.clear(); state->position=0;
    state->mix.setCurrentAndTargetValue(retained.mix);
    state->bypass.setCurrentAndTargetValue(retained.bypass ? 1.f : 0.f);
}
bool FullChain::setParameters(const FullChainSettings& s) noexcept
{
    if (!isValidFullChainSettings(s)) return false;
    if (state)
    {
        (void) state->input.setParameters(s.input); (void) state->middle.setParameters(s.middle);
        (void) state->output.setParameters(s.output); (void) state->gain.setParameters(s.finalGain);
        state->mix.setTargetValue(s.mix); state->bypass.setTargetValue(s.bypass ? 1.f : 0.f);
    }
    retained = s;
    return true;
}
unsigned int FullChain::getLatencySamples() const noexcept { return state ? state->latency : 0; }

bool FullChain::process(juce::dsp::AudioBlock<float> block) noexcept
{
    if (!state) return false;
    const juce::ScopedNoDenormals noDenormals;
    FullChainPeaks current;
    bool good=true;
    const auto valid=state->dispatcher.process(block,[this,&current,&good](auto chunk) noexcept
    {
        auto dry=juce::dsp::AudioBlock<float>(state->dry).getSubBlock(0,chunk.getNumSamples());
        // Read before writing the same ring slot: exactly latency frames, with
        // independent channel history and one shared frame cursor.
        for(std::size_t n=0;n<chunk.getNumSamples();++n)
        {
            for(std::size_t c=0;c<chunk.getNumChannels();++c)
            {
                const auto x=chunk.getChannelPointer(c)[n];
                auto* history=state->delay.getWritePointer(static_cast<int>(c));
                dry.getChannelPointer(c)[n]=state->latency ? history[state->position] : x;
                history[state->position]=x;
            }
            if (++state->position >= std::max(1u,state->latency)) state->position=0;
        }
        good &= state->input.process(chunk);
        current.postInputGain=std::max(current.postInputGain,state->input.getPostGainPeak());
        good &= state->middle.process(chunk);
        good &= state->output.process(chunk);
        current.postHighPass=std::max(current.postHighPass,state->output.getPostHighPassPeak());
        for(std::size_t n=0;n<chunk.getNumSamples();++n)
        {
            const auto weight=state->mix.getNextValue()*(1.f-state->bypass.getNextValue());
            for(std::size_t c=0;c<chunk.getNumChannels();++c)
            {
                auto& wet=chunk.getChannelPointer(c)[n];
                current.wet=std::max(current.wet,std::abs(wet));
                const auto d=dry.getChannelPointer(c)[n];
                // Preserve exact endpoints, including dry under internal wet overload.
                wet=weight==0.f ? d : weight==1.f ? wet : d*(1.f-weight)+wet*weight;
            }
        }
        good &= state->gain.process(chunk);
        for(std::size_t c=0;c<chunk.getNumChannels();++c)
            for(std::size_t n=0;n<chunk.getNumSamples();++n)
                current.output=std::max(current.output,std::abs(chunk.getChannelPointer(c)[n]));
    });
    if(valid && block.getNumSamples()!=0) peaks=current;
    return valid && good;
}
}
