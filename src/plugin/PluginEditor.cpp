#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "FactoryPresets.h"
#include <cmath>

namespace disdorktion
{
namespace
{
const juce::Colour background {0xff10161e}, panel {0xff1a2430}, text {0xffe4edf4}, accent {0xff66dfbc};
}

// Components precede attachments so listeners are detached before their controls die.
class PluginEditor::ControlRow final : public juce::Component
{
public:
    ControlRow(juce::AudioProcessorValueTreeState& state, const char* id, const char* caption)
    {
        auto* parameter = state.getParameter(id);
        jassert(parameter != nullptr);
        label.setText(caption, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, text);
        label.setFont(juce::Font(11.0f));
        addAndMakeVisible(label);
        const juce::String parameterId(id);
        if (dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr)
        {
            toggle.setButtonText(caption);
            toggle.setComponentID(id);
            toggle.setTitle(caption);
            addAndMakeVisible(toggle);
            label.setVisible(false);
            buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, id, toggle);
        }
        else if (parameterId == "bitDepth" || parameterId == "hpResponse" || parameterId == "lpResponse")
        {
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameter))
                choiceBox.addItemList(choice->choices, 1);
            else
                choiceBox.addItemList({"Butterworth", "Chebyshev I", "Chebyshev II"}, 1);
            choiceBox.setComponentID(id);
            choiceBox.setTitle(caption);
            addAndMakeVisible(choiceBox);
            choiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, id, choiceBox);
        }
        else
        {
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 68, 18);
            slider.setComponentID(id);
            slider.setTitle(caption);
            slider.setScrollWheelEnabled(false);
            slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
            addAndMakeVisible(slider);
            sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, id, slider);
            // This affects only mouse travel in the editor; the host range stays linear.
            if (parameterId.endsWith("Hz"))
            {
                slider.setSkewFactorFromMidPoint(std::sqrt(slider.getMinimum() * slider.getMaximum()));
                slider.textFromValueFunction = [](double value) { return juce::String(value, 0) + " Hz"; };
                slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
            }
            else if (parameterId.endsWith("Db"))
            {
                slider.textFromValueFunction = [](double value) { return juce::String(value, 1) + " dB"; };
                slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
            }
            else if (parameterId == "referenceGain")
            {
                slider.textFromValueFunction = [](double value) { return juce::String(value, 2) + " x"; };
                slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
            }
            else
            {
                slider.textFromValueFunction = [](double value) { return juce::String(value * 100.0, 1) + " %"; };
                slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue() / 100.0; };
            }
            slider.updateText();
        }
    }

    bool hasKnob() const noexcept { return slider.isVisible(); }
    bool isSwitch() const noexcept { return toggle.isVisible(); }

    void resized() override
    {
        auto area = getLocalBounds();
        if (toggle.isVisible()) { toggle.setBounds(area); return; }
        label.setJustificationType(hasKnob() ? juce::Justification::centred : juce::Justification::centredLeft);
        label.setBounds(area.removeFromTop(14));
        slider.setBounds(area.withHeight(juce::jmin(78, area.getHeight())).reduced(1, 0));
        choiceBox.setBounds(area.reduced(2, 0));
    }

private:
    juce::Label label;
    juce::Slider slider;
    juce::ToggleButton toggle;
    juce::ComboBox choiceBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choiceAttachment;
};

class PluginEditor::PeakMeter final : public juce::Component
{
public:
    explicit PeakMeter(juce::String caption) : name(std::move(caption)) { setTitle(name + " peak meter"); }
    void update(float nextPeak, bool nextOverload)
    {
        peak = juce::jmax(nextPeak, peak * 0.89f);
        overload = nextOverload;
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        const auto db = juce::Decibels::gainToDecibels(peak, -60.0f);
        g.setColour(text);
        g.setFont(12.0f);
        g.drawText(name + "  " + (db <= -60.0f ? juce::String("-inf") : juce::String(db, 1)) + " dBFS",
                   area.removeFromTop(19), juce::Justification::centredLeft);
        auto lamp = area.removeFromRight(65);
        g.setColour(overload ? juce::Colour(0xffff6f75) : juce::Colour(0xff455363));
        g.drawText(overload ? "OVER" : "PEAK", lamp, juce::Justification::centred);
        g.setColour(juce::Colour(0xff0b1017));
        g.fillRoundedRectangle(area.toFloat(), 3.0f);
        g.setColour(overload ? juce::Colour(0xffff6f75) : accent);
        g.fillRoundedRectangle(area.withWidth(juce::roundToInt(area.getWidth() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f))).toFloat(), 3.0f);
    }
private:
    juce::String name;
    float peak = 0.0f;
    bool overload = false;
};

