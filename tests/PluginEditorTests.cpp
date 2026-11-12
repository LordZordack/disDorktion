#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

namespace
{
class ControlledPresetDialogs final : public disdorktion::PresetDialogs
{
public:
    juce::PopupMenu menu;
    std::function<void(int)> menuDone;
    std::function<void(juce::File)> fileDone;
    std::function<void(bool)> overwriteDone;
    juce::String error;
    juce::File suggested;
    bool saving = false;
    void showMenu(juce::PopupMenu value, juce::Component&, std::function<void(int)> done) override
    { menu = std::move(value); menuDone = std::move(done); }
    void chooseFile(bool save, const juce::File& path, std::function<void(juce::File)> done) override
    { saving = save; suggested = path; fileDone = std::move(done); }
    void showError(const juce::String& value) override { error = value; }
    void confirmOverwrite(const juce::File&, std::function<void(bool)> done) override
    { overwriteDone = std::move(done); }
    int findAction(const juce::String& label) const
    {
        for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();)
            if (it.getItem().text == label) return it.getItem().itemID;
        return 0;
    }
    bool enabled(const juce::String& label) const
    {
        for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();)
            if (it.getItem().text == label) return it.getItem().isEnabled;
        return false;
    }
    void select(const juce::String& label)
    {
        const auto id = findAction(label);
        REQUIRE(id != 0);
        auto done = std::move(menuDone);
        REQUIRE(static_cast<bool>(done));
        done(id);
    }
};

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
    for (const auto& parameter : disdorktion::fullChainControls) requireControl(parameter.id);
}
}

