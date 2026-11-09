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

TEST_CASE("Presets reset Driver and section controls and roundtrip independently", "[adapter][preset][state]")
{
    PluginProcessor plugin;
    for (int index = 0; index < 5; ++index)
    {
        setControl(plugin, "driver", 4);
        setControl(plugin, "waveshaperMix", 0.25f);
        REQUIRE(plugin.loadFactoryPreset(index));
        REQUIRE(parameter(plugin, "driver").convertFrom0to1(parameter(plugin, "driver").getValue()) == 1.0f);
        const auto saved = savedState(plugin);
        PluginProcessor reopened;
        restoreState(reopened, *saved);
        REQUIRE(reopened.matchingFactoryPreset() == index);
        INFO("Saved: " << saved->toString().toStdString());
        INFO("Restored: " << savedState(reopened)->toString().toStdString());
        REQUIRE(savedState(reopened)->isEquivalentTo(saved.get(), true));
        setControl(reopened, "driver", 2);
        REQUIRE(reopened.matchingFactoryPreset() == -1);
        REQUIRE(plugin.matchingFactoryPreset() == index);
    }
    REQUIRE(plugin.loadFactoryPreset(0));
    PluginProcessor defaults;
    REQUIRE(savedState(plugin)->isEquivalentTo(savedState(defaults).get(), true));
}

TEST_CASE("Meters capture raw input and actual delayed host bypass output", "[adapter][meters]")
{
    PluginProcessor plugin;
    setControl(plugin, "driver", 4);
    setControl(plugin, "outputGainDb", -60);
    plugin.prepareToPlay(48000, 32);
    requireClear(plugin.readMeters());
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> audio(2, 1024);
    audio.clear(); audio.setSample(0, 0, 1.5f);
    plugin.processBlockBypassed(audio, midi);
    REQUIRE(audio.getMagnitude(0, audio.getNumSamples()) == 1.5f);
    const auto meters = plugin.readMeters();
    REQUIRE(meters.inputPeak == 1.5f);
    REQUIRE(meters.outputPeak == 1.5f);
    REQUIRE(meters.inputOverload); REQUIRE(meters.outputOverload);
    plugin.clearMeterOverloads();
    audio.clear(); plugin.processBlock(audio, midi);
    REQUIRE_FALSE(plugin.readMeters().inputOverload);
    plugin.reset(); requireClear(plugin.readMeters());
    plugin.releaseResources(); requireClear(plugin.readMeters());
}

TEST_CASE("Factory preset direct settings agree with parameter mapping", "[adapter][preset][issue38]")
{
    for(const auto& preset : disdorktion::factoryPresets)
    {
        CAPTURE(preset.key);
        disdorktion::FullChainSettings mapped;
        for(const auto& item : disdorktion::fullChainControls)
            REQUIRE(disdorktion::setFullChainControl(mapped,item.id,disdorktion::getFullChainControl(preset.settings,item.id)));
        REQUIRE(preset.settings.middle.tanh.bypass==mapped.middle.tanh.bypass);
        REQUIRE(preset.settings.middle.folder.bypass==mapped.middle.folder.bypass);
        REQUIRE(preset.settings.output.highPass.bypass==mapped.output.highPass.bypass);
        REQUIRE(preset.settings.output.lowPass.bypass==mapped.output.lowPass.bypass);
        REQUIRE(preset.settings.output.comb.dampingEnabled);
        REQUIRE(preset.settings.middle.tanh.outputDb==0);
        REQUIRE(preset.settings.middle.folder.outputDb==0);
        PluginProcessor plugin; disdorktion::FullChain reference;
        REQUIRE(plugin.loadFactoryPreset(static_cast<int>(&preset-disdorktion::factoryPresets.data())));
        REQUIRE(reference.setParameters(preset.settings)); REQUIRE(reference.prepare({48000,32,2}));
        plugin.prepareToPlay(48000,32);
        juce::MidiBuffer midi;
        for(const int frames : {1,127,2051})
        {
            juce::AudioBuffer<float> actual(2,frames),expected(2,frames);
            fillSignal(actual); expected.makeCopyOf(actual);
            plugin.processBlock(actual,midi); REQUIRE(reference.process(juce::dsp::AudioBlock<float>(expected)));
            for(int c=0;c<2;++c) for(int n=0;n<frames;++n)
                REQUIRE(actual.getSample(c,n)==Catch::Approx(expected.getSample(c,n)).margin(3e-5f));
        }
    }
}
