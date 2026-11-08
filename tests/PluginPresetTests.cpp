#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "plugin/PluginProcessor.h"
#include "plugin/FactoryPresets.h"
#include "AllocationTracking.h"
#include <array>
#include <atomic>
#include <cmath>
#include <thread>

namespace
{
using disdorktion::PluginProcessor;

juce::RangedAudioParameter& parameter(PluginProcessor& plugin, const char* id)
{
    auto* result = plugin.parameterState().getParameter(id);
    REQUIRE(result != nullptr);
    return *result;
}

float hostValue(const char* id, float value)
{
    if (std::string_view(id) == "bitDepth")
        for (std::size_t i = 0; i < disdorktion::bitCrusherDepths.size(); ++i)
            if (value == static_cast<float>(disdorktion::bitCrusherDepths[i]))
                return static_cast<float>(i);
    return value;
}

void setControl(PluginProcessor& plugin, const char* id, float value)
{
    auto& target = parameter(plugin, id);
    target.setValueNotifyingHost(target.convertTo0to1(hostValue(id, value)));
}

std::unique_ptr<juce::XmlElement> savedState(PluginProcessor& plugin)
{
    juce::MemoryBlock bytes;
    plugin.getStateInformation(bytes);
    return juce::AudioProcessor::getXmlFromBinary(bytes.getData(), static_cast<int>(bytes.getSize()));
}

void restoreState(PluginProcessor& plugin, const juce::XmlElement& xml)
{
    juce::MemoryBlock bytes;
    juce::AudioProcessor::copyXmlToBinary(xml, bytes);
    plugin.setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
}

juce::XmlElement legacyState(int version, PluginProcessor& plugin)
{
    if (version == 3)
    {
        auto xml = savedState(plugin);
        xml->setAttribute("version", version);
        xml->setAttribute("mode", "legacyGain");
        return *xml;
    }
    juce::XmlElement xml("DisDorktionState");
    xml.setAttribute("version", version);
    xml.setAttribute("gain", "2");
    xml.setAttribute("bypass", "0");
    if (version == 2)
        for (const auto* id : {"inputGainDb", "inputMute", "inputBypass", "outputGainDb", "outputMute", "outputBypass"})
            xml.setAttribute(id, "0");
    return xml;
}

void fillSignal(juce::AudioBuffer<float>& audio, int offset = 0)
{
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        for (int frame = 0; frame < audio.getNumSamples(); ++frame)
            audio.setSample(channel, frame, 0.3f * std::sin(0.071f * static_cast<float>(offset + frame)));
}

void requireClear(const disdorktion::PluginMeters& meters)
{
    REQUIRE(meters.inputPeak == 0.0f);
    REQUIRE(meters.outputPeak == 0.0f);
    REQUIRE_FALSE(meters.inputOverload);
    REQUIRE_FALSE(meters.outputOverload);
}
}

TEST_CASE("Factory presets contain complete valid settings and stable identities", "[adapter][preset]")
{
    constexpr std::array keys {"initialize", "subtle-warmth", "crunch", "folded", "digital-destruction"};
    REQUIRE(disdorktion::factoryPresets.size() == keys.size());
    PluginProcessor plugin;
    for (std::size_t index = 0; index < keys.size(); ++index)
    {
        const auto& preset = disdorktion::factoryPresets[index];
        CAPTURE(index);
        REQUIRE(std::string_view(preset.key) == keys[index]);
        REQUIRE(std::string_view(preset.name).size() > 0);
        REQUIRE(disdorktion::isValidFullChainSettings(preset.settings));
        REQUIRE(plugin.loadFactoryPreset(static_cast<int>(index)));
        REQUIRE(plugin.matchingFactoryPreset() == static_cast<int>(index));
        for (const auto& control : disdorktion::fullChainControls)
        {
            CAPTURE(control.id);
            auto& target = parameter(plugin, control.id);
            const auto expected = target.convertTo0to1(hostValue(control.id,
                disdorktion::getFullChainControl(preset.settings, control.id)));
            REQUIRE(target.getValue() == Catch::Approx(expected).margin(2e-6f));
        }
    }
    const auto before = savedState(plugin);
    for (const int index : {-1, 5, 999})
    {
        REQUIRE_FALSE(plugin.loadFactoryPreset(index));
        REQUIRE(savedState(plugin)->isEquivalentTo(before.get(), true));
        REQUIRE(plugin.matchingFactoryPreset() == 4);
    }
}

