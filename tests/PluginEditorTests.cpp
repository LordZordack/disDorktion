#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

namespace
{
juce::Component* findControl(juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id) return &root;
    for (auto* child : root.getChildren())
        if (auto* found = findControl(*child, id)) return found;
    return nullptr;
}

void requireVisibleControls(disdorktion::PluginEditor& editor)
{
    std::vector<juce::Rectangle<int>> occupied;
    const auto requireControl = [&](const char* id)
    {
        auto* control = findControl(editor, id);
        REQUIRE(control != nullptr);
        REQUIRE(control->isVisible());
        const auto bounds = editor.getLocalArea(control, control->getLocalBounds());
        REQUIRE(bounds.getWidth() >= 40);
        REQUIRE(bounds.getHeight() >= 20);
        REQUIRE(editor.getLocalBounds().contains(bounds));
        for (const auto& other : occupied) REQUIRE_FALSE(bounds.intersects(other));
        occupied.push_back(bounds);
        if (const auto* knob = dynamic_cast<juce::Slider*>(control))
            REQUIRE(knob->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag);
    };
    requireControl("referenceGain");
    requireControl("referenceBypass");
    for (const auto& parameter : disdorktion::fullChainControls) requireControl(parameter.id);
}
}

TEST_CASE("Production editor exposes all host controls at minimum and expanded sizes", "[adapter][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    disdorktion::PluginProcessor processor;
    {
        disdorktion::PluginEditor editor(processor);
        REQUIRE(editor.getWidth() == 820);
        REQUIRE(editor.getHeight() == 560);
        for (const auto size : {juce::Point<int>(740, 520), juce::Point<int>(820, 560), juce::Point<int>(1200, 800)})
        {
            editor.setSize(size.x, size.y);
            requireVisibleControls(editor);
            const auto snapshotDirectory = juce::SystemStats::getEnvironmentVariable("DISDORKTION_EDITOR_SNAPSHOT_DIR", {});
            if (snapshotDirectory.isNotEmpty())
            {
                const auto target = juce::File(snapshotDirectory).getChildFile("issue14-editor-" + juce::String(size.x) + ".png");
                auto stream = target.createOutputStream();
                REQUIRE(stream != nullptr);
                REQUIRE(stream->setPosition(0));
                REQUIRE(stream->truncate().wasOk());
                juce::PNGImageFormat png;
                REQUIRE(png.writeImageToStream(editor.createComponentSnapshot(editor.getLocalBounds()), *stream));
            }
        }
        REQUIRE(findControl(editor, "factoryPreset") != nullptr);
        REQUIRE(findControl(editor, "initialize") != nullptr);
        REQUIRE(findControl(editor, "clearOverloads") != nullptr);
    }
    disdorktion::PluginEditor reopened(processor);
    requireVisibleControls(reopened);
}

TEST_CASE("Editor attachments synchronize sliders choices buttons and active restore", "[adapter][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    disdorktion::PluginProcessor processor;
    disdorktion::PluginEditor editor(processor);
    auto& state = processor.parameterState();
    auto* gain = dynamic_cast<juce::Slider*>(findControl(editor, "inputGainDb"));
    auto* mute = dynamic_cast<juce::ToggleButton*>(findControl(editor, "outputMute"));
    auto* depth = dynamic_cast<juce::ComboBox*>(findControl(editor, "bitDepth"));
    auto* response = dynamic_cast<juce::ComboBox*>(findControl(editor, "hpResponse"));
    REQUIRE(gain != nullptr); REQUIRE(mute != nullptr); REQUIRE(depth != nullptr); REQUIRE(response != nullptr);
    gain->setValue(6.0, juce::sendNotificationSync);
    REQUIRE(state.getRawParameterValue("inputGainDb")->load() == Catch::Approx(6.0f));
    mute->setToggleState(true, juce::sendNotificationSync);
    REQUIRE(state.getRawParameterValue("outputMute")->load() == 1.0f);
    depth->setSelectedItemIndex(2, juce::sendNotificationSync);
    REQUIRE(state.getRawParameterValue("bitDepth")->load() == 2.0f);
    response->setSelectedItemIndex(1, juce::sendNotificationSync);
    REQUIRE(state.getRawParameterValue("hpResponse")->load() == 1.0f);

    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    auto* gainParameter = state.getParameter("inputGainDb");
    gainParameter->setValueNotifyingHost(gainParameter->convertTo0to1(-12.0f));
    REQUIRE(gain->getValue() == Catch::Approx(-12.0f));
    processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(gain->getValue() == Catch::Approx(6.0f));
    REQUIRE(mute->getToggleState());
    REQUIRE(depth->getSelectedItemIndex() == 2);

    disdorktion::PluginProcessor independent;
    disdorktion::PluginEditor independentEditor(independent);
    auto* independentGain = dynamic_cast<juce::Slider*>(findControl(independentEditor, "inputGainDb"));
    REQUIRE(independentGain != nullptr);
    REQUIRE(independentGain->getValue() == 0.0);
    gain->setValue(9.0, juce::sendNotificationSync);
    REQUIRE(independentGain->getValue() == 0.0);
}

