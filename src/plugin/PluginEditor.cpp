#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "FactoryPresets.h"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace disdorktion
{
namespace
{
const juce::Colour background{0xff161719};
constexpr int editorWidth = FaceplateLayout::width, editorHeight = FaceplateLayout::height;
constexpr int minimumWidth = 624, minimumHeight = 569;
constexpr int maximumWidth = 1300, maximumHeight = 1185;
}

// Components precede attachments so listeners are detached before their controls die.
class PluginEditor::ControlRow final : public juce::Component
{
public:
    ControlRow(juce::AudioProcessorValueTreeState& state, const FaceplateLayout::Control& geometry,
               const juce::String& caption)
        : slider(geometry)
    {
        const auto* id = geometry.parameter;
        auto* parameter = state.getParameter(id);
        jassert(parameter != nullptr);
        const juce::String parameterId(id);
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
        setBounds(slider.getBounds());
    }

    void resized() override
    {
        slider.setBounds(getLocalBounds());
    }

private:
    faceplate::RotarySlider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
};

class PluginEditor::PeakMeter final : public juce::Component
{
    class OverloadButton final : public juce::Button
    {
    public:
        OverloadButton() : juce::Button("Clear overload") {}

        void paintButton(juce::Graphics& g, bool hovered, bool pressed) override
        {
            if (!isEnabled() || !(hovered || pressed || hasKeyboardFocus(false)))
                return;
            g.setColour(faceplate::overloadColour.withAlpha(hasKeyboardFocus(false) ? 1.0f : 0.65f));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(2.0f), 4.0f, 1.2f);
        }
    };

public:
    PeakMeter(juce::String caption, const FaceplateLayout::Meter& geometry, std::function<void()> clear)
    {
        setTitle(caption + " peak meter");
        setBounds(geometry.track.bounds.getUnion(geometry.value.bounds).getSmallestIntegerContainer());
        track = geometry.track.bounds - getPosition().toFloat();
        value = geometry.value.bounds - getPosition().toFloat();
        trackClip = geometry.track.clipAt(getPosition());
        valueClip = geometry.value.clipAt(getPosition());
        const auto action = "Clear " + caption.toLowerCase() + " overload";
        readout.setTitle(action);
        readout.setTooltip(action);
        readout.setBounds(value.getSmallestIntegerContainer());
        readout.onClick = std::move(clear);
        addAndMakeVisible(readout);
        update(0, false);
    }

    void update(float nextPeak, bool nextOverload)
    {
        peak = juce::jmax(nextPeak, peak * 0.89f);
        if (readout.isEnabled() != nextOverload)
        {
            readout.setEnabled(nextOverload);
            readout.setMouseCursor(nextOverload ? juce::MouseCursor::PointingHandCursor
                                                : juce::MouseCursor::NormalCursor);
        }
        overload = nextOverload;
        const auto db = juce::Decibels::gainToDecibels(peak, -60.0f);
        level = (db <= -60.0f ? juce::String("-inf") : juce::String(db, 1)) + " dBFS";
        setDescription(level + (overload ? ", overload" : ""));
        readout.setDescription(getDescription());
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto db = juce::Decibels::gainToDecibels(peak, -60.0f);
        const auto activity = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        const auto colour = overload ? faceplate::overloadColour : faceplate::phosphor;
        {
            const juce::Graphics::ScopedSaveState saved(g);
            if (g.reduceClipRegion(trackClip))
            {
                const auto interior = track.reduced(3, 4);
                const auto bar = interior.withWidth(interior.getWidth() * activity);
                g.setColour(colour);
                g.fillRoundedRectangle(bar, juce::jmin(3.0f, bar.getWidth() / 2, bar.getHeight() / 2));
            }
        }
        if (g.reduceClipRegion(valueClip))
        {
            faceplate::drawReadout(g, overload ? value.withTrimmedLeft(15) : value, level, colour);
            if (overload)
            {
                g.setColour(colour);
                g.setFont(12.0f);
                g.drawText("!", value.withWidth(14), juce::Justification::centred, false);
            }
        }
    }

private:
    OverloadButton readout;
    juce::String level;
    juce::Rectangle<float> track, value;
    juce::Path trackClip, valueClip;
    float peak = 0;
    bool overload = false;
};

