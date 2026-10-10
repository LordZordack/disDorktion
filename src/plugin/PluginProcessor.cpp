#include "PluginProcessor.h"
#include <charconv>
#include <limits>

namespace disdorktion
{
static_assert(std::atomic<float>::is_always_lock_free, "Host control reads must be lock free");

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"referenceGain", 1},
        "Legacy trim gain", juce::NormalisableRange<float>(0.0f, 4.0f), 1.0f));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"referenceBypass", 1},
        "Legacy trim bypass", false));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"inputGainDb", 1},
        "Input gain (dB)", juce::NormalisableRange<float>(-60.0f, 24.0f), 0.0f));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"inputMute", 1}, "Input mute", false));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"inputBypass", 1}, "Input bypass", false));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"outputGainDb", 1},
        "Output gain (dB)", juce::NormalisableRange<float>(-60.0f, 24.0f), 0.0f));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"outputMute", 1}, "Output mute", false));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"outputBypass", 1}, "Output bypass", false));
    return result;
}

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ReferenceControls", makeParameters())
{
    gainControl = parameters.getRawParameterValue("referenceGain");
    bypassControl = parameters.getRawParameterValue("referenceBypass");
    const char* ids[] = {"inputGainDb", "inputMute", "inputBypass", "outputGainDb", "outputMute", "outputBypass"};
    for (std::size_t i = 0; i < productionControls.size(); ++i)
        productionControls[i] = parameters.getRawParameterValue(ids[i]);
}

void PluginProcessor::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    hostReady = false;
    const auto channels = getTotalNumInputChannels();
    if (maximumBlockSize <= 0 || getTotalNumOutputChannels() != channels) return;
    applyControls(); // Invalid updates retain the last complete valid parameter set.
    const juce::dsp::ProcessSpec spec {sampleRate, static_cast<juce::uint32>(maximumBlockSize),
                                     static_cast<juce::uint32>(channels)};
    if (!gain.prepare(spec) || !inputGain.prepare(spec) || !outputGain.prepare(spec)) return;
    setLatencySamples(static_cast<int>(gain.getLatencySamples() + inputGain.getLatencySamples()
                                      + outputGain.getLatencySamples()));
    hostReady = true;
}
void PluginProcessor::releaseResources() { hostReady = false; }
void PluginProcessor::reset()
{
    applyControls();
    gain.reset();
    inputGain.reset();
    outputGain.reset();
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
    std::array<float, 6> controls {};
    for (std::size_t i = 0; i < controls.size(); ++i)
        controls[i] = productionControls[i]->load(std::memory_order_relaxed);
    const auto boolean = [](float control) noexcept { return control == 0.0f || control == 1.0f; };
    if (!std::isfinite(value) || value < 0.0f || value > 4.0f || !boolean(bypass)) return false;
    for (std::size_t i = 0; i < controls.size(); i += 3)
        if (!std::isfinite(controls[i]) || controls[i] < -60.0f || controls[i] > 24.0f
            || !boolean(controls[i + 1]) || !boolean(controls[i + 2])) return false;
    // Host atomics are independent scalar controls, not a transactional snapshot.
    const bool legacyApplied = gain.setParameters({value, bypass == 1.0f});
    const bool inputApplied = inputGain.setParameters({controls[0], controls[1] == 1.0f, controls[2] == 1.0f});
    const bool outputApplied = outputGain.setParameters({controls[3], controls[4] == 1.0f, controls[5] == 1.0f});
    return legacyApplied && inputApplied && outputApplied;
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
    if (!gain.process(active) || !inputGain.process(active) || !outputGain.process(active)) buffer.clear();
}
juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new juce::GenericAudioProcessorEditor(*this); }
void PluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    // Persistence is host-facing, outside the module realtime interface.
    juce::XmlElement state("DisDorktionState");
    state.setAttribute("version", "2");
    const char* attributes[] = {"gain", "bypass", "inputGainDb", "inputMute", "inputBypass",
                                "outputGainDb", "outputMute", "outputBypass"};
    const std::array<std::atomic<float>*, 8> controls {gainControl, bypassControl, productionControls[0],
        productionControls[1], productionControls[2], productionControls[3], productionControls[4], productionControls[5]};
    for (std::size_t i = 0; i < controls.size(); ++i)
    {
        const auto value = controls[i]->load(std::memory_order_relaxed);
        if (i == 0 || i == 2 || i == 5)
        {
            char text[64];
            const auto result = std::to_chars(text, text + sizeof(text), value, std::chars_format::general,
                                              std::numeric_limits<float>::max_digits10);
            state.setAttribute(attributes[i], juce::String::fromUTF8(text, static_cast<int>(result.ptr - text)));
        }
        else state.setAttribute(attributes[i], value == 1.0f ? "1" : "0");
    }
    copyXmlToBinary(state, destination);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0) return;
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName("DisDorktionState")) return;
    const auto version = xml->getStringAttribute("version");
    if (version != "1" && version != "2") return;
    const char* attributes[] = {"gain", "bypass", "inputGainDb", "inputMute", "inputBypass",
                                "outputGainDb", "outputMute", "outputBypass"};
    const char* ids[] = {"referenceGain", "referenceBypass", "inputGainDb", "inputMute", "inputBypass",
                         "outputGainDb", "outputMute", "outputBypass"};
    std::array<float, 8> values {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    const std::size_t count = version == "1" ? 2 : values.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!xml->hasAttribute(attributes[i])) return;
        const auto text = xml->getStringAttribute(attributes[i]).toStdString();
        if (i == 0 || i == 2 || i == 5)
        {
            // Check source precision before narrowing so values just beyond an
            // endpoint cannot round into the supported float range.
            double sourceValue = 0.0;
            const auto sourceResult = std::from_chars(text.data(), text.data() + text.size(), sourceValue);
            const double minimum = i == 0 ? 0.0 : -60.0;
            const double maximum = i == 0 ? 4.0 : 24.0;
            if (sourceResult.ec != std::errc{} || sourceResult.ptr != text.data() + text.size()
                || !std::isfinite(sourceValue) || sourceValue < minimum || sourceValue > maximum) return;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), values[i]);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
                || !std::isfinite(values[i]) || values[i] < minimum || values[i] > maximum) return;
        }
        else
        {
            if (text != "0" && text != "1") return;
            values[i] = text == "1" ? 1.0f : 0.0f;
        }
    }
    // Lifecycle synchronization belongs to the host; no persistence runs in processBlock.
    // The supported notification path keeps APVTS listeners and raw atomics in sync.
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        auto* parameter = parameters.getParameter(ids[i]);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(values[i]));
    }
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new disdorktion::PluginProcessor(); }
