#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"
#include <charconv>
#include <limits>
#include <string_view>

namespace disdorktion
{
static_assert(std::atomic<float>::is_always_lock_free, "Host control reads must be lock free");
namespace
{
float decodeControl(const FullChainControl& control, float value) noexcept
{
    if (std::string_view(control.id) != "bitDepth") return value;
    if (!std::isfinite(value) || value != std::floor(value) || value < 0 || value >= bitCrusherDepths.size())
        return std::numeric_limits<float>::quiet_NaN();
    return static_cast<float>(bitCrusherDepths[static_cast<std::size_t>(value)]);
}

}

juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    const auto defaults = pluginDefaultSettings();
    for (const auto& control : fullChainControls)
    {
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
        else
        {
            juce::NormalisableRange<float> range(control.minimum, control.maximum);
            const auto minimum = control.minimum, maximum = control.maximum;
            const juce::String name(control.id);
            auto attributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                [name, minimum, maximum](float value, int)
                {
                    if (((name == "tanhDriveDb" || name == "folderDriveDb" || name == "hpCutoffHz") && value <= minimum)
                        || (name == "lpCutoffHz" && value >= maximum)) return juce::String("Off");
                    return juce::String(value, 2);
                }).withValueFromStringFunction([name, minimum, maximum](const juce::String& text)
                {
                    if (text.trim().equalsIgnoreCase("Off")) return name == "lpCutoffHz" ? maximum : minimum;
                    return text.getFloatValue();
                });
            result.add(std::make_unique<juce::AudioParameterFloat>(id, control.name, range, getFullChainControl(defaults, control.id), attributes));
        }
    }
    return result;
}

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "DisDorktionControls", makeParameters())
{
    for (std::size_t i = 0; i < fullChainControls.size(); ++i)
        chainControls[i] = parameters.getRawParameterValue(fullChainControls[i].id);
    associatePreset(PresetType::factory, factoryPresets[0].name, {}, 0);
}

void PluginProcessor::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    hostReady = false;
    resetMeters();
    const auto channels = getTotalNumInputChannels();
    if (maximumBlockSize <= 0 || getTotalNumOutputChannels() != channels) return;
    const juce::dsp::ProcessSpec spec {sampleRate, static_cast<juce::uint32>(maximumBlockSize), static_cast<juce::uint32>(channels)};
    if (!isValidProcessSpec(spec)) return;
    applyControls();
    if (!chain.prepare(spec)) return;
    chunkCapacity = std::min<std::size_t>(static_cast<std::size_t>(maximumBlockSize), 1024);
    bypassDelay.setSize(channels, static_cast<int>(chain.getLatencySamples()));
    bypassScratch.setSize(channels, static_cast<int>(chunkCapacity));
    bypassDelay.clear(); bypassScratch.clear(); bypassPosition = 0;
    // Static allowance covers maximum comb feedback decay plus dispersion and
    // cutoff-filter histories, including warm bypass and automation transitions.
    fullTailSeconds.store(20.0 + chain.getLatencySamples() / sampleRate, std::memory_order_relaxed);
    setLatencySamples(static_cast<int>(chain.getLatencySamples()));
    tailSeconds.store(fullTailSeconds.load(std::memory_order_relaxed), std::memory_order_relaxed);
    hostReady = true;
}
void PluginProcessor::releaseResources() { hostReady = false; resetMeters(); }
void PluginProcessor::reset()
{
    applyControls(); chain.reset();
    resetMeters();
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
    FullChainSettings settings;
    for (std::size_t i = 0; i < chainControls.size(); ++i)
        if (!setFullChainControl(settings, fullChainControls[i].id,
                decodeControl(fullChainControls[i], chainControls[i]->load(std::memory_order_relaxed)))) return false;
    // Every scalar is read once per outer callback. Independent host atomics
    // are not a transactional snapshot; invalid sets retain ALL last targets.
    return chain.setParameters(settings);
}
void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { render(buffer, false); }
void PluginProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) { render(buffer, true); }
void PluginProcessor::render(juce::AudioBuffer<float>& buffer, bool hostBypassed) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    if (clearOverloads.exchange(false, std::memory_order_relaxed))
    {
        inputClip.store(false, std::memory_order_relaxed);
        outputClip.store(false, std::memory_order_relaxed);
    }
    const auto inputs = getTotalNumInputChannels();
    const auto outputs = getTotalNumOutputChannels();
    for (int c = inputs; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, buffer.getNumSamples());
    if (!hostReady || inputs != outputs || buffer.getNumChannels() < outputs) { buffer.clear(); return; }
    applyControls();
    float inputPeak = 0, outputPeak = 0;
    auto active = juce::dsp::AudioBlock<float>(buffer).getSubsetChannelBlock(0, static_cast<std::size_t>(outputs));
    const auto delay = static_cast<std::size_t>(bypassDelay.getNumSamples());
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
                inputPeak = std::max(inputPeak, std::abs(original));
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
        const bool ok = chain.process(block);
        if (!ok) { buffer.clear(); return; }
        if (hostBypassed)
            for (int c = 0; c < outputs; ++c)
                juce::FloatVectorOperations::copy(block.getChannelPointer(static_cast<std::size_t>(c)), bypassScratch.getReadPointer(c), static_cast<int>(count));
        for (std::size_t c = 0; c < block.getNumChannels(); ++c)
            for (std::size_t n = 0; n < count; ++n)
                outputPeak = std::max(outputPeak, std::abs(block.getSample(c, n)));
        offset += count;
    }
    accumulateMeters(inputPeak, outputPeak);
}
juce::AudioProcessorEditor* PluginProcessor::createEditor() { return new PluginEditor(*this); }
void PluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    const juce::ScopedLock lock(presetIdentityLock);
    auto state = PresetManager::encode(capturePresetValues());
    writePresetIdentity(*state);
    copyXmlToBinary(*state, destination);
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
    if (!xml) return;
    PresetValues values;
    if (PresetManager::decode(*xml, values).failed()) return;
    {
        const juce::ScopedLock identityLock(presetIdentityLock);
        publishPresetValues(values, false);
        // Invalid/absent metadata explicitly detaches any previous user target.
        restorePresetIdentity(*xml);
    }
    const juce::ScopedLock lock(getCallbackLock());
    applyControls();
    chain.reset();
    bypassDelay.clear(); bypassScratch.clear(); bypassPosition = 0;
    resetMeters();
}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new disdorktion::PluginProcessor(); }
