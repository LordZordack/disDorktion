#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "FactoryPresets.h"
#include <cmath>

namespace disdorktion
{
namespace
{
const juce::Colour background {0xff10161e}, panel {0xff1a2430}, text {0xffe4edf4}, accent {0xff66dfbc};
constexpr int editorWidth = 1120, editorHeight = 740;
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
        label.setFont(juce::Font(12.0f));
        label.setMinimumHorizontalScale(1.0f);
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
        else
        {
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 92, 20);
            slider.setComponentID(id);
            slider.setTitle(caption);
            slider.setScrollWheelEnabled(false);
            slider.setDoubleClickReturnValue(true, parameter->convertFrom0to1(parameter->getDefaultValue()));
            addAndMakeVisible(slider);
            sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, id, slider);
            if (parameterId == "bitDepth" || parameterId == "hpResponse" || parameterId == "lpResponse")
            {
                const juce::StringArray values = parameterId == "bitDepth"
                    ? juce::StringArray{"1", "2", "3", "4", "5", "6", "7", "8", "16", "32", "64"}
                    : juce::StringArray{"Butterworth", "Chebyshev I", "Chebyshev II"};
                slider.setRange(0, values.size() - 1, 1);
                slider.textFromValueFunction = [values](double value) { return values[juce::jlimit(0, values.size()-1, juce::roundToInt(value))]; };
                slider.valueFromTextFunction = [values, fallback = slider.getDoubleClickReturnValue()](const juce::String& value)
                {
                    for (int i=0;i<values.size();++i) if(values[i].equalsIgnoreCase(value.trim())) return static_cast<double>(i);
                    return fallback;
                };
            }
            else if (parameterId.endsWith("Hz"))
            {
                // Mouse movement is logarithmic; host plain-value conversion
                // stays linear so repeated state restores do not accumulate
                // float log/pow roundoff. Attachments exchange plain values.
                slider.setNormalisableRange(juce::NormalisableRange<double>(slider.getMinimum(), slider.getMaximum(),
                    [](double lo, double hi, double t)
                    {
                        if (t <= 0) return lo;
                        if (t >= 1) return hi;
                        return lo * std::pow(hi / lo, t);
                    },
                    [](double lo, double hi, double value)
                    {
                        return std::log(value / lo) / std::log(hi / lo);
                    }));
                slider.textFromValueFunction = [](double value) { return juce::String(value, 0) + " Hz"; };
                slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
            }
            else if (parameterId.endsWith("Db"))
            {
                slider.textFromValueFunction = [](double value) { return juce::String(value, 1) + " dB"; };
                slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
            }
            else if (parameterId == "driver")
            {
                slider.textFromValueFunction = [](double value) { return juce::String(value, 2) + " x"; };
                slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
            }
            else if (parameterId.endsWith("Ms"))
            {
                slider.textFromValueFunction = [](double value) { return juce::String(value, 2) + " ms"; };
                slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
            }
            else if (parameterId == "combFeedforward" || parameterId == "combFeedback")
            {
                slider.textFromValueFunction = [](double value) { return juce::String(value, 2); };
                slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue(); };
            }
            else
            {
                slider.textFromValueFunction = [](double value) { return juce::String(value * 100.0, 1) + " %"; };
                slider.valueFromTextFunction = [](const juce::String& value) { return value.getDoubleValue() / 100.0; };
            }
            const auto minimum = slider.getMinimum(), maximum = slider.getMaximum();
            if (parameterId == "tanhDriveDb" || parameterId == "folderDriveDb" || parameterId == "hpCutoffHz" || parameterId == "lpCutoffHz")
            {
                const auto previousFormatter = slider.textFromValueFunction;
                slider.textFromValueFunction = [parameterId, minimum, maximum, previousFormatter](double value)
                {
                    if ((parameterId == "lpCutoffHz" && value >= maximum) || (parameterId != "lpCutoffHz" && value <= minimum))
                        return juce::String("Off");
                    return previousFormatter(value);
                };
                slider.valueFromTextFunction = [parameterId, minimum, maximum](const juce::String& value)
                {
                    if (value.trim().equalsIgnoreCase("Off")) return parameterId == "lpCutoffHz" ? maximum : minimum;
                    return value.getDoubleValue();
                };
            }
            if (parameterId == "driver")
                slider.setTooltip("Drive tanh and folding from 1x to 4x, with gentle fixed compensation. Dry and crusher levels stay unchanged; use Waveshaper Trim for manual level matching.");
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
        label.setBounds(area.removeFromTop(32));
        slider.setBounds(area.reduced(2, 1));
    }