TEST_CASE("Production editor exposes all host controls at minimum and expanded sizes", "[adapter][editor]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    disdorktion::PluginProcessor processor;
    {
        disdorktion::PluginEditor editor(processor);
        REQUIRE(editor.getWidth() == 1120);
        REQUIRE(editor.getHeight() == 740);
        for (const auto size : {juce::Point<int>(1000, 680), juce::Point<int>(1120, 740), juce::Point<int>(1200, 800)})
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
        REQUIRE(findControl(editor, "presetMenu") != nullptr);
        REQUIRE(findControl(editor, "initialize") == nullptr);
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
    auto* depth = dynamic_cast<juce::ComboBox*>(findControl(editor, "bitDepth"));
    auto* response = dynamic_cast<juce::ComboBox*>(findControl(editor, "hpResponse"));
    REQUIRE(gain != nullptr); REQUIRE(depth != nullptr); REQUIRE(response != nullptr);
    gain->setValue(6.0, juce::sendNotificationSync);
    REQUIRE(state.getRawParameterValue("inputGainDb")->load() == Catch::Approx(6.0f));
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
    REQUIRE(depth->getSelectedItemIndex() == 2);

    disdorktion::PluginProcessor independent;
    disdorktion::PluginEditor independentEditor(independent);
    auto* independentGain = dynamic_cast<juce::Slider*>(findControl(independentEditor, "inputGainDb"));
    REQUIRE(independentGain != nullptr);
    REQUIRE(independentGain->getValue() == 0.0);
    gain->setValue(9.0, juce::sendNotificationSync);
    REQUIRE(independentGain->getValue() == 0.0);
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
        {"shape", "-25 %", -0.25, "%"},
        {"combDelayMs", "12.5 ms", 12.5, "ms"},
        {"combFeedforward", "-0.75", -0.75, "-0.75"},
        {"combFeedback", "0.9", 0.9, "0.90"},
        {"toneDb", "-4.5 dB", -4.5, "dB"}
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

TEST_CASE("Deferred preset menu selection survives refresh before and after edits", "[adapter][editor][presets][issue43-regression]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    disdorktion::PluginProcessor processor;
    auto dialogs = std::make_shared<ControlledPresetDialogs>();
    {
        disdorktion::PluginEditor editor(processor, dialogs);
        auto* presets = dynamic_cast<juce::TextButton*>(findControl(editor, "presetMenu"));
        auto* gain = dynamic_cast<juce::Slider*>(findControl(editor, "inputGainDb"));
        REQUIRE(presets != nullptr); REQUIRE(gain != nullptr);
        presets->onClick();
        REQUIRE_FALSE(dialogs->enabled("Delete preset"));
        // This ordering reproduced the previous ComboBox bug before replacement.
        juce::Thread::sleep(80);
        juce::Timer::callPendingTimersSynchronously();
        dialogs->select("Folded");
        REQUIRE(processor.matchingFactoryPreset() == 3);
        REQUIRE(presets->getButtonText() == "Folded");
        gain->setValue(6.0, juce::sendNotificationSync);
        REQUIRE(processor.matchingFactoryPreset() == -1);
        presets->onClick();
        juce::Thread::sleep(80);
        juce::Timer::callPendingTimersSynchronously();
        dialogs->select("Reset to defaults");
        REQUIRE(processor.matchingFactoryPreset() == 0);
        REQUIRE(processor.parameterState().getRawParameterValue("tanhDriveDb")->load() == 0.0f);
        REQUIRE(processor.parameterState().getRawParameterValue("folderDriveDb")->load() == 0.0f);
        REQUIRE(gain->getValue() == Catch::Approx(0.0));
        gain->setValue(9.0, juce::sendNotificationSync);
    }
    {
        disdorktion::PluginEditor reopened(processor, dialogs);
        auto* presets = dynamic_cast<juce::TextButton*>(findControl(reopened, "presetMenu"));
        REQUIRE(presets != nullptr);
        REQUIRE(presets->getButtonText().contains("*"));
        presets->onClick();
        dialogs->select("Subtle Warmth");
        REQUIRE(processor.matchingFactoryPreset() == 1);
    }
    disdorktion::PluginEditor reopened(processor, dialogs);
    auto* presets = dynamic_cast<juce::TextButton*>(findControl(reopened, "presetMenu"));
    REQUIRE(presets != nullptr);
    REQUIRE(presets->getButtonText() == "Subtle Warmth");
}

TEST_CASE("Preset dialogs cancel stale operations and retain user identity across editor reopen", "[adapter][editor][presets][issue43]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    disdorktion::PluginProcessor processor;
    auto dialogs = std::make_shared<ControlledPresetDialogs>();
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("disdorktion-editor-" + juce::Uuid().toString());
    struct Cleanup { juce::File file; ~Cleanup() { file.deleteRecursively(); } } cleanup {folder};
    const auto file = folder.getChildFile("My sound.disdorktionpreset");
    {
        disdorktion::PluginEditor editor(processor, dialogs);
        auto* button = dynamic_cast<juce::TextButton*>(findControl(editor, "presetMenu"));
        REQUIRE(button != nullptr);
        button->onClick();
        juce::StringArray labels;
        for (juce::PopupMenu::MenuItemIterator it(dialogs->menu); it.next();)
            if (!it.getItem().isSeparator) labels.add(it.getItem().text);
        REQUIRE(labels.joinIntoString("|") == "Save preset|Save preset as|Load preset|Delete preset|Reset to defaults|Factory presets|User presets");
        dialogs->select("Save preset as");
        REQUIRE(dialogs->saving);
        REQUIRE(dialogs->suggested.getParentDirectory() == processor.defaultUserPresetDirectory());
        auto cancelled = std::move(dialogs->fileDone);
        cancelled({});
        REQUIRE(processor.getPresetInfo().type == disdorktion::PresetType::factory);

        button->onClick(); dialogs->select("Save preset as");
        auto save = std::move(dialogs->fileDone); save(file);
        REQUIRE(file.existsAsFile());
        REQUIRE(processor.getPresetInfo().type == disdorktion::PresetType::user);
        button->onClick();
        REQUIRE(dialogs->enabled("Delete preset"));
        dialogs->select("Save preset as");
        juce::MemoryBlock restore;
        disdorktion::PluginProcessor other;
        REQUIRE(other.loadFactoryPreset(2));
        other.getStateInformation(restore);
        processor.setStateInformation(restore.getData(), static_cast<int>(restore.getSize()));
        auto stale = std::move(dialogs->fileDone);
        const auto staleFile = folder.getChildFile("stale.disdorktionpreset");
        stale(staleFile);
        REQUIRE_FALSE(staleFile.exists());
        REQUIRE(processor.matchingFactoryPreset() == 2);

        button->onClick(); dialogs->select("Load preset");
        REQUIRE_FALSE(dialogs->saving);
        auto load = std::move(dialogs->fileDone); load(file);
        REQUIRE(processor.getPresetInfo().file == file);
        button->onClick(); dialogs->select("Load preset");
        auto failedLoad = std::move(dialogs->fileDone); failedLoad(folder.getChildFile("missing.disdorktionpreset"));
        REQUIRE(dialogs->error.isNotEmpty());
        REQUIRE(processor.getPresetInfo().file == file);
    }
    {
        disdorktion::PluginEditor editor(processor, dialogs);
        auto* button = dynamic_cast<juce::TextButton*>(findControl(editor, "presetMenu"));
        REQUIRE(button->getButtonText() == "My sound");
        button->onClick(); dialogs->select("Delete preset");
        REQUIRE_FALSE(file.exists());
        REQUIRE(processor.matchingFactoryPreset() == 0);
        button->onClick();
        auto pendingMenu = std::move(dialogs->menuDone);
        REQUIRE(processor.loadFactoryPreset(2));
        pendingMenu(dialogs->findAction("Folded"));
        REQUIRE(processor.matchingFactoryPreset() == 2);
        button->onClick(); dialogs->select("Save preset as");
    }
    // A native completion may arrive after the editor has closed.
    auto afterClose = std::move(dialogs->fileDone);
    REQUIRE(static_cast<bool>(afterClose));
    afterClose(file);
    REQUIRE_FALSE(file.exists());
}

TEST_CASE("Save as confirms the normalized extension target before replacing it", "[adapter][editor][presets][issue43-overwrite]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    disdorktion::PluginProcessor processor;
    auto dialogs = std::make_shared<ControlledPresetDialogs>();
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("disdorktion-overwrite-" + juce::Uuid().toString());
    struct Cleanup { juce::File file; ~Cleanup() { file.deleteRecursively(); } } cleanup {folder};
    const auto file = folder.getChildFile("Existing.disdorktionpreset");
    REQUIRE(processor.savePresetAs(file).wasOk());
    const auto original = file.loadFileAsString();
    REQUIRE(processor.loadFactoryPreset(2));
    disdorktion::PluginEditor editor(processor, dialogs);
    auto* button = dynamic_cast<juce::TextButton*>(findControl(editor, "presetMenu"));
    button->onClick(); dialogs->select("Save preset as");
    auto choose = std::move(dialogs->fileDone); choose(folder.getChildFile("Existing"));
    REQUIRE(static_cast<bool>(dialogs->overwriteDone));
    REQUIRE(file.loadFileAsString() == original);
    auto cancel = std::move(dialogs->overwriteDone); cancel(false);
    REQUIRE(file.loadFileAsString() == original);
    button->onClick(); dialogs->select("Save preset as");
    choose = std::move(dialogs->fileDone); choose(folder.getChildFile("Existing"));
    auto confirm = std::move(dialogs->overwriteDone); confirm(true);
    REQUIRE(file.loadFileAsString() != original);
    REQUIRE(processor.getPresetInfo().file == file);
}

TEST_CASE("Overwrite confirmation cannot save after restore or editor closure", "[adapter][editor][presets][issue43-overwrite]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    disdorktion::PluginProcessor processor;
    auto dialogs = std::make_shared<ControlledPresetDialogs>();
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("disdorktion-confirm-" + juce::Uuid().toString() + ".disdorktionpreset");
    struct Cleanup { juce::File file; ~Cleanup() { file.deleteFile(); } } cleanup {file};
    REQUIRE(processor.savePresetAs(file).wasOk());
    const auto original = file.loadFileAsString();
    auto editor = std::make_unique<disdorktion::PluginEditor>(processor, dialogs);
    auto* button = dynamic_cast<juce::TextButton*>(findControl(*editor, "presetMenu"));
    button->onClick(); dialogs->select("Save preset as");
    auto choose = std::move(dialogs->fileDone); choose(file);
    REQUIRE(static_cast<bool>(dialogs->overwriteDone));
    juce::MemoryBlock restored;
    disdorktion::PluginProcessor other;
    REQUIRE(other.loadFactoryPreset(2)); other.getStateInformation(restored);
    processor.setStateInformation(restored.getData(), static_cast<int>(restored.getSize()));
    auto stale = std::move(dialogs->overwriteDone); stale(true);
    REQUIRE(file.loadFileAsString() == original);
    REQUIRE(processor.getPresetInfo().factoryIndex == 2);
    button->onClick(); dialogs->select("Save preset as");
    choose = std::move(dialogs->fileDone); choose(file);
    auto afterClose = std::move(dialogs->overwriteDone);
    REQUIRE(static_cast<bool>(afterClose));
    editor.reset(); afterClose(true);
    REQUIRE(file.loadFileAsString() == original);
}

TEST_CASE("Editor has no mute bypass or damping enable and displays Off endpoints", "[adapter][editor][issue38]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    disdorktion::PluginProcessor processor; disdorktion::PluginEditor editor(processor);
    for(const auto* id : {"inputMute","outputMute","inputBypass","chainBypass","referenceGain","referenceBypass","combDampingEnabled"})
        REQUIRE(findControl(editor,id)==nullptr);
    for(const auto* id : {"tanhDriveDb","folderDriveDb","hpCutoffHz","lpCutoffHz"})
    {
        auto* slider=dynamic_cast<juce::Slider*>(findControl(editor,id)); REQUIRE(slider);
        const auto off=juce::String(id)=="lpCutoffHz" ? slider->getMaximum() : slider->getMinimum();
        REQUIRE(slider->getTextFromValue(off)=="Off");
        REQUIRE(slider->getValueFromText("Off")==off);
    }
    for(const auto* id : {"captureRateHz","hpCutoffHz","lpCutoffHz"})
    {
        auto* slider=dynamic_cast<juce::Slider*>(findControl(editor,id)); REQUIRE(slider);
        const auto lo=slider->getMinimum(), hi=slider->getMaximum();
        for(const double fraction : {0.25,0.5,0.75})
            REQUIRE(slider->proportionOfLengthToValue(fraction)==Catch::Approx(lo*std::pow(hi/lo,fraction)).epsilon(2e-6));
        REQUIRE(slider->getValueFromText("1234 Hz")==1234.0);
    }
    const auto input=editor.getLocalArea(findControl(editor,"inputGainDb"),findControl(editor,"inputGainDb")->getLocalBounds());
    const auto driver=editor.getLocalArea(findControl(editor,"driver"),findControl(editor,"driver")->getLocalBounds());
    REQUIRE(input.getY()==driver.getY()); REQUIRE(input.getX()<driver.getX());
}

TEST_CASE("Editor Waveshaper controls follow processing order and new controls accept numeric entry", "[adapter][editor][issue38]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    disdorktion::PluginProcessor processor; disdorktion::PluginEditor editor(processor);
    juce::Rectangle<int> previous;
    for(const auto* id : {"tanhDriveDb","folderDriveDb","folderThreshold","waveshaperTrimDb","waveshaperMix"})
    {
        auto* control=findControl(editor,id); REQUIRE(control);
        const auto bounds=editor.getLocalArea(control,control->getLocalBounds());
        if(!previous.isEmpty()) REQUIRE((bounds.getY()>previous.getY() || (bounds.getY()==previous.getY() && bounds.getX()>previous.getX())));
        previous=bounds;
    }
    struct Entry {const char* id; const char* text; double value;};
    for(const auto& entry : {Entry{"driver","2.5 x",2.5},Entry{"waveshaperTrimDb","-3 dB",-3},Entry{"waveshaperMix","25 %",0.25}})
    {
        auto* slider=dynamic_cast<juce::Slider*>(findControl(editor,entry.id)); REQUIRE(slider);
        REQUIRE(slider->getValueFromText(entry.text)==entry.value);
        slider->setValue(slider->getValueFromText(entry.text),juce::sendNotificationSync);
        REQUIRE(processor.parameterState().getRawParameterValue(entry.id)->load()==Catch::Approx(entry.value));
    }
}