PluginEditor::PluginEditor(PluginProcessor& owner) : AudioProcessorEditor(owner), processor(owner)
{
    lookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, background);
    lookAndFeel.setColour(juce::Slider::thumbColourId, accent);
    lookAndFeel.setColour(juce::Slider::trackColourId, accent);
    lookAndFeel.setColour(juce::ToggleButton::textColourId, text);
    lookAndFeel.setColour(juce::ToggleButton::tickColourId, accent);
    lookAndFeel.setColour(juce::ComboBox::backgroundColourId, background);
    lookAndFeel.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff314357));
    setLookAndFeel(&lookAndFeel);
    title.setText("DisDorktion", juce::dontSendNotification);
    title.setFont(juce::Font(24.0f, juce::Font::bold));
    subtitle.setText("CRUSH / SATURATE / FOLD / FILTER", juce::dontSendNotification);
    subtitle.setFont(juce::Font(11.0f));
    compatibilityStatus.setFont(juce::Font(11.0f));
    for (auto* label : {&title, &subtitle, &presetStatus, &compatibilityStatus})
    {
        label->setColour(juce::Label::textColourId, text);
        addAndMakeVisible(*label);
    }
    for (std::size_t i = 0; i < factoryPresets.size(); ++i)
        presets.addItem(factoryPresets[i].name, static_cast<int>(i + 1));
    presets.setComponentID("factoryPreset");
    presets.setTitle("Factory preset");
    presets.setTextWhenNothingSelected("Modified");
    presets.onChange = [this] { if (presets.getSelectedId() > 0) { processor.loadFactoryPreset(presets.getSelectedId() - 1); refreshPresetAndMode(); } };
    initialize.setComponentID("initialize");
    initialize.onClick = [this] { processor.loadFactoryPreset(0); refreshPresetAndMode(); };
    clearOverloads.setComponentID("clearOverloads");
    clearOverloads.onClick = [this] { processor.clearMeterOverloads(); };
    addAndMakeVisible(presets);
    addAndMakeVisible(initialize);
    addAndMakeVisible(clearOverloads);

    addControl(0, "inputGainDb", "Input gain");
    addControl(0, "inputMute", "Mute input");
    addControl(0, "inputBypass", "Bypass input gain");
    addControl(0, "bitDepth", "Bit depth");
    addControl(0, "captureRateHz", "Capture rate");
    addControl(0, "crusherMix", "Crush mix");
    addControl(0, "crush", "Crush");
    addControl(0, "shape", "Shape");
    addControl(0, "jitter", "Jitter");
    addControl(0, "crusherBypass", "Bypass crusher");
    addControl(1, "tanhDriveDb", "Tanh drive");
    addControl(1, "tanhOutputDb", "Tanh trim");
    addControl(1, "tanhBypass", "Bypass tanh");
    addControl(1, "folderDriveDb", "Fold drive");
    addControl(1, "folderOutputDb", "Fold trim");
    addControl(1, "folderThreshold", "Threshold");
    addControl(1, "folderBypass", "Bypass folder");
    addControl(2, "hpCutoffHz", "High-pass");
    addControl(2, "hpResponse", "High-pass response");
    addControl(2, "hpBypass", "Bypass high-pass");
    addControl(2, "lpCutoffHz", "Low-pass");
    addControl(2, "lpResponse", "Low-pass response");
    addControl(2, "lpBypass", "Bypass low-pass");
    addControl(3, "globalMix", "Dry/wet");
    addControl(3, "outputGainDb", "Final gain");
    addControl(3, "outputMute", "Mute output");
    addControl(3, "outputBypass", "Bypass final gain");
    addControl(3, "chainBypass", "Bypass chain");
    addControl(4, "referenceGain", "Legacy trim");
    addControl(4, "referenceBypass", "Bypass trim");
    inputMeter = std::make_unique<PeakMeter>("Raw input");
    outputMeter = std::make_unique<PeakMeter>("Final output");
    addAndMakeVisible(*inputMeter);
    addAndMakeVisible(*outputMeter);
    setResizable(true, true);
    setResizeLimits(740, 520, 1400, 1000);
    setSize(820, 560);
    refreshPresetAndMode();
    startTimerHz(30);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void PluginEditor::addControl(int section, const char* id, const char* caption)
{
    auto row = std::make_unique<ControlRow>(processor.parameterState(), id, caption);
    addAndMakeVisible(*row);
    controls[static_cast<std::size_t>(section)].push_back(std::move(row));
}

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const char* names[] = {"01  INPUT", "02  MIDDLE", "03  OUTPUT", "GLOBAL", "COMPATIBILITY"};
    for (std::size_t i = 0; i < sections.size(); ++i)
    {
        g.setColour(panel);
        g.fillRoundedRectangle(sections[i].toFloat(), 9.0f);
        g.setColour(accent);
        g.setFont(juce::Font(14.0f, juce::Font::bold));
        g.drawText(names[i], sections[i].reduced(12, 6).removeFromTop(24), juce::Justification::centredLeft);
    }
}