private:
    juce::Label label;
    juce::Slider slider;
    juce::ToggleButton toggle;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
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

// Only the message thread owns the displayed history. Each poll is bounded.
class PluginEditor::WaveformDisplay final : public juce::Component
{
public:
    WaveformDisplay(WaveformHistory& source, juce::String caption) : history(source), name(std::move(caption)), currentEpoch(source.epoch())
    {
        history.discardPending();
        setTitle(name + " waveform, fixed -1 to +1 amplitude");
        setDescription(name + " waveform: 0 frames");
    }
    void poll()
    {
        const auto epoch = history.epoch();
        if (epoch != currentEpoch) { count = 0; currentEpoch = epoch; }
        std::array<WaveformHistory::Frame, WaveformHistory::capacity> incoming;
        const auto received = history.drain(incoming.data(), incoming.size());
        for (std::size_t i = 0; i < received; ++i)
        {
            const auto& frame = incoming[i];
            if (frame.epoch != currentEpoch) { count = 0; currentEpoch = frame.epoch; }
            if (count != 0 && frame.sequence != lastSequence + 1) count = 0;
            frames[write] = frame;
            write = (write + 1) % frames.size();
            count = juce::jmin(count + 1, frames.size()); lastSequence = frame.sequence;
        }
        if (history.epoch() != currentEpoch) { count = 0; currentEpoch = history.epoch(); }
        setDescription(name + " waveform: " + juce::String(static_cast<int>(count)) + " frames");
        repaint();
    }
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(panel); g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(text); g.setFont(12.0f);
        g.drawText(name + "   -1 / +1", getLocalBounds().reduced(10, 5).removeFromTop(18), juce::Justification::centredLeft);
        auto plot = bounds.reduced(10, 9); plot.removeFromTop(20);
        g.setColour(text.withAlpha(0.15f)); g.drawHorizontalLine(juce::roundToInt(plot.getCentreY()),plot.getX(),plot.getRight());
        g.setColour(accent);
        for (std::size_t i=0;i<count;++i)
        {
            const auto& frame=frames[(write+frames.size()-count+i)%frames.size()];
            const float x=plot.getRight()-static_cast<float>(count-1-i)*plot.getWidth()/static_cast<float>(frames.size()-1);
            const auto y=[&](float v){ return plot.getCentreY()-juce::jlimit(-1.0f,1.0f,v)*plot.getHeight()*0.5f; };
            g.drawLine(x,y(frame.maximum),x,y(frame.minimum),1.0f);
        }
    }
private:
    WaveformHistory& history;
    juce::String name;
    std::array<WaveformHistory::Frame, WaveformHistory::capacity> frames {};
    std::size_t count=0, write=0;
    std::uint64_t currentEpoch=0, lastSequence=0;
};

namespace
{
class NativePresetDialogs final : public PresetDialogs
{
public:
    void showMenu(juce::PopupMenu menu, juce::Component& target, std::function<void(int)> done) override
    {
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target), std::move(done));
    }
    void chooseFile(bool save, const juce::File& suggested, std::function<void(juce::File)> done) override
    {
        chooser = std::make_unique<juce::FileChooser>(save ? "Save preset as" : "Load preset", suggested, "*.disdorktionpreset");
        const auto flags = juce::FileBrowserComponent::canSelectFiles
            | (save ? juce::FileBrowserComponent::saveMode
                    : juce::FileBrowserComponent::openMode);
        chooser->launchAsync(flags, [done = std::move(done)](const juce::FileChooser& browser) { done(browser.getResult()); });
    }
    void confirmOverwrite(const juce::File& file, std::function<void(bool)> done) override
    {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::WarningIcon).withTitle("Replace preset?")
            .withMessage("Replace " + file.getFileName() + "?").withButton("Replace").withButton("Cancel"),
            [done = std::move(done)](int result) { done(result == 1); });
    }
    void showError(const juce::String& message) override
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Preset", message);
    }