// Only the message thread owns the displayed history. Each poll is bounded.
class PluginEditor::WaveformDisplay final : public juce::Component
{
public:
    WaveformDisplay(WaveformHistory& source, juce::String caption, const FaceplateLayout::Window& window)
        : history(source), name(std::move(caption)), currentEpoch(source.epoch())
    {
        history.discardPending();
        setBounds(window.bounds.getSmallestIntegerContainer());
        glass = window.bounds - getPosition().toFloat();
        clip = window.clipAt(getPosition());
        setTitle(name + " waveform, fixed -1 to +1 amplitude");
        setDescription(name + " waveform: 0 frames");
    }

    void poll()
    {
        const auto epoch = history.epoch();
        if (epoch != currentEpoch) { count = 0; currentEpoch = epoch; }
        std::array<WaveformHistory::Frame, WaveformHistory::capacity> incoming;
        const auto received = history.drain(incoming.data(), incoming.size());
        const auto now = juce::Time::getMillisecondCounter();
        if (now - lastReceived >= 600)
            count = 0;
        if (received != 0)
            lastReceived = now;
        for (std::size_t i = 0; i < received; ++i)
        {
            const auto& frame = incoming[i];
            if (frame.epoch != currentEpoch) { count = 0; currentEpoch = frame.epoch; }
            if (count != 0 && frame.sequence != lastSequence + 1) count = 0;
            frames[write] = frame;
            write = (write + 1) % frames.size();
            count = juce::jmin(count + 1, frames.size());
            lastSequence = frame.sequence;
        }
        if (history.epoch() != currentEpoch) { count = 0; currentEpoch = history.epoch(); }
        setDescription(name + " waveform: " + juce::String(static_cast<int>(count)) + " frames");
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        if (!g.reduceClipRegion(clip))
            return;
        const auto inset = glass.getWidth() * 0.038f;
        const auto left = glass.getX() + inset;
        const auto right = glass.getRight() - inset;
        const auto center = glass.getCentreY();
        const auto y = [&](float sample) {
            return center - 0.298f * glass.getHeight() * juce::jlimit(-1.0f, 1.0f, sample);
        };
        juce::Path trace, peaks;
        trace.startNewSubPath(left, center);
        if (count == 0)
            trace.lineTo(right, center);
        else
        {
            const auto spacing = (right - left) / static_cast<float>(frames.size() - 1);
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto& frame = frames[(write + frames.size() - count + i) % frames.size()];
                const auto x = right - static_cast<float>(count - 1 - i) * spacing;
                const auto middle = y((frame.minimum + frame.maximum) * 0.5f);
                if (i == 0 && middle != center)
                    trace.startNewSubPath(x - juce::jmin(1.0f, spacing), middle);
                trace.lineTo(x, middle);
                if (frame.minimum != frame.maximum)
                {
                    peaks.startNewSubPath(x, y(frame.maximum));
                    peaks.lineTo(x, y(frame.minimum));
                }
            }
            trace.addPath(peaks);
        }
        g.setColour(faceplate::scopeHalo);
        g.strokePath(trace, juce::PathStrokeType(4.0f));
        g.setColour(faceplate::phosphor);
        g.strokePath(trace, juce::PathStrokeType(2.0f));
    }