void PluginEditor::resized()
{
    auto area = getLocalBounds().reduced(12);
    auto header = area.removeFromTop(48);
    auto branding = header.removeFromLeft(225);
    title.setBounds(branding.removeFromTop(28));
    subtitle.setBounds(branding.removeFromTop(16));
    initialize.setBounds(header.removeFromRight(88).withSizeKeepingCentre(84, 28));
    presetStatus.setBounds(header.removeFromRight(90).withSizeKeepingCentre(88, 28));
    presets.setBounds(header.reduced(8, 10));
    auto meterArea = area.removeFromBottom(40);
    clearOverloads.setBounds(meterArea.removeFromRight(130).reduced(3, 7));
    const auto meterWidth = meterArea.getWidth() / 2;
    inputMeter->setBounds(meterArea.removeFromLeft(meterWidth).reduced(7, 4));
    outputMeter->setBounds(meterArea.reduced(7, 4));
    area.removeFromBottom(8);
    auto bottom = area.removeFromBottom(112);
    sections[4] = bottom.removeFromRight(juce::jmax(240, bottom.getWidth() / 3));
    bottom.removeFromRight(8);
    sections[3] = bottom;
    area.removeFromBottom(8);
    const auto columnWidth = (area.getWidth() - 16) / 3;
    for (int i = 0; i < 3; ++i)
    {
        sections[static_cast<std::size_t>(i)] = area.removeFromLeft(columnWidth);
        area.removeFromLeft(8);
        auto cells = sections[static_cast<std::size_t>(i)].reduced(8);
        cells.removeFromTop(22);
        auto& group = controls[static_cast<std::size_t>(i)];
        int knobs = 0, choices = 0, switches = 0;
        for (const auto& control : group)
            if (control->hasKnob()) ++knobs;
            else if (control->isSwitch()) ++switches;
            else ++choices;
        auto switchArea = cells.removeFromBottom(switches * 21);
        auto choiceArea = cells.removeFromBottom(choices * 38);
        const int columns = i == 2 ? 2 : 3;
        const int knobRows = (knobs + columns - 1) / columns;
        const int cellWidth = cells.getWidth() / columns;
        const int cellHeight = cells.getHeight() / juce::jmax(1, knobRows);
        int knob = 0;
        for (auto& control : group)
        {
            if (control->hasKnob())
            {
                control->setBounds(cells.getX() + (knob % columns) * cellWidth,
                    cells.getY() + (knob / columns) * cellHeight, cellWidth, cellHeight);
                ++knob;
            }
            else if (control->isSwitch()) control->setBounds(switchArea.removeFromTop(21));
            else control->setBounds(choiceArea.removeFromTop(38));
        }
    }
    auto global = sections[3].reduced(8);
    global.removeFromTop(22);
    for (auto& control : controls[3])
        if (control->hasKnob()) control->setBounds(global.removeFromLeft(88));
    global.removeFromLeft(8);
    for (auto& control : controls[3])
        if (control->isSwitch()) control->setBounds(global.removeFromTop(23));
    compatibilityStatus.setBounds(sections[4].getRight() - 100, sections[4].getY() + 7, 92, 21);
    auto compatibility = sections[4].reduced(8);
    compatibility.removeFromTop(22);
    controls[4][0]->setBounds(compatibility.removeFromLeft(88));
    compatibility.removeFromLeft(8);
    controls[4][1]->setBounds(compatibility.withHeight(23));
}

void PluginEditor::refreshPresetAndMode()
{
    const auto matched = processor.matchingFactoryPreset();
    presets.setSelectedId(matched + 1, juce::dontSendNotification);
    presetStatus.setText(matched < 0 ? "Modified" : "Factory preset", juce::dontSendNotification);
    const auto legacy = processor.isLegacyMode();
    compatibilityStatus.setText(legacy ? "Legacy gain" : "Full chain", juce::dontSendNotification);
    compatibilityStatus.setTooltip(legacy
        ? "Effects are inactive. Select a factory preset or Initialize to enter the full chain."
        : "Legacy trim is applied before the dry/wet split.");
}

void PluginEditor::timerCallback()
{
    const auto meters = processor.readMeters();
    inputMeter->update(meters.inputPeak, meters.inputOverload);
    outputMeter->update(meters.outputPeak, meters.outputOverload);
    refreshPresetAndMode();
}
}