TEST_CASE("Factory identity tolerates normalized roundoff and ignores only output mute", "[adapter][preset][state]")
{
    PluginProcessor plugin;
    REQUIRE(plugin.loadFactoryPreset(1));
    auto& mix = parameter(plugin, "globalMix");
    const auto original = mix.getValue();
    mix.setValueNotifyingHost(original + 1e-7f);
    REQUIRE(plugin.matchingFactoryPreset() == 1);
    setControl(plugin, "outputMute", 1);
    REQUIRE(plugin.matchingFactoryPreset() == 1);
    mix.setValueNotifyingHost(original + 0.01f);
    REQUIRE(plugin.matchingFactoryPreset() == -1);
    REQUIRE(plugin.loadFactoryPreset(1));
    setControl(plugin, "inputMute", 1);
    REQUIRE(plugin.matchingFactoryPreset() == -1);
    REQUIRE(plugin.loadFactoryPreset(1));
    setControl(plugin, "referenceGain", 2);
    REQUIRE(plugin.matchingFactoryPreset() == -1);
    REQUIRE(plugin.loadFactoryPreset(1));
    setControl(plugin, "referenceBypass", 1);
    REQUIRE(plugin.matchingFactoryPreset() == -1);
}

TEST_CASE("Presets reset compatibility trim preserve mute and roundtrip independently", "[adapter][preset][state]")
{
    PluginProcessor plugin;
    for (int index = 1; index < 5; ++index)
    {
        setControl(plugin, "referenceGain", 3);
        setControl(plugin, "referenceBypass", 1);
        setControl(plugin, "outputMute", 1);
        REQUIRE(plugin.loadFactoryPreset(index));
        REQUIRE(parameter(plugin, "referenceGain").convertFrom0to1(parameter(plugin, "referenceGain").getValue()) == Catch::Approx(1));
        REQUIRE(parameter(plugin, "referenceBypass").getValue() == 0);
        REQUIRE(parameter(plugin, "outputMute").getValue() == 1);
        const auto saved = savedState(plugin);
        PluginProcessor reopened;
        restoreState(reopened, *saved);
        REQUIRE(reopened.matchingFactoryPreset() == index);
        REQUIRE_FALSE(reopened.isLegacyMode());
        REQUIRE(savedState(reopened)->isEquivalentTo(saved.get(), true));
        REQUIRE(reopened.loadFactoryPreset(0));
        REQUIRE(plugin.matchingFactoryPreset() == index);
        REQUIRE(parameter(plugin, "outputMute").getValue() == 1);
    }
    const auto beforeReset = savedState(plugin);
    plugin.prepareToPlay(48000, 32);
    plugin.reset();
    REQUIRE(savedState(plugin)->isEquivalentTo(beforeReset.get(), true));
    REQUIRE(plugin.loadFactoryPreset(0));
    PluginProcessor defaults;
    REQUIRE(savedState(plugin)->isEquivalentTo(savedState(defaults).get(), true));
    REQUIRE(plugin.matchingFactoryPreset() == 0);
}

TEST_CASE("Preset recall exits each legacy schema at the next audio boundary", "[adapter][preset][state][realtime]")
{
    for (const int version : {1, 2, 3})
    {
        CAPTURE(version);
        PluginProcessor plugin;
        plugin.prepareToPlay(48000, 32);
        const auto fullLatency = plugin.getLatencySamples();
        REQUIRE(fullLatency > 0);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> audio(2, 617);
        fillSignal(audio);
        plugin.processBlock(audio, midi);
        restoreState(plugin, legacyState(version, plugin));
        REQUIRE(plugin.isLegacyMode());
        REQUIRE(plugin.getLatencySamples() == 0);
        fillSignal(audio);
        plugin.processBlock(audio, midi);
        REQUIRE(plugin.loadFactoryPreset(0));
        REQUIRE_FALSE(plugin.isLegacyMode());
        REQUIRE(plugin.getLatencySamples() == fullLatency);
        audio.clear();
        {
            disdorktion::test::ScopedAllocationTracking tracking;
            plugin.processBlock(audio, midi);
        }
        const auto counts = disdorktion::test::getAllocationCounts();
        REQUIRE(counts.allocations == 0);
        REQUIRE(counts.deallocations == 0);
        REQUIRE(audio.getMagnitude(0, audio.getNumSamples()) == 0);
    }
}