TEST_CASE("Active editor follows every supported state mode and rejects malformed restore", "[adapter][editor][state]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    for (const auto version : {1, 2, 3})
        for (const bool legacy : {false, true})
        {
            if (version < 3 && !legacy) continue;
            CAPTURE(version, legacy);
            disdorktion::PluginProcessor processor;
            disdorktion::PluginEditor editor(processor);
            juce::MemoryBlock defaults;
            processor.getStateInformation(defaults);
            auto xml = juce::AudioProcessor::getXmlFromBinary(defaults.getData(), static_cast<int>(defaults.getSize()));
            REQUIRE(xml != nullptr);
            xml->setAttribute("version", juce::String(version));
            xml->setAttribute("mode", legacy ? "legacyGain" : "fullChain");
            xml->setAttribute("gain", "2");
            xml->setAttribute("bypass", "1");
            xml->setAttribute("inputGainDb", "6");
            juce::MemoryBlock saved;
            juce::AudioProcessor::copyXmlToBinary(*xml, saved);
            processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            auto* trim = dynamic_cast<juce::Slider*>(findControl(editor, "referenceGain"));
            auto* bypass = dynamic_cast<juce::ToggleButton*>(findControl(editor, "referenceBypass"));
            auto* input = dynamic_cast<juce::Slider*>(findControl(editor, "inputGainDb"));
            REQUIRE(trim != nullptr); REQUIRE(bypass != nullptr); REQUIRE(input != nullptr);
            REQUIRE(trim->getValue() == Catch::Approx(2.0));
            REQUIRE(bypass->getToggleState());
            REQUIRE(input->getValue() == Catch::Approx(version == 1 ? 0.0 : 6.0));
            REQUIRE(processor.isLegacyMode() == legacy);

            // Invalid trim rejects the complete transaction, including later valid controls.
            xml->setAttribute("gain", "NaN");
            xml->setAttribute("inputGainDb", "-12");
            juce::AudioProcessor::copyXmlToBinary(*xml, saved);
            processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            REQUIRE(trim->getValue() == Catch::Approx(2.0));
            REQUIRE(input->getValue() == Catch::Approx(version == 1 ? 0.0 : 6.0));
            REQUIRE(processor.isLegacyMode() == legacy);
            processor.setStateInformation(nullptr, 0);
            const char malformed[] = "invalid state";
            processor.setStateInformation(malformed, sizeof(malformed));
            REQUIRE(trim->getValue() == Catch::Approx(2.0));
            REQUIRE(input->getValue() == Catch::Approx(version == 1 ? 0.0 : 6.0));
        }
}

TEST_CASE("Editor numeric entry preserves readable Hz dB and percent units", "[adapter][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    disdorktion::PluginProcessor processor;
    disdorktion::PluginEditor editor(processor);
    struct Entry { const char* id; const char* typed; double expected; const char* suffix; };
    const Entry entries[] {
        {"hpCutoffHz", "1234 Hz", 1234.0, "Hz"},
        {"tanhDriveDb", "6.5 dB", 6.5, "dB"},
        {"globalMix", "37.5 %", 0.375, "%"},
        {"shape", "-25 %", -0.25, "%"}
    };
    for (const auto& entry : entries)
    {
        CAPTURE(entry.id);
        auto* slider = dynamic_cast<juce::Slider*>(findControl(editor, entry.id));
        REQUIRE(slider != nullptr);
        REQUIRE(slider->getValueFromText(entry.typed) == Catch::Approx(entry.expected));
        REQUIRE(slider->getTextFromValue(entry.expected).contains(entry.suffix));
        slider->setValue(slider->getValueFromText(entry.typed), juce::sendNotificationSync);
        REQUIRE(processor.parameterState().getRawParameterValue(entry.id)->load() == Catch::Approx(entry.expected));
    }
}

TEST_CASE("Editor preset actions recall initialize and derive modified status on reopening", "[adapter][editor][presets]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    disdorktion::PluginProcessor processor;
    {
        disdorktion::PluginEditor editor(processor);
        auto* presets = dynamic_cast<juce::ComboBox*>(findControl(editor, "factoryPreset"));
        auto* initialize = dynamic_cast<juce::TextButton*>(findControl(editor, "initialize"));
        auto* gain = dynamic_cast<juce::Slider*>(findControl(editor, "inputGainDb"));
        REQUIRE(presets != nullptr); REQUIRE(initialize != nullptr); REQUIRE(gain != nullptr);
        presets->setSelectedItemIndex(3, juce::sendNotificationSync);
        REQUIRE(processor.matchingFactoryPreset() == 3);
        REQUIRE(presets->getSelectedItemIndex() == 3);
        REQUIRE_FALSE(processor.isLegacyMode());
        gain->setValue(6.0, juce::sendNotificationSync);
        REQUIRE(processor.matchingFactoryPreset() == -1);
        REQUIRE(static_cast<bool>(initialize->onClick));
        initialize->onClick();
        REQUIRE(processor.matchingFactoryPreset() == 0);
        REQUIRE(presets->getSelectedItemIndex() == 0);
        REQUIRE(gain->getValue() == Catch::Approx(0.0));
        gain->setValue(9.0, juce::sendNotificationSync);
    }
    {
        disdorktion::PluginEditor reopened(processor);
        auto* presets = dynamic_cast<juce::ComboBox*>(findControl(reopened, "factoryPreset"));
        REQUIRE(presets != nullptr);
        REQUIRE(presets->getSelectedId() == 0);
        REQUIRE(presets->getTextWhenNothingSelected() == "Modified");
        presets->setSelectedItemIndex(1, juce::sendNotificationSync);
        REQUIRE(processor.matchingFactoryPreset() == 1);
    }
    disdorktion::PluginEditor reopened(processor);
    auto* presets = dynamic_cast<juce::ComboBox*>(findControl(reopened, "factoryPreset"));
    REQUIRE(presets != nullptr);
    REQUIRE(presets->getSelectedItemIndex() == 1);
}
