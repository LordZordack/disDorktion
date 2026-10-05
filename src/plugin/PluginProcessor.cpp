#include "PluginProcessor.h"
#include <charconv>

namespace disdorktion
{
static_assert(std::atomic<float>::is_always_lock_free, "Host control reads must be lock free");

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"referenceGain", 1},
        "Reference gain", juce::NormalisableRange<float>(0.0f, 4.0f), 1.0f));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"referenceBypass", 1},
        "Reference bypass", false));
    return result;
}

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ReferenceControls", makeParameters())
{
    gainControl = parameters.getRawParameterValue("referenceGain");
    bypassControl = parameters.getRawParameterValue("referenceBypass");
}

void PluginProcessor::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    hostReady = false;
    const auto channels = getTotalNumInputChannels();
    if (maximumBlockSize <= 0 || getTotalNumOutputChannels() != channels) return;
    applyControls(); // Invalid updates retain the last complete valid parameter set.
    if (!gain.prepare({sampleRate, static_cast<juce::uint32>(maximumBlockSize),
                       static_cast<juce::uint32>(channels)})) return;
    setLatencySamples(static_cast<int>(gain.getLatencySamples()));
    hostReady = true;
}
void PluginProcessor::releaseResources() { hostReady = false; }
void PluginProcessor::reset()
{
    applyControls();
    gain.reset();
}
bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    if (layout.inputBuses.size() != 1 || layout.outputBuses.size() != 1) return false;
    const auto input = layout.getMainInputChannelSet();
    return (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo())
        && input == layout.getMainOutputChannelSet();
}

bool PluginProcessor::applyControls() noexcept
{
    const auto value = gainControl->load(std::memory_order_relaxed);
    const auto bypass = bypassControl->load(std::memory_order_relaxed);
    if (!std::isfinite(bypass) || (bypass != 0.0f && bypass != 1.0f)) return false;
    return gain.setParameters({value, bypass == 1.0f});
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto inputs = getTotalNumInputChannels();
    const auto outputs = getTotalNumOutputChannels();
    for (int c = inputs; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, buffer.getNumSamples());
    if (!hostReady || inputs != outputs || buffer.getNumChannels() < outputs)
    {
        buffer.clear();
        return;
    }
    applyControls();
    auto active = juce::dsp::AudioBlock<float>(buffer).getSubsetChannelBlock(0, static_cast<std::size_t>(outputs));
    if (!gain.process(active)) buffer.clear();
}
juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new juce::GenericAudioProcessorEditor(*this); }
void PluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    // Persistence is host-facing, outside the module realtime interface.
    juce::XmlElement state("DisDorktionState");
    state.setAttribute("version", "1");
    state.setAttribute("gain", static_cast<double>(gainControl->load(std::memory_order_relaxed)));
    state.setAttribute("bypass", bypassControl->load(std::memory_order_relaxed) == 1.0f ? "1" : "0");
    copyXmlToBinary(state, destination);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0) return;
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName("DisDorktionState") || xml->getStringAttribute("version") != "1"
        || !xml->hasAttribute("gain") || !xml->hasAttribute("bypass")) return;
    const auto text = xml->getStringAttribute("gain").toStdString();
    float value = 0.0f;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    const auto bypass = xml->getStringAttribute("bypass");
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value)
        || value < 0.0f || value > 4.0f || (bypass != "0" && bypass != "1")) return;
    auto* gainParameter = parameters.getParameter("referenceGain");
    auto* bypassParameter = parameters.getParameter("referenceBypass");
    gainParameter->setValueNotifyingHost(gainParameter->convertTo0to1(value));
    bypassParameter->setValueNotifyingHost(bypass == "1" ? 1.0f : 0.0f);
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new disdorktion::PluginProcessor(); }
