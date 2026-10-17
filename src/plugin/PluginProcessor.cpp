#include "PluginProcessor.h"
#include <charconv>
#include <limits>
#include <string_view>

namespace disdorktion
{
static_assert(std::atomic<float>::is_always_lock_free, "Host control reads must be lock free");
namespace
{
bool oldGainControl(std::string_view id) noexcept
{
    return id == "inputGainDb" || id == "inputMute" || id == "inputBypass"
        || id == "outputGainDb" || id == "outputMute" || id == "outputBypass";
}
float decodeControl(const FullChainControl& control, float value) noexcept
{
    if (std::string_view(control.id) != "bitDepth") return value;
    if (!std::isfinite(value) || value != std::floor(value) || value < 0 || value >= bitCrusherDepths.size())
        return std::numeric_limits<float>::quiet_NaN();
    return static_cast<float>(bitCrusherDepths[static_cast<std::size_t>(value)]);
}
void attribute(juce::XmlElement& xml, const char* id, float value)
{
    char text[64];
    const auto result = std::to_chars(text, text + sizeof(text), value, std::chars_format::general,
                                     std::numeric_limits<float>::max_digits10);
    xml.setAttribute(id, juce::String::fromUTF8(text, static_cast<int>(result.ptr - text)));
}
bool read(const juce::XmlElement& xml, const char* id, float minimum, float maximum, bool boolean, bool discrete, float& value)
{
    if (!xml.hasAttribute(id)) return false;
    const auto text = xml.getStringAttribute(id).toStdString();
    if (boolean)
    {
        if (text != "0" && text != "1") return false;
        value = text == "1" ? 1.0f : 0.0f;
        return true;
    }
    // Validate source precision before float conversion: e.g. 1.0000000001
    // must not round into an accepted [0,1] endpoint.
    // Metadata bounds are floats; compare decimal source bounds in their
    // shortest roundtrippable form so the documented 0.05 endpoint remains
    // legal without allowing a source just outside it to round inward.
    const auto decimalBound = [](float bound)
    {
        char text[64];
        const auto written = std::to_chars(text, text + sizeof(text), bound);
        double result = 0;
        std::from_chars(text, written.ptr, result);
        return result;
    };
    double source = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), source);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(source)
        || source < decimalBound(minimum) || source > decimalBound(maximum) || (discrete && source != std::floor(source))) return false;
    const auto narrowed = std::from_chars(text.data(), text.data() + text.size(), value);
    return narrowed.ec == std::errc{} && narrowed.ptr == text.data() + text.size() && std::isfinite(value);
}
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"referenceGain", 1},
        "Legacy trim gain", juce::NormalisableRange<float>(0.0f, 4.0f), 1.0f));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"referenceBypass", 1}, "Legacy trim bypass", false));
    // Preserve the original eight IDs and their exact host order.
    const char* gainIds[] = {"inputGainDb", "inputMute", "inputBypass", "outputGainDb", "outputMute", "outputBypass"};
    const char* gainNames[] = {"Input gain (dB)", "Input mute", "Input bypass", "Output gain (dB)", "Output mute", "Output bypass"};
    for (int i = 0; i < 6; ++i)
        if (i % 3 == 0)
            result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{gainIds[i], 1}, gainNames[i],
                juce::NormalisableRange<float>(-60.0f, 24.0f), 0.0f));
        else result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{gainIds[i], 1}, gainNames[i], false));
    for (const auto& control : fullChainControls)
    {
        if (oldGainControl(control.id)) continue;
        const juce::ParameterID id {control.id, 1};
        if (std::string_view(control.id) == "bitDepth")
        {
            juce::StringArray choices;
            for (const auto depth : bitCrusherDepths) choices.add(juce::String(depth));
            result.add(std::make_unique<juce::AudioParameterChoice>(id, control.name, choices, 10));
        }
        else if (control.discrete && control.minimum == 0 && control.maximum == 1)
            result.add(std::make_unique<juce::AudioParameterBool>(id, control.name, control.defaultValue != 0));
        else if (control.discrete)
            result.add(std::make_unique<juce::AudioParameterInt>(id, control.name,
                static_cast<int>(control.minimum), static_cast<int>(control.maximum), static_cast<int>(control.defaultValue)));
        else result.add(std::make_unique<juce::AudioParameterFloat>(id, control.name,
            juce::NormalisableRange<float>(control.minimum, control.maximum), control.defaultValue));
    }
    return result;
}

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "ReferenceControls", makeParameters())
{
    gainControl = parameters.getRawParameterValue("referenceGain");
    bypassControl = parameters.getRawParameterValue("referenceBypass");
    for (std::size_t i = 0; i < fullChainControls.size(); ++i)
        chainControls[i] = parameters.getRawParameterValue(fullChainControls[i].id);
}