private:
    std::unique_ptr<juce::FileChooser> chooser;
};
constexpr int savePresetAction = 1, saveAsAction = 2, loadAction = 3, deleteAction = 4, resetAction = 5;
constexpr int factoryAction = 100, userAction = 1000;
}

PluginEditor::PluginEditor(PluginProcessor& owner, std::shared_ptr<PresetDialogs> dialogs)
    : AudioProcessorEditor(owner), processor(owner), presetDialogs(dialogs ? std::move(dialogs) : std::make_shared<NativePresetDialogs>())
{
    lookAndFeel.setColour(juce::ResizableWindow::backgroundColourId, background);
    lookAndFeel.setColour(juce::Slider::thumbColourId, accent);
    lookAndFeel.setColour(juce::Slider::trackColourId, accent);
    lookAndFeel.setColour(juce::ToggleButton::textColourId, text);
    lookAndFeel.setColour(juce::ToggleButton::tickColourId, accent);
    lookAndFeel.setColour(juce::ComboBox::backgroundColourId, background);
    lookAndFeel.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff314357));
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(content);
    title.setComponentID("branding");
    title.setText("DisDorktion", juce::dontSendNotification);
    title.setFont(juce::Font(24.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, text);
    title.setJustificationType(juce::Justification::centred);
    content.addAndMakeVisible(title);
    presets.setComponentID("presetMenu");
    presets.setTitle("Presets");
    presets.onClick = [this] { showPresetMenu(); };
    clearOverloads.setComponentID("clearOverloads");
    clearOverloads.onClick = [this] { processor.clearMeterOverloads(); };
    content.addAndMakeVisible(presets);
    content.addAndMakeVisible(clearOverloads);

    filterGroup.setComponentID("filterGroup");
    filterGroup.setInterceptsMouseClicks(false, false);
    content.addAndMakeVisible(filterGroup);
    const std::array<std::vector<const char*>,4> layout {{
        {"captureRateHz","crush","bitDepth","shape","jitter","crusherMix"},
        {"tanhDriveDb","folderDriveDb","folderThreshold","driver","waveshaperTrimDb","waveshaperMix"},
        {"combDelayMs","dispersionFrequencyHz","combDampingHz","combFeedforward","dispersionMix","toneDb","combFeedback","hpCutoffHz","hpResponse","combMix","lpCutoffHz","lpResponse"},
        {"inputGainDb","globalMix","outputGainDb"}
    }};
    for (std::size_t group=0;group<layout.size();++group)
        for (const auto* id : layout[group])
            for (const auto& control : fullChainControls)
                if (juce::String(id)==control.id)
                {
                    const auto caption=juce::String(control.name).upToFirstOccurrenceOf(" (",false,false);
                    addControl(static_cast<int>(group),id,caption.toRawUTF8());
                }
    inputWaveform=std::make_unique<WaveformDisplay>(processor.getInputWaveformHistory(),"Raw input");
    outputWaveform=std::make_unique<WaveformDisplay>(processor.getOutputWaveformHistory(),"Final output");
    inputWaveform->setComponentID("inputWaveform"); outputWaveform->setComponentID("outputWaveform");
    content.addAndMakeVisible(*inputWaveform); content.addAndMakeVisible(*outputWaveform);
    inputMeter = std::make_unique<PeakMeter>("Raw input");
    outputMeter = std::make_unique<PeakMeter>("Final output");
    inputMeter->setComponentID("inputMeter");
    outputMeter->setComponentID("outputMeter");
    content.addAndMakeVisible(*inputMeter);
    content.addAndMakeVisible(*outputMeter);
    setResizable(true, true);
    setResizeLimits(672, 444, 1400, 925);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(editorWidth) / editorHeight);
    setSize(editorWidth, editorHeight);
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
    content.addAndMakeVisible(*row);
    controls[static_cast<std::size_t>(section)].push_back(std::move(row));
}

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    g.addTransform(content.getTransform());
    const char* names[] = {"RECEIVER", "WAVESHAPER", "REFINER", "GLOBAL"};
    for (std::size_t i=0;i<sections.size();++i)
    {
        g.setColour(panel); g.fillRoundedRectangle(sections[i].toFloat(),9.0f);
        g.setColour(accent); g.setFont(juce::Font(14.0f,juce::Font::bold));
        g.drawText(names[i],sections[i].reduced(12,6).removeFromTop(24),juce::Justification::centredLeft);
    }
    g.setColour(juce::Colour(0xff263647));
    g.fillRoundedRectangle(filterGroup.getBounds().toFloat(),6.0f);
}