TEST_CASE("Restored presets reproduce deterministic offline rendering", "[adapter][preset][state][offline]")
{
    for (const double rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
        for (const int channels : {1, 2})
            for (int index = 0; index < 5; ++index)
            {
                CAPTURE(rate, channels, index);
                PluginProcessor original, restored;
                REQUIRE(original.loadFactoryPreset(index));
                restoreState(restored, *savedState(original));
                for (auto* plugin : {&original, &restored})
                {
                    juce::AudioProcessor::BusesLayout layout;
                    layout.inputBuses.add(channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
                    layout.outputBuses.add(layout.inputBuses[0]);
                    REQUIRE(plugin->setBusesLayout(layout));
                    plugin->setNonRealtime(true);
                    plugin->prepareToPlay(rate, 32);
                }
                juce::MidiBuffer midi;
                int offset = 0;
                for (const int frames : {0, 1, 32, 64, 127, 256, 1024})
                {
                    juce::AudioBuffer<float> actual(channels, frames), expected(channels, frames);
                    fillSignal(actual, offset);
                    expected.makeCopyOf(actual);
                    original.processBlock(actual, midi);
                    restored.processBlock(expected, midi);
                    for (int channel = 0; channel < channels; ++channel)
                        for (int frame = 0; frame < frames; ++frame)
                        {
                            REQUIRE(std::isfinite(actual.getSample(channel, frame)));
                            REQUIRE(actual.getSample(channel, frame) == expected.getSample(channel, frame));
                        }
                    offset += frames;
                }
            }
}

TEST_CASE("Meters retain raw input and actual host bypass output peaks between polls", "[adapter][meters]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay(48000, 32);
    requireClear(plugin.readMeters());
    setControl(plugin, "inputMute", 1);
    setControl(plugin, "outputMute", 1);
    setControl(plugin, "referenceGain", 4);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> audio(2, 1024);
    audio.clear();
    audio.setSample(0, 0, 1.25f);
    audio.setSample(1, 0, -1.5f);
    plugin.processBlockBypassed(audio, midi);
    const auto actualOutput = audio.getMagnitude(0, audio.getNumSamples());
    REQUIRE(actualOutput == 1.5f);
    for (int block = 0; block < 3; ++block)
    {
        audio.clear();
        plugin.processBlockBypassed(audio, midi);
    }
    auto meters = plugin.readMeters();
    REQUIRE(meters.inputPeak == 1.5f);
    REQUIRE(meters.outputPeak == actualOutput);
    REQUIRE(meters.inputOverload);
    REQUIRE(meters.outputOverload);
    meters = plugin.readMeters();
    REQUIRE(meters.inputPeak == 0);
    REQUIRE(meters.outputPeak == 0);
    REQUIRE(meters.inputOverload);
    REQUIRE(meters.outputOverload);
    plugin.clearMeterOverloads();
    REQUIRE(plugin.readMeters().inputOverload);
    audio.clear();
    plugin.processBlockBypassed(audio, midi);
    requireClear(plugin.readMeters());
    // Normal processing meters input before mute, and output after final mute.
    audio.clear();
    plugin.processBlock(audio, midi); // Allow the production mute ramps to settle.
    plugin.readMeters();
    audio.clear(); audio.setSample(0, 0, 0.75f);
    plugin.processBlock(audio, midi);
    meters = plugin.readMeters();
    REQUIRE(meters.inputPeak == 0.75f);
    REQUIRE(meters.outputPeak == 0);
    juce::AudioBuffer<float> zero(2, 0);
    plugin.processBlock(zero, midi);
    requireClear(plugin.readMeters());
    for (const bool release : {false, true})
    {
        audio.clear(); audio.setSample(0, 0, 2);
        plugin.processBlockBypassed(audio, midi);
        if (release) plugin.releaseResources(); else plugin.reset();
        requireClear(plugin.readMeters());
    }
}

TEST_CASE("Preset processing with active meters is allocation free for varying blocks", "[adapter][preset][meters][realtime]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay(48000, 32);
    juce::MidiBuffer midi;
    for (const int frames : {0, 1, 32, 127, 256, 1024, 2051})
    {
        juce::AudioBuffer<float> audio(2, frames);
        REQUIRE(plugin.loadFactoryPreset(4));
        fillSignal(audio);
        {
            disdorktion::test::ScopedAllocationTracking tracking;
            plugin.processBlock(audio, midi);
            plugin.readMeters();
            plugin.clearMeterOverloads();
            plugin.processBlockBypassed(audio, midi);
        }
        const auto counts = disdorktion::test::getAllocationCounts();
        REQUIRE(counts.allocations == 0);
        REQUIRE(counts.deallocations == 0);
    }
}

TEST_CASE("Message thread preset publication renders finite audio without audio allocations", "[adapter][preset][realtime][concurrency]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay(48000, 32);
    std::atomic<bool> started {false}, done {false};
    bool finite = true;
    std::uint64_t rendered = 0;
    disdorktion::test::AllocationCounts counts;
    std::thread renderer([&]
    {
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> audio(2, 127);
        {
            disdorktion::test::ScopedAllocationTracking tracking;
            started.store(true, std::memory_order_release);
            do
            {
                fillSignal(audio);
                plugin.processBlock(audio, midi);
                for (int channel = 0; channel < 2; ++channel)
                    for (int frame = 0; frame < audio.getNumSamples(); ++frame)
                        finite = finite && std::isfinite(audio.getSample(channel, frame));
                ++rendered;
            } while (!done.load(std::memory_order_acquire) || rendered < 100);
        }
        counts = disdorktion::test::getAllocationCounts();
    });
    while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
    bool loaded = true;
    for (int recall = 0; recall < 250; ++recall)
        loaded = plugin.loadFactoryPreset(recall % 5) && loaded;
    done.store(true, std::memory_order_release);
    renderer.join();
    REQUIRE(loaded);
    REQUIRE(rendered >= 100);
    REQUIRE(finite);
    REQUIRE(counts.allocations == 0);
    REQUIRE(counts.deallocations == 0);
}

TEST_CASE("Concurrent preset requests leave each legacy schema on the audio thread", "[adapter][preset][state][realtime][concurrency]")
{
    for (const int version : {1, 2, 3})
    {
        CAPTURE(version);
        PluginProcessor plugin;
        plugin.prepareToPlay(48000, 32);
        const auto expectedLatency = plugin.getLatencySamples();
        restoreState(plugin, legacyState(version, plugin));
        REQUIRE(plugin.isLegacyMode());
        REQUIRE(plugin.getLatencySamples() == 0);
        std::atomic<bool> legacyRendered {false}, recallPublished {false};
        bool finite = true;
        std::uint64_t legacyBlocks = 0, afterRequestBlocks = 0;
        disdorktion::test::AllocationCounts counts;
        std::thread renderer([&]
        {
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> audio(2, 127);
            const auto render = [&]
            {
                fillSignal(audio);
                plugin.processBlock(audio, midi);
                for (int channel = 0; channel < 2; ++channel)
                    for (int frame = 0; frame < audio.getNumSamples(); ++frame)
                        finite = finite && std::isfinite(audio.getSample(channel, frame));
            };
            {
                disdorktion::test::ScopedAllocationTracking tracking;
                // Main cannot publish recall until several old-mode callbacks
                // have completed, and this thread continues through publication.
                for (int block = 0; block < 4; ++block) { render(); ++legacyBlocks; }
                legacyRendered.store(true, std::memory_order_release);
                while (!recallPublished.load(std::memory_order_acquire)) render();
                // Acquiring publication before these callbacks ensures the mode
                // request is observed at an audio boundary before thread exit.
                for (int block = 0; block < 8; ++block) { render(); ++afterRequestBlocks; }
            }
            counts = disdorktion::test::getAllocationCounts();
        });
        while (!legacyRendered.load(std::memory_order_acquire)) std::this_thread::yield();
        const auto loaded = plugin.loadFactoryPreset(0);
        recallPublished.store(true, std::memory_order_release);
        renderer.join();
        REQUIRE(loaded);
        REQUIRE(legacyBlocks == 4);
        REQUIRE(afterRequestBlocks == 8);
        REQUIRE(finite);
        REQUIRE(counts.allocations == 0);
        REQUIRE(counts.deallocations == 0);
        REQUIRE_FALSE(plugin.isLegacyMode());
        REQUIRE(plugin.getLatencySamples() == expectedLatency);
        REQUIRE(plugin.matchingFactoryPreset() == 0);
        REQUIRE(savedState(plugin)->getStringAttribute("mode") == "fullChain");
    }
}

TEST_CASE("Unread meter impulses survive zero length callbacks", "[adapter][meters]")
{
    PluginProcessor plugin;
    plugin.prepareToPlay(48000, 32);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> impulse(2, 1024), zero(2, 0);
    impulse.clear();
    impulse.setSample(0, 0, 1.5f);
    impulse.setSample(1, 0, -1.25f);
    plugin.processBlockBypassed(impulse, midi);
    REQUIRE(impulse.getMagnitude(0, impulse.getNumSamples()) == 1.5f);
    plugin.processBlock(zero, midi);
    plugin.processBlockBypassed(zero, midi);
    const auto meters = plugin.readMeters();
    REQUIRE(meters.inputPeak == 1.5f);
    REQUIRE(meters.outputPeak == 1.5f);
    REQUIRE(meters.inputOverload);
    REQUIRE(meters.outputOverload);
    const auto consumed = plugin.readMeters();
    REQUIRE(consumed.inputPeak == 0);
    REQUIRE(consumed.outputPeak == 0);
    REQUIRE(consumed.inputOverload);
    REQUIRE(consumed.outputOverload);
}