private:
    WaveformHistory& history;
    juce::String name;
    juce::Rectangle<float> glass;
    juce::Path clip;
    juce::uint32 lastReceived = juce::Time::getMillisecondCounter();
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
    : AudioProcessorEditor(owner), processor(owner), artwork(FaceplateAssets::embedded()),
      lookAndFeel(artwork), presetDialogs(dialogs ? std::move(dialogs) : std::make_shared<NativePresetDialogs>())
{
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(content);
    content.setTitle("DisDorktion");
    content.setComponentID("faceplate");
    setResizable(true, true);
    setResizeLimits(minimumWidth, minimumHeight, maximumWidth, maximumHeight);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(editorWidth) / editorHeight);

    auto scale = 1.0;
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto usable = display->userBounds;
        scale = juce::jlimit(0.6, 1.0, juce::jmin((usable.getWidth() - 24) / static_cast<double>(editorWidth),
                                                  (usable.getHeight() - 64) / static_cast<double>(editorHeight)));
    }
    setSize(juce::roundToInt(editorWidth * scale), juce::roundToInt(editorHeight * scale));
    if (!artwork.layout)
        return;

    const auto& layout = *artwork.layout;
    for (const auto& geometry : layout.controls)
    {
        const auto entry = std::find_if(fullChainControls.begin(), fullChainControls.end(),
            [&](const auto& parameter) { return std::string_view(parameter.id) == geometry.parameter; });
        jassert(entry != fullChainControls.end());
        addControl(geometry, juce::String(entry->name).upToFirstOccurrenceOf(" (", false, false));
    }

    presets.setComponentID("presetMenu");
    presets.setTitle("Presets");
    presets.window = layout.preset;
    presets.setBounds(layout.preset.bounds.getSmallestIntegerContainer());
    presets.onClick = [this] { showPresetMenu(); };
    content.addAndMakeVisible(presets);

    inputWaveform = std::make_unique<WaveformDisplay>(processor.getInputWaveformHistory(), "Raw input", layout.inputScope);
    outputWaveform = std::make_unique<WaveformDisplay>(processor.getOutputWaveformHistory(), "Final output", layout.outputScope);
    inputWaveform->setComponentID("inputWaveform");
    outputWaveform->setComponentID("outputWaveform");
    content.addAndMakeVisible(*inputWaveform);
    content.addAndMakeVisible(*outputWaveform);

    inputMeter = std::make_unique<PeakMeter>("Raw input", layout.inputMeter, [this] {
        processor.clearMeterOverloads(PluginProcessor::MeterOverload::input);
    });
    outputMeter = std::make_unique<PeakMeter>("Final output", layout.outputMeter, [this] {
        processor.clearMeterOverloads(PluginProcessor::MeterOverload::output);
    });
    inputMeter->setComponentID("inputMeter");
    outputMeter->setComponentID("outputMeter");
    content.addAndMakeVisible(*inputMeter);
    content.addAndMakeVisible(*outputMeter);

    refreshPresetAndMode();
    startTimerHz(30);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void PluginEditor::addControl(const FaceplateLayout::Control& geometry, const juce::String& caption)
{
    auto row = std::make_unique<ControlRow>(processor.parameterState(), geometry, caption);
    content.addAndMakeVisible(*row);
    controls.push_back(std::move(row));
}

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);
    if (!artwork.layout)
    {
        g.setColour(juce::Colours::white);
        g.setFont(18.0f);
        g.drawText("DisDorktion faceplate could not be loaded.", getLocalBounds(), juce::Justification::centred);
        return;
    }

    g.addTransform(content.getTransform());
    g.drawImage(artwork.plate, content.getLocalBounds().toFloat());

    const auto& die = artwork.layout->die;
    if (!g.reduceClipRegion(die.clipAt({})))
        return;
    g.setColour(faceplate::phosphor);
    const auto size = juce::jmin(die.bounds.getWidth(), die.bounds.getHeight());
    const auto radius = size * 0.063f;
    const auto offset = size * 0.22f;
    const auto center = die.bounds.getCentre();
    for (const auto point : {juce::Point<float>{-offset, -offset}, {offset, -offset}, {},
                             {-offset, offset}, {offset, offset}})
        g.fillEllipse(juce::Rectangle<float>(radius * 2, radius * 2).withCentre(center + point));
}

void PluginEditor::resized()
{
    content.setBounds(0, 0, editorWidth, editorHeight);
    const auto scale = juce::jmin(static_cast<float>(getWidth()) / editorWidth,
                                  static_cast<float>(getHeight()) / editorHeight);
    content.setTransform(juce::AffineTransform::scale(scale).translated(
        (getWidth() - editorWidth * scale) * 0.5f, (getHeight() - editorHeight * scale) * 0.5f));
    repaint();
}

void PluginEditor::refreshPresetAndMode()
{
    const auto info = processor.getPresetInfo();
    presets.setDisplay(info.name, info.modified);
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