void PluginEditor::resized()
{
    // Host DPI scaling stays on AudioProcessorEditor. User resizing transforms
    // this fixed canvas, including fonts, text boxes, painting and hit testing.
    content.setBounds(0, 0, editorWidth, editorHeight);
    content.setTransform(juce::AffineTransform::scale(juce::jmin(
        static_cast<float>(getWidth()) / editorWidth, static_cast<float>(getHeight()) / editorHeight)));
    auto area=content.getLocalBounds().reduced(12);
    const int gap=10, width=(area.getWidth()-2*gap)/3;
    const int top=110, bodyY=area.getY()+top+gap;
    const int bodyHeight=area.getBottom()-bodyY;
    std::array<juce::Rectangle<int>,3> columns;
    for(int i=0;i<3;++i) columns[i]={area.getX()+i*(width+gap),bodyY,width,bodyHeight};
    inputWaveform->setBounds(columns[0].withY(area.getY()).withHeight(top));
    outputWaveform->setBounds(columns[2].withY(area.getY()).withHeight(top));
    auto branding=columns[1].withY(area.getY()).withHeight(top).reduced(8,0);
    branding=branding.withSizeKeepingCentre(branding.getWidth(),70);
    title.setBounds(branding.removeFromTop(30));
    branding.removeFromTop(8); presets.setBounds(branding.removeFromTop(32));
    const int rowHeight=(bodyHeight-38)/4;
    sections[0]=columns[0].withHeight(32+rowHeight*2);
    sections[1]=columns[1].withHeight(32+rowHeight*2);
    sections[2]=columns[2];
    sections[3]=columns[0].withY(sections[0].getBottom()+gap).withHeight(bodyHeight-sections[0].getHeight()-gap);
    const auto grid=[&](int group,juce::Rectangle<int> bounds,int rows)
    {
        bounds=bounds.reduced(7,0); bounds.removeFromTop(32);
        const int cellWidth=bounds.getWidth()/3;
        const int cellHeight=juce::jmin(rowHeight,bounds.getHeight()/rows);
        for(std::size_t i=0;i<controls[group].size();++i)
            controls[group][i]->setBounds(bounds.getX()+static_cast<int>(i%3)*cellWidth+1,bounds.getY()+static_cast<int>(i/3)*cellHeight+1,cellWidth-2,cellHeight-2);
    };
    grid(0,sections[0],2); grid(1,sections[1],2); grid(2,sections[2],4); grid(3,sections[3],1);
    filterGroup.setBounds(controls[2][7]->getBounds().getUnion(controls[2][11]->getBounds()).expanded(1));
    auto meters=columns[1].withY(sections[1].getBottom()+gap).withHeight(bodyHeight-sections[1].getHeight()-gap).reduced(14,8);
    inputMeter->setBounds(meters.removeFromTop(44)); meters.removeFromTop(8);
    outputMeter->setBounds(meters.removeFromTop(44)); meters.removeFromTop(10);
    clearOverloads.setBounds(meters.removeFromTop(28));
}
void PluginEditor::refreshPresetAndMode()
{
    const auto info = processor.getPresetInfo();
    presets.setButtonText(info.name + (info.modified ? " *" : ""));
}

void PluginEditor::reportPresetResult(const juce::Result& result)
{
    if (result.failed()) presetDialogs->showError(result.getErrorMessage());
    refreshPresetAndMode();
}

