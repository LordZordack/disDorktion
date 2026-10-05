#pragma once
#include "ModuleContract.h"
#include <algorithm>
#include <type_traits>

namespace disdorktion
{
class BlockDispatcher
{
public:
    bool prepare(const juce::dsp::ProcessSpec& spec) noexcept
    {
        if (!isValidProcessSpec(spec)) return false;
        configuration = spec;
        prepared = true;
        return true;
    }
    template<class Callback>
    bool process(juce::dsp::AudioBlock<float> block, Callback&& callback) const noexcept
    {
        static_assert(std::is_nothrow_invocable_v<Callback&, juce::dsp::AudioBlock<float>>);
        if (!prepared || block.getNumChannels() != configuration.numChannels) return false;
        for (std::size_t offset = 0; offset < block.getNumSamples();)
        {
            const auto count = std::min<std::size_t>(configuration.maximumBlockSize, block.getNumSamples() - offset);
            callback(block.getSubBlock(offset, count));
            offset += count;
        }
        return true;
    }
private:
    juce::dsp::ProcessSpec configuration {};
    bool prepared = false;
};
}