void PluginProcessor::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    hostReady = false;
    const auto channels = getTotalNumInputChannels();
    if (maximumBlockSize <= 0 || getTotalNumOutputChannels() != channels) return;
    const juce::dsp::ProcessSpec spec {sampleRate, static_cast<juce::uint32>(maximumBlockSize), static_cast<juce::uint32>(channels)};
    if (!isValidProcessSpec(spec)) return;
    applyControls();
    if (!gain.prepare(spec) || !inputGain.prepare(spec) || !outputGain.prepare(spec) || !chain.prepare(spec)) return;
    chunkCapacity = std::min<std::size_t>(static_cast<std::size_t>(maximumBlockSize), 1024);
    bypassDelay.setSize(channels, static_cast<int>(chain.getLatencySamples()));
    bypassScratch.setSize(channels, static_cast<int>(chunkCapacity));
    bypassDelay.clear(); bypassScratch.clear(); bypassPosition = 0;
    setLatencySamples(legacyMode ? 0 : static_cast<int>(chain.getLatencySamples()));
    preparedSampleRate = sampleRate;
    tailSeconds.store(legacyMode ? 0.0 : 3.0 + chain.getLatencySamples() / sampleRate, std::memory_order_relaxed);
    hostReady = true;
}
void PluginProcessor::releaseResources() { hostReady = false; }
void PluginProcessor::reset()
{
    applyControls(); gain.reset(); inputGain.reset(); outputGain.reset(); chain.reset();
    bypassDelay.clear(); bypassScratch.clear(); bypassPosition = 0;
}
bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    if (layout.inputBuses.size() != 1 || layout.outputBuses.size() != 1) return false;
    const auto input = layout.getMainInputChannelSet();
    return (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo()) && input == layout.getMainOutputChannelSet();
}
bool PluginProcessor::applyControls() noexcept
{
    const auto trim = gainControl->load(std::memory_order_relaxed);
    const auto bypass = bypassControl->load(std::memory_order_relaxed);
    if (!std::isfinite(trim) || trim < 0 || trim > 4 || (bypass != 0 && bypass != 1)) return false;
    FullChainSettings settings;
    for (std::size_t i = 0; i < chainControls.size(); ++i)
        if (!setFullChainControl(settings, fullChainControls[i].id,
                decodeControl(fullChainControls[i], chainControls[i]->load(std::memory_order_relaxed)))) return false;
    // Every scalar is read once per outer callback. Independent host atomics
    // are not a transactional snapshot; invalid sets retain ALL last targets.
    const bool full = chain.setParameters(settings);
    const bool legacy = gain.setParameters({trim, bypass == 1});
    const bool input = inputGain.setParameters(settings.input.gain);
    const bool output = outputGain.setParameters(settings.finalGain);
    return full && legacy && input && output;
}
void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { render(buffer, false); }
void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { render(buffer, true); }
void PluginProcessor::render(juce::AudioBuffer<float>& buffer, bool hostBypassed) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    const auto inputs = getTotalNumInputChannels();
    const auto outputs = getTotalNumOutputChannels();
    for (int c = inputs; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, buffer.getNumSamples());
    if (!hostReady || inputs != outputs || buffer.getNumChannels() < outputs) { buffer.clear(); return; }
    applyControls();
    auto active = juce::dsp::AudioBlock<float>(buffer).getSubsetChannelBlock(0, static_cast<std::size_t>(outputs));
    const auto delay = legacyMode ? 0 : static_cast<std::size_t>(bypassDelay.getNumSamples());
    for (std::size_t offset = 0; offset < active.getNumSamples();)
    {
        const auto count = std::min(chunkCapacity, active.getNumSamples() - offset);
        auto block = active.getSubBlock(offset, count);
        // Keep an untrimmed host-bypass delay hot in every callback. Host
        // bypass is exact delayed input, including bypass of the final gain.
        for (std::size_t n = 0; n < count; ++n)
        {
            for (int c = 0; c < outputs; ++c)
            {
                const auto original = block.getSample(static_cast<std::size_t>(c), n);
                float dry = original;
                if (delay != 0)
                {
                    dry = bypassDelay.getSample(c, static_cast<int>(bypassPosition));
                    bypassDelay.setSample(c, static_cast<int>(bypassPosition), original);
                }
                if (hostBypassed) bypassScratch.setSample(c, static_cast<int>(n), dry);
            }
            if (delay != 0) bypassPosition = (bypassPosition + 1) % delay;
        }
        bool ok = gain.process(block);
        if (legacyMode) ok = inputGain.process(block) && outputGain.process(block) && ok;
        else ok = chain.process(block) && ok;
        if (!ok) { buffer.clear(); return; }
        if (hostBypassed)
            for (int c = 0; c < outputs; ++c)
                juce::FloatVectorOperations::copy(block.getChannelPointer(static_cast<std::size_t>(c)), bypassScratch.getReadPointer(c), static_cast<int>(count));
        offset += count;
    }
}
juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new juce::GenericAudioProcessorEditor(*this); }
void PluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    const juce::ScopedLock lock(getCallbackLock());
    juce::XmlElement state("DisDorktionState");
    state.setAttribute("version", "3"); state.setAttribute("mode", legacyMode ? "legacyGain" : "fullChain");
    attribute(state, "gain", gainControl->load(std::memory_order_relaxed));
    attribute(state, "bypass", bypassControl->load(std::memory_order_relaxed));
    for (std::size_t i = 0; i < fullChainControls.size(); ++i)
        attribute(state, fullChainControls[i].id, decodeControl(fullChainControls[i], chainControls[i]->load(std::memory_order_relaxed)));
    copyXmlToBinary(state, destination);
}
void PluginProcessor::publishControl(const char* id, float value)
{
    if (std::string_view(id) == "bitDepth")
        for (std::size_t i = 0; i < bitCrusherDepths.size(); ++i)
            if (value == static_cast<float>(bitCrusherDepths[i])) { value = static_cast<float>(i); break; }
    auto* parameter = parameters.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0) return;
    const auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml || !xml->hasTagName("DisDorktionState")) return;
    const auto version = xml->getStringAttribute("version");
    if (version != "1" && version != "2" && version != "3") return;
    const auto mode = xml->getStringAttribute("mode");
    if (version == "3" && mode != "legacyGain" && mode != "fullChain") return;
    float trim = 1, bypass = 0;
    if (!read(*xml,"gain",0,4,false,false,trim) || !read(*xml,"bypass",0,1,true,true,bypass)) return;
    FullChainSettings settings;
    std::array<float, fullChainControls.size()> values {};
    for (std::size_t i = 0; i < fullChainControls.size(); ++i)
    {
        const auto& control = fullChainControls[i];
        values[i] = control.defaultValue;
        const bool required = version == "3" || (version == "2" && oldGainControl(control.id));
        if (required && !read(*xml,control.id,control.minimum,control.maximum,
                control.discrete && control.minimum == 0 && control.maximum == 1,control.discrete,values[i])) return;
        if (!setFullChainControl(settings, control.id, values[i])) return;
    }
    // Parsing/validation finishes before any publication. State restoration is
    // a serialized lifecycle operation; JUCE wrappers take this same callback
    // lock around rendering. Persistence never runs from an audio callback.
    const juce::ScopedLock lock(getCallbackLock());
    publishControl("referenceGain",trim); publishControl("referenceBypass",bypass);
    for (std::size_t i = 0; i < fullChainControls.size(); ++i) publishControl(fullChainControls[i].id,values[i]);
    const bool nextLegacy = version != "3" || mode == "legacyGain";
    if (legacyMode != nextLegacy)
    {
        // The inactive path has not advanced while the other mode ran. Apply
        // the restored targets before clearing its histories, so returning
        // to it cannot replay old FIR/delay samples or old gain envelopes.
        // ReferenceGain runs in both modes; retain its active legacy ramp.
        applyControls();
        chain.reset(); inputGain.reset(); outputGain.reset();
        bypassDelay.clear(); bypassScratch.clear(); bypassPosition = 0;
    }
    legacyMode = nextLegacy;
    // Conservative filter decay allowance plus the fixed FIR/delay latency.
    tailSeconds.store(legacyMode ? 0.0 : 3.0 + (hostReady ? chain.getLatencySamples() / preparedSampleRate : 0.0),
                      std::memory_order_relaxed);
    if (hostReady) setLatencySamples(legacyMode ? 0 : static_cast<int>(chain.getLatencySamples()));
}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new disdorktion::PluginProcessor(); }