void PluginEditor::choosePresetFile(bool save, uint64_t generation)
{
    presetDialogPending = true;
    const auto info = processor.getPresetInfo();
    const auto suggested = save ? processor.defaultUserPresetDirectory().getChildFile(info.name + ".disdorktionpreset")
                                : processor.defaultUserPresetDirectory();
    const juce::Component::SafePointer<PluginEditor> safe(this);
    presetDialogs->chooseFile(save, suggested, [safe, save, generation](juce::File file)
    {
        if (!safe) return;
        if (file == juce::File() || safe->processor.getPresetInfo().generation != generation)
        {
            safe->presetDialogPending = false;
            return;
        }
        if (save && file.getFileExtension().isEmpty()) file = file.withFileExtension(".disdorktionpreset");
        auto complete = [safe, save, file, generation](bool accepted)
        {
            if (!safe) return;
            safe->presetDialogPending = false;
            if (!accepted || safe->processor.getPresetInfo().generation != generation) return;
            safe->reportPresetResult(save ? safe->processor.savePresetAs(file, generation)
                                          : safe->processor.loadUserPreset(file, generation));
        };
        // Confirm the final path, including any appended extension. The fallback
        // chooser's overwrite check applies to its raw selection only.
        if (save && file.exists()) safe->presetDialogs->confirmOverwrite(file, std::move(complete));
        else complete(true);
    });
}

void PluginEditor::showPresetMenu()
{
    if (presetDialogPending) return;
    const auto generation = processor.beginPresetOperation();
    const auto info = processor.getPresetInfo();
    const auto files = processor.listUserPresets();
    juce::PopupMenu menu, factory, users;
    menu.addItem(savePresetAction, "Save preset");
    menu.addItem(saveAsAction, "Save preset as");
    menu.addItem(loadAction, "Load preset");
    menu.addItem(deleteAction, "Delete preset", info.type == PresetType::user);
    menu.addItem(resetAction, "Reset to defaults");
    menu.addSeparator();
    for (std::size_t i = 0; i < factoryPresets.size(); ++i)
        factory.addItem(factoryAction + static_cast<int>(i), factoryPresets[i].name, true,
                        info.type == PresetType::factory && info.factoryIndex == static_cast<int>(i));
    for (int i = 0; i < files.size(); ++i)
        users.addItem(userAction + i, files[i].getFileNameWithoutExtension(), true,
                      info.type == PresetType::user && info.file == files[i]);
    if (files.isEmpty()) users.addItem(-1, "No user presets", false);
    menu.addSubMenu("Factory presets", factory);
    menu.addSubMenu("User presets", users);
    const juce::Component::SafePointer<PluginEditor> safe(this);
    presetDialogPending = true;
    presetDialogs->showMenu(std::move(menu), presets, [safe, generation, files](int action)
    {
        if (!safe) return;
        safe->presetDialogPending = false;
        auto& owner = *safe;
        if (action == 0 || owner.processor.getPresetInfo().generation != generation) return;
        if (action >= userAction && action < userAction + files.size())
            owner.reportPresetResult(owner.processor.loadUserPreset(files[action - userAction], generation));
        else if (action >= factoryAction && action < factoryAction + static_cast<int>(factoryPresets.size()))
        {
            if (!owner.processor.loadFactoryPreset(action - factoryAction, generation)) return;
            owner.refreshPresetAndMode();
        }
        else if (action == saveAsAction || action == loadAction) owner.choosePresetFile(action == saveAsAction, generation);
        else if (action == savePresetAction)
        {
            const auto current = owner.processor.getPresetInfo();
            const auto collision = current.type == PresetType::factory
                && owner.processor.defaultUserPresetDirectory().getChildFile(current.name + ".disdorktionpreset").exists();
            if (current.type == PresetType::detached || collision) owner.choosePresetFile(true, generation);
            else owner.reportPresetResult(owner.processor.savePreset(owner.processor.defaultUserPresetDirectory(), generation));
        }
        else if (action == deleteAction) owner.reportPresetResult(owner.processor.deleteUserPreset(generation));
        else if (action == resetAction) owner.reportPresetResult(owner.processor.resetToDefaults(generation));
    });
}

void PluginEditor::timerCallback()
{
    const auto meters = processor.readMeters();
    inputMeter->update(meters.inputPeak, meters.inputOverload);
    outputMeter->update(meters.outputPeak, meters.outputOverload);
    inputWaveform->poll();
    outputWaveform->poll();
    refreshPresetAndMode();
}
}
