#include "MainComponent.h"
#include "CaptureRateMapping.h"
#include <charconv>
#include <cmath>
#include <algorithm>

namespace disdorktion::audition
{
using namespace harness;
namespace
{
bool number(const juce::TextEditor& editor, double& result)
{
    const auto text = editor.getText().trim().toStdString();
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(result);
}
juce::NormalisableRange<double> captureRange(double host)
{
    return {100.0, 384000.0,
        [host](double, double, double position) { return captureRateFromPosition(position, host); },
        [host](double, double, double value) { return captureRateToPosition(value, host); }};
}
}

MainComponent::MainComponent()
{
    setSize(1080, 890);
    setWantsKeyboardFocus(true);
    title.setText("DisDorktion - module audition", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions(24.0f)));
    gainLabel.setText("Module gain (linear amplitude)", juce::dontSendNotification);
    monitorLabel.setText("Monitoring volume (dB)", juce::dontSendNotification);
    sourceLabel.setText("Source", juce::dontSendNotification);
    fieldsLabel.setText("Frequency Hz      Second/end Hz     Peak amplitude     Phase radians       Source frames       Render frames       Noise seed", juce::dontSendNotification);
    observationsLabel.setText("Experiment observations", juce::dontSendNotification);
    target.addItem("Reference gain", 1);
    target.addItem("Gain (dB)", 2);
    target.addItem("Bit crusher", 3);
    target.addItem("Tanh saturation", 4);
    target.addItem("Wave folder", 5);
    target.addItem("High-pass", 6);
    target.addItem("Low-pass", 7);
    target.addItem("Input section", 8);
    target.addItem("Middle section (candidate)", 9);
    target.addItem("Output section", 10);
    target.addItem("Full chain (candidate)", 11);
    fullViewport.setViewedComponent(&fullPanel,false);
    addChildComponent(fullViewport);
    for(std::size_t i=0;i<fullChainControls.size();++i)
    {
        const auto& c=fullChainControls[i];
        auto publish=[this,i](float value)
        {
            if(record.version!=11) return;
            if(disdorktion::setFullChainControl(record.fullChain,fullChainControls[i].id,value))
                audio.engine().setFullChainControl(fullChainControls[i].id,value);
        };
        auto& label=fullLabels[i];label.setText(c.name,juce::dontSendNotification);fullPanel.addAndMakeVisible(label);
        if(c.discrete && c.maximum==1.f)
        {
            auto& toggle=fullToggles[i];fullPanel.addAndMakeVisible(toggle);
            toggle.onClick=[this,i,publish] {publish(fullToggles[i].getToggleState()?1.f:0.f);};
        }
        else if(c.discrete)
        {
            auto& choice=fullChoices[i];fullPanel.addAndMakeVisible(choice);
            if(std::string_view(c.id)=="bitDepth")
                for(auto depth:bitCrusherDepths) choice.addItem(juce::String(depth),static_cast<int>(depth)+1);
            else
            {
                choice.addItem("Butterworth",1);choice.addItem("Chebyshev I (1 dB)",2);choice.addItem("Chebyshev II (40 dB)",3);
            }
            choice.onChange=[this,i,publish] {publish(static_cast<float>(fullChoices[i].getSelectedId()-1));};
        }
        else
        {
            auto& slider=fullSliders[i];fullPanel.addAndMakeVisible(slider);
            slider.setSliderStyle(juce::Slider::LinearHorizontal);slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,100,26);
            slider.setRange(c.minimum,c.maximum,0);slider.setValue(c.defaultValue,juce::dontSendNotification);
            if(c.maximum>=20000.f) slider.setSkewFactorFromMidPoint(std::string_view(c.id)=="captureRateHz"?48000.:1000.);
            slider.onValueChange=[this,i,publish] {publish(static_cast<float>(fullSliders[i].getValue()));};
        }
    }
    fullSeedLabel.setText("Jitter seed (uint32)",juce::dontSendNotification);
    fullPanel.addAndMakeVisible(fullSeedLabel);fullPanel.addAndMakeVisible(fullSeed);
    fullSeed.setInputRestrictions(10,"0123456789");fullSeed.setText("1",false);
    fullSeed.onTextChange=[this] {
        if(record.version!=11) return;
        std::uint32_t seedValue=0;
        if(readCrusherSeed(seedValue)){record.fullChain.input.crusher.jitterSeed=seedValue;audio.engine().setFullChainJitterSeed(seedValue);}
    };
    target.setSelectedId(1);
    for (const auto* name : {"Impulse", "Sine", "Two-tone", "Logarithmic sweep", "Seeded white noise", "Audio file loop", "Keyboard / MIDI sine"})
        source.addItem(name, source.getNumItems() + 1);
    source.setSelectedId(2);
    frequency.setText("440"); frequency2.setText("880"); amplitude.setText("0.25");
    phase.setText("0"); duration.setText("48000"); renderDuration.setText("48000"); seed.setText("1");
    gain.setRange(0, 4, 0); gain.setValue(1);
    bitDepthLabel.setText("Bit depth", juce::dontSendNotification);
    captureRateLabel.setText("Capture rate (Hz)", juce::dontSendNotification);
    for (const auto depth : bitCrusherDepths) bitDepth.addItem(juce::String(depth), static_cast<int>(depth));
    bitDepth.setSelectedId(64, juce::dontSendNotification);
    captureRateHz.setNormalisableRange(captureRange(48000.0));
    mappedCaptureHost = 48000.0;
    captureRateHz.setNumDecimalPlacesToDisplay(6);
    captureRateHz.setValue(384000.0, juce::dontSendNotification);
    fullRateLabel.setText("Full rate 48000 Hz", juce::dontSendNotification);
    crusherMixLabel.setText("Mix", juce::dontSendNotification);
    crusherCrushLabel.setText("Crush", juce::dontSendNotification);
    crusherShapeLabel.setText("Shape", juce::dontSendNotification);
    crusherJitterLabel.setText("Jitter", juce::dontSendNotification);
    jitterSeedLabel.setText("Jitter seed", juce::dontSendNotification);
    jitterSeed.setText("1", false);
    jitterSeed.setInputRestrictions(10, "0123456789");
    crusherMix.setRange(0.0, 1.0, 0.0); crusherMix.setValue(1.0, juce::dontSendNotification);
    crusherCrush.setRange(0.0, 1.0, 0.0); crusherCrush.setValue(0.0, juce::dontSendNotification);
    crusherShape.setRange(-1.0, 1.0, 0.0); crusherShape.setValue(0.0, juce::dontSendNotification);
    crusherJitter.setRange(0.0, 1.0, 0.0); crusherJitter.setValue(0.0, juce::dontSendNotification);
    for (juce::Component* control : std::initializer_list<juce::Component*>{&bitDepth, &captureRateHz,
        &bitDepthLabel, &captureRateLabel, &effectiveRateLabel, &fullRateLabel,
        &crusherMix, &crusherCrush, &crusherShape, &crusherJitter, &jitterSeed,
        &crusherMixLabel, &crusherCrushLabel, &crusherShapeLabel, &crusherJitterLabel, &jitterSeedLabel})
        control->setEnabled(false);
    monitor.setRange(-60, 0, 0.1); monitor.setValue(-18);
    tanhDriveLabel.setText("Drive (dB)", juce::dontSendNotification);
    tanhOutputLabel.setText("Output trim (dB)", juce::dontSendNotification);
    tanhDrive.setRange(-24, 36, 0); tanhDrive.setValue(0);
    tanhOutput.setRange(-60, 12, 0); tanhOutput.setValue(0);
    folderThresholdLabel.setText("Fold threshold", juce::dontSendNotification);
    folderThreshold.setRange(0.05, 1.0, 0); folderThreshold.setValue(1.0);
    folderThreshold.setNumDecimalPlacesToDisplay(4);
    middleFolderDriveLabel.setText("Folder drive (dB)", juce::dontSendNotification);
    middleFolderOutputLabel.setText("Folder trim (dB)", juce::dontSendNotification);
    middleFolderDrive.setRange(-24, 36, 0); middleFolderDrive.setValue(0);
    middleFolderOutput.setRange(-60, 12, 0); middleFolderOutput.setValue(0);
    filterCutoffLabel.setText("Cutoff (Hz)",juce::dontSendNotification);
    filterResponseLabel.setText("Response",juce::dontSendNotification);
    filterCutoff.setRange(20,20000,0); filterCutoff.setSkewFactorFromMidPoint(1000);
    filterCutoff.setSliderStyle(juce::Slider::LinearHorizontal);
    filterCutoff.setTextBoxStyle(juce::Slider::TextBoxRight,false,85,26);
    filterResponse.addItem("Butterworth",1);
    filterResponse.addItem("Chebyshev I (1 dB ripple)",2);
    filterResponse.addItem("Chebyshev II (40 dB stopband)",3);
    filterResponse.setTooltip("Cutoff is the half-power (-3.0103 dB) point in every response mode.");
    outputLowCutoffLabel.setText("Low-pass cutoff (Hz)",juce::dontSendNotification);
    outputLowResponseLabel.setText("Low-pass response",juce::dontSendNotification);
    outputLowCutoff.setRange(20,20000,0); outputLowCutoff.setSkewFactorFromMidPoint(1000);
    outputLowCutoff.setSliderStyle(juce::Slider::LinearHorizontal);
    outputLowCutoff.setTextBoxStyle(juce::Slider::TextBoxRight,false,85,26);
    outputLowResponse.addItem("Butterworth",1);
    outputLowResponse.addItem("Chebyshev I (1 dB ripple)",2);
    outputLowResponse.addItem("Chebyshev II (40 dB stopband)",3);
    for (juce::Component* control : std::initializer_list<juce::Component*>{&outputLowCutoff,&outputLowResponse,
        &outputLowCutoffLabel,&outputLowResponseLabel,&outputHighBypass,&outputLowBypass}) addChildComponent(*control);
    for (juce::Component* control : std::initializer_list<juce::Component*>{&tanhDrive, &tanhOutput,
        &tanhDriveLabel, &tanhOutputLabel, &folderThreshold, &folderThresholdLabel})
        control->setVisible(false);
    for (auto* slider : {&gain, &monitor, &captureRateHz, &crusherMix, &crusherCrush, &crusherShape, &crusherJitter, &tanhDrive, &tanhOutput, &folderThreshold, &middleFolderDrive, &middleFolderOutput})
    {
        slider->setSliderStyle(juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 85, 26);
    }
    muted.setToggleState(true, juce::dontSendNotification);
    for (juce::Component* component : std::initializer_list<juce::Component*>{
        &title,&gainLabel,&monitorLabel,&sourceLabel,&fieldsLabel,&observationsLabel,
        &target,&source,&gain,&monitor,&bitDepth,&captureRateHz,&bitDepthLabel,&captureRateLabel,&effectiveRateLabel,&fullRateLabel,
        &crusherMix,&crusherCrush,&crusherShape,&crusherJitter,&jitterSeed,
        &crusherMixLabel,&crusherCrushLabel,&crusherShapeLabel,&crusherJitterLabel,&jitterSeedLabel,
        &bypass,&moduleMuted,&muted,&gainBypass,&crusherBypass,&apply,&load,&play,&restart,
        &save,&restore,&render,&deviceButton,&frequency,&frequency2,&amplitude,&phase,&duration,&seed,
        &renderDuration,&observations,&meterLabel,&deviceLabel,&statusLabel,&keyboard}) addAndMakeVisible(*component);
    for (juce::Component* control : std::initializer_list<juce::Component*>{&tanhDrive, &tanhOutput,
        &tanhDriveLabel, &tanhOutputLabel, &folderThreshold, &folderThresholdLabel})
        addChildComponent(*control);
    for(juce::Component* control:std::initializer_list<juce::Component*>{&filterCutoff,&filterResponse,&filterCutoffLabel,&filterResponseLabel,&filterInfo})
        addChildComponent(*control);
    observations.setMultiLine(true);
    for (juce::Component* control : std::initializer_list<juce::Component*>{&middleFolderDrive,
        &middleFolderOutput, &middleFolderDriveLabel, &middleFolderOutputLabel, &middleTanhBypass, &middleFolderBypass})
        addChildComponent(*control);
    observations.setReturnKeyStartsNewLine(true);
    observations.setTextToShowWhenEmpty("Source, exact settings, listening observations, and decisions...", juce::Colours::grey);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffe1bb76));
    keyboard.setAvailableRange(36, 96);
    keyboardState.addListener(this);
    apply.onClick = [this] { applySource(); };
    source.onChange = [this] { applySource(); };
    moduleMuted.setEnabled(false);
    gainBypass.setVisible(false); crusherBypass.setVisible(false);
    target.onChange = [this] {
        auto next = snapshot();
        const auto selected = target.getSelectedId();
        next.version = selected == 11 ? 11 : selected == 10 ? 10 : selected == 9 ? 9 : selected == 8 ? 8 : selected >= 6 ? 7 : selected == 5 ? 6 : selected == 4 ? 5 : selected == 3 ? 4 : selected;
        next.targetId = selected == 1 ? "reference-gain" : selected == 2 ? "gain" : selected == 3 ? "bit-crusher"
            : selected == 4 ? "tanh-saturation" : selected == 5 ? "wave-folder" : selected == 6 ? "high-pass" : selected == 7 ? "low-pass" : selected == 8 ? "input-section" : selected == 9 ? "middle-section" : selected==10 ? "output-section" : "full-chain";
        next.gain = 1.0f; next.gainDb = 0.0f; next.mute = false; next.bypass = false;
        next.gainBypass = false; next.crusherBypass = false;
        next.bitDepth = 64; next.captureRateHz = 384000.0f;
        next.mix = 1.0f; next.crush = 0.0f; next.shape = 0.0f;
        next.jitter = 0.0f; next.jitterSeed = 1;
        next.driveDb = 0.0f; next.outputDb = 0.0f; next.threshold = 1.0f;
        next.cutoffHz = selected == 6 ? 20.f : selected == 7 ? 20000.f : 1000.f;
        next.response = FilterResponse::butterworth;
        next.middle = {};
        next.output = {};
        next.fullChain = {};
        next.middleQuality = "4x-fir-adaa-v1";
        configureRecord(next);
    };
    gain.onValueChange = [this] {
        if (target.getSelectedId() == 2 || target.getSelectedId() == 8) audio.engine().setGainDb(static_cast<float>(gain.getValue()));
        else if (target.getSelectedId() == 1) audio.engine().setGain(static_cast<float>(gain.getValue()));
    };
    bitDepth.onChange = [this] { audio.engine().setBitDepth(static_cast<unsigned>(bitDepth.getSelectedId())); };
    captureRateHz.onValueChange = [this] {
        if (!updatingCaptureMapping) audio.engine().setCaptureRateHz(static_cast<float>(captureRateHz.getValue()));
    };
    crusherMix.onValueChange = [this] { audio.engine().setCrusherMix(static_cast<float>(crusherMix.getValue())); };
    crusherCrush.onValueChange = [this] { audio.engine().setCrush(static_cast<float>(crusherCrush.getValue())); };
    crusherShape.onValueChange = [this] { audio.engine().setQuantizerShape(static_cast<float>(crusherShape.getValue())); };
    crusherJitter.onValueChange = [this] { audio.engine().setJitter(static_cast<float>(crusherJitter.getValue())); };
    tanhDrive.onValueChange = [this] {
        if (target.getSelectedId() == 9) audio.engine().setMiddleTanhDriveDb(static_cast<float>(tanhDrive.getValue()));
        else if (target.getSelectedId() == 5) audio.engine().setWaveDriveDb(static_cast<float>(tanhDrive.getValue()));
        else if (target.getSelectedId() == 4) audio.engine().setTanhDriveDb(static_cast<float>(tanhDrive.getValue()));
    };
    tanhOutput.onValueChange = [this] {
        if (target.getSelectedId() == 9) audio.engine().setMiddleTanhOutputDb(static_cast<float>(tanhOutput.getValue()));
        else if (target.getSelectedId() == 5) audio.engine().setWaveOutputDb(static_cast<float>(tanhOutput.getValue()));
        else if (target.getSelectedId() == 4) audio.engine().setTanhOutputDb(static_cast<float>(tanhOutput.getValue()));
    };
    folderThreshold.onValueChange = [this] {
        if (target.getSelectedId() == 9) audio.engine().setMiddleThreshold(static_cast<float>(folderThreshold.getValue()));
        else if (target.getSelectedId() == 5) audio.engine().setWaveThreshold(static_cast<float>(folderThreshold.getValue()));
    };
    middleFolderDrive.onValueChange = [this] { audio.engine().setMiddleFolderDriveDb(static_cast<float>(middleFolderDrive.getValue())); };
    middleFolderOutput.onValueChange = [this] { audio.engine().setMiddleFolderOutputDb(static_cast<float>(middleFolderOutput.getValue())); };
    middleTanhBypass.onClick = [this] { audio.engine().setMiddleTanhBypass(middleTanhBypass.getToggleState()); };
    middleFolderBypass.onClick = [this] { audio.engine().setMiddleFolderBypass(middleFolderBypass.getToggleState()); };
    filterCutoff.onValueChange = [this] {
        if (target.getSelectedId()==10) audio.engine().setOutputCutoffHz(true,static_cast<float>(filterCutoff.getValue()));
        else audio.engine().setFilterCutoffHz(static_cast<float>(filterCutoff.getValue()));
    };
    filterResponse.onChange = [this] {
        if(filterResponse.getSelectedId()>0)
        {
            const auto value=static_cast<FilterResponse>(filterResponse.getSelectedId()-1);
            if (target.getSelectedId()==10) audio.engine().setOutputResponse(true,value);
            else audio.engine().setFilterResponse(value);
        }
    };
    outputLowCutoff.onValueChange=[this] { audio.engine().setOutputCutoffHz(false,static_cast<float>(outputLowCutoff.getValue())); };
    outputLowResponse.onChange=[this] {
        if(outputLowResponse.getSelectedId()>0) audio.engine().setOutputResponse(false,static_cast<FilterResponse>(outputLowResponse.getSelectedId()-1));
    };
    outputHighBypass.onClick=[this] { audio.engine().setOutputStageBypass(true,outputHighBypass.getToggleState()); };
    outputLowBypass.onClick=[this] { audio.engine().setOutputStageBypass(false,outputLowBypass.getToggleState()); };
    jitterSeed.onTextChange = [this] {
        std::uint32_t value = 0;
        if (readCrusherSeed(value))
        {
            record.jitterSeed = value;
            audio.engine().setJitterSeed(value);
        }
    };
    moduleMuted.onClick = [this] { audio.engine().setModuleMuted(moduleMuted.getToggleState()); };
    bypass.onClick = [this] { audio.engine().setBypass(bypass.getToggleState()); };
    gainBypass.onClick = [this] { audio.engine().setInputGainBypass(gainBypass.getToggleState()); };
    crusherBypass.onClick = [this] { audio.engine().setInputCrusherBypass(crusherBypass.getToggleState()); };
    monitor.onValueChange = [this] { audio.engine().setMonitorDb(static_cast<float>(monitor.getValue())); };
    muted.onClick = [this] { audio.engine().setMuted(muted.getToggleState()); };
    play.onClick = [this] { running = !audio.isPlaying(); audio.setPlaying(running); play.setButtonText(running ? "Stop" : "Play"); };
    restart.onClick = [this] { audio.restart(); };
    load.onClick = [this] {
        chooseFile(false, "*.wav;*.aif;*.aiff", [this](const auto& file) {
            std::uint32_t parsedSeed = 0;
            if ((record.version == 3 || record.version == 4 || record.version == 8 || record.version == 11) && !readCrusherSeed(parsedSeed))
            { status("Jitter seed must be an unsigned 32-bit integer."); return; }
            auto next = snapshot(); next.source.kind = SourceKind::file; next.filePath = file.getFullPathName();
            next.fileHash.clear(); loadAudio(file, next, false);
        });
    };
    save.onClick = [this] { saveExperiment(); };
    restore.onClick = [this] { loadExperiment(); };
    render.onClick = [this] { renderExperiment(); };
    deviceButton.onClick = [this] { showDevices(); };
    audio.configure(record);
    const auto error = devices.initialise(0, 2, nullptr, true);
    if (error.isNotEmpty()) status(error);
    devices.addAudioCallback(&audio);
    devices.addMidiInputDeviceCallback({}, &audio);
    startTimerHz(30);
    if (error.isEmpty()) status("Ready. Monitoring starts muted; select a source, press Play, and unmute to listen.");
}

MainComponent::~MainComponent()
{
    stopTimer();
    chooser.reset();
    if (deviceDialog != nullptr) delete deviceDialog.getComponent();
    keyboardState.removeListener(this);
    devices.removeMidiInputDeviceCallback({}, &audio);
    devices.removeAudioCallback(&audio);
    cancel.store(true);
    if (worker.joinable()) worker.join();
}

void MainComponent::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff20252c)); }
void MainComponent::resized()
{
    auto bounds = getLocalBounds().reduced(22);
    title.setBounds(bounds.removeFromTop(44));
    auto row = bounds.removeFromTop(38); target.setBounds(row.removeFromLeft(230));
    sourceLabel.setBounds(row.removeFromLeft(65)); source.setBounds(row.removeFromLeft(240));
    deviceButton.setBounds(row.reduced(8, 0));
    deviceLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(6);
    fieldsLabel.setBounds(bounds.removeFromTop(26));
    row = bounds.removeFromTop(32);
    const auto width = row.getWidth() / 7;
    for (auto* editor : {&frequency,&frequency2,&amplitude,&phase,&duration,&renderDuration,&seed}) editor->setBounds(row.removeFromLeft(width).reduced(3, 0));
    row = bounds.removeFromTop(45).reduced(0, 5);
    apply.setBounds(row.removeFromLeft(155).reduced(3)); load.setBounds(row.removeFromLeft(180).reduced(3));
    play.setBounds(row.removeFromLeft(130).reduced(3)); restart.setBounds(row.removeFromLeft(130).reduced(3));
    gainLabel.setBounds(bounds.removeFromTop(24));
    row = bounds.removeFromTop(38); bypass.setBounds(row.removeFromRight(120));
    if (record.version == 8)
    {
        crusherBypass.setBounds(row.removeFromRight(140));
        gainBypass.setBounds(row.removeFromRight(130));
    }
    if (record.version == 9)
    {
        middleFolderBypass.setBounds(row.removeFromRight(140));
        middleTanhBypass.setBounds(row.removeFromRight(130));
    }
    if (record.version == 10)
    {
        outputLowBypass.setBounds(row.removeFromRight(150));
        outputHighBypass.setBounds(row.removeFromRight(150));
    }
    moduleMuted.setBounds(row.removeFromRight(140)); gain.setBounds(row);
    row = bounds.removeFromTop(46);
    bitDepthLabel.setBounds(row.removeFromLeft(75)); bitDepth.setBounds(row.removeFromLeft(90).reduced(2));
    captureRateLabel.setBounds(row.removeFromLeft(130)); captureRateHz.setBounds(row.removeFromLeft(335).reduced(2));
    fullRateLabel.setBounds(row.removeFromLeft(150)); effectiveRateLabel.setBounds(row);
    tanhDriveLabel.setBounds(bitDepthLabel.getBounds().withWidth(130));
    tanhDrive.setBounds(bitDepth.getBounds().getUnion(captureRateHz.getBounds()).withTrimmedLeft(60));
    if (record.version == 9)
    {
        auto drives = bitDepthLabel.getBounds().getUnion(effectiveRateLabel.getBounds());
        auto left = drives.removeFromLeft(drives.getWidth()/2);
        tanhDriveLabel.setBounds(left.removeFromLeft(130)); tanhDrive.setBounds(left.reduced(2));
        middleFolderDriveLabel.setBounds(drives.removeFromLeft(130)); middleFolderDrive.setBounds(drives.reduced(2));
    }
    row = bounds.removeFromTop(42);
    auto half = row.removeFromLeft(row.getWidth() / 2);
    crusherMixLabel.setBounds(half.removeFromLeft(75)); crusherMix.setBounds(half.reduced(2));
    crusherCrushLabel.setBounds(row.removeFromLeft(75)); crusherCrush.setBounds(row.reduced(2));
    tanhOutputLabel.setBounds(crusherMixLabel.getBounds().withWidth(130));
    tanhOutput.setBounds(crusherMix.getBounds().getUnion(crusherCrush.getBounds()).withTrimmedLeft(60));
    if (record.version == 9)
    {
        auto trims = crusherMixLabel.getBounds().getUnion(crusherCrush.getBounds());
        auto left = trims.removeFromLeft(trims.getWidth()/2);
        tanhOutputLabel.setBounds(left.removeFromLeft(130)); tanhOutput.setBounds(left.reduced(2));
        middleFolderOutputLabel.setBounds(trims.removeFromLeft(130)); middleFolderOutput.setBounds(trims.reduced(2));
    }
    row = bounds.removeFromTop(42);
    half = row.removeFromLeft(row.getWidth() / 2);
    crusherShapeLabel.setBounds(half.removeFromLeft(75)); crusherShape.setBounds(half.reduced(2));
    crusherJitterLabel.setBounds(row.removeFromLeft(75)); crusherJitter.setBounds(row.reduced(2));
    folderThresholdLabel.setBounds(crusherShapeLabel.getBounds().withWidth(130));
    folderThreshold.setBounds(crusherShape.getBounds().getUnion(crusherJitter.getBounds()).withTrimmedLeft(60));
    filterCutoffLabel.setBounds(tanhDriveLabel.getBounds());
    filterCutoff.setBounds(tanhDrive.getBounds());
    filterResponseLabel.setBounds(tanhOutputLabel.getBounds());
    filterResponse.setBounds(tanhOutput.getBounds().withWidth(310));
    filterInfo.setBounds(crusherShapeLabel.getBounds().getUnion(crusherJitter.getBounds()));
    row = bounds.removeFromTop(38);
    jitterSeedLabel.setBounds(row.removeFromLeft(90)); jitterSeed.setBounds(row.removeFromLeft(190).reduced(2));
    if (record.version == 10)
    {
        filterInfo.setBounds(gain.getBounds());
        auto highRow=bitDepthLabel.getBounds().getUnion(effectiveRateLabel.getBounds());
        filterCutoffLabel.setBounds(highRow.removeFromLeft(170)); filterCutoff.setBounds(highRow.reduced(2));
        auto modeRow=crusherMixLabel.getBounds().getUnion(crusherCrush.getBounds());
        filterResponseLabel.setBounds(modeRow.removeFromLeft(170)); filterResponse.setBounds(modeRow.removeFromLeft(310).reduced(2));
        auto lowRow=crusherShapeLabel.getBounds().getUnion(crusherJitter.getBounds());
        outputLowCutoffLabel.setBounds(lowRow.removeFromLeft(170)); outputLowCutoff.setBounds(lowRow.reduced(2));
        auto lowModeRow=jitterSeedLabel.getBounds().getUnion(row);
        outputLowResponseLabel.setBounds(lowModeRow.removeFromLeft(170)); outputLowResponse.setBounds(lowModeRow.removeFromLeft(310).reduced(2));
    }
    monitorLabel.setBounds(bounds.removeFromTop(24));
    fullViewport.setBounds(gain.getBounds().getUnion(jitterSeed.getBounds()).withWidth(getWidth()-44));
    fullPanel.setSize(std::max(300,fullViewport.getWidth()-18),static_cast<int>(fullChainControls.size()+1)*34);
    for(std::size_t i=0;i<fullChainControls.size();++i)
    {
        auto controlRow=juce::Rectangle<int>(0,static_cast<int>(i)*34,fullPanel.getWidth(),32);
        fullLabels[i].setBounds(controlRow.removeFromLeft(220));
        fullSliders[i].setBounds(controlRow);fullChoices[i].setBounds(controlRow);fullToggles[i].setBounds(controlRow);
    }
    auto seedRow=juce::Rectangle<int>(0,static_cast<int>(fullChainControls.size())*34,fullPanel.getWidth(),32);
    fullSeedLabel.setBounds(seedRow.removeFromLeft(220));fullSeed.setBounds(seedRow);
    row = bounds.removeFromTop(38); muted.setBounds(row.removeFromRight(180)); monitor.setBounds(row);
    meterLabel.setBounds(bounds.removeFromTop(40));
    keyboard.setBounds(bounds.removeFromTop(72)); bounds.removeFromTop(10);
    observationsLabel.setBounds(bounds.removeFromTop(24));
    statusLabel.setBounds(bounds.removeFromBottom(45));
    row = bounds.removeFromBottom(44);
    save.setBounds(row.removeFromLeft(180).reduced(3)); restore.setBounds(row.removeFromLeft(180).reduced(3));
    render.setBounds(row.removeFromLeft(180).reduced(3));
    observations.setBounds(bounds.reduced(0, 5));
}

void MainComponent::status(const juce::String& text) { statusLabel.setText(text, juce::dontSendNotification); }
void MainComponent::focusLost(FocusChangeType) { audio.allNotesOff(); keyboardState.reset(); }
void MainComponent::focusOfChildComponentChanged(FocusChangeType)
{
    if (!hasKeyboardFocus(true)) { audio.allNotesOff(); keyboardState.reset(); }
}
void MainComponent::handleNoteOn(juce::MidiKeyboardState*, int, int note, float velocity) { audio.queueNote(true, note, velocity); }
void MainComponent::handleNoteOff(juce::MidiKeyboardState*, int, int note, float) { audio.queueNote(false, note); }

ExperimentRecord MainComponent::snapshot() const
{
    auto result = record;
    if (result.version == 10)
        result.output={{static_cast<float>(filterCutoff.getValue()),outputHighBypass.getToggleState(),static_cast<FilterResponse>(filterResponse.getSelectedId()-1)},
            {static_cast<float>(outputLowCutoff.getValue()),outputLowBypass.getToggleState(),static_cast<FilterResponse>(outputLowResponse.getSelectedId()-1)}};
    if (result.version == 2 || result.version == 8) { result.gainDb = static_cast<float>(gain.getValue()); result.mute = moduleMuted.getToggleState(); }
    else if (result.version == 1) result.gain = static_cast<float>(gain.getValue());
    else if (result.version == 7)
    {
        result.cutoffHz=static_cast<float>(filterCutoff.getValue());
        result.response=static_cast<FilterResponse>(filterResponse.getSelectedId()-1);
    }
    else if (result.version == 5 || result.version == 6)
    {
        result.driveDb = static_cast<float>(tanhDrive.getValue());
        result.outputDb = static_cast<float>(tanhOutput.getValue());
        result.threshold = result.version == 6 ? static_cast<float>(folderThreshold.getValue()) : 1.0f;
    }
    if (result.version == 3 || result.version == 4 || result.version == 8)
    {
        if (result.version == 3) result.version = 4; // Upgrade legacy crusher records on save.
        result.bitDepth = static_cast<unsigned>(bitDepth.getSelectedId());
        result.captureRateHz = static_cast<float>(captureRateHz.getValue());
        result.mix = static_cast<float>(crusherMix.getValue());
        result.crush = static_cast<float>(crusherCrush.getValue());
        result.shape = static_cast<float>(crusherShape.getValue());
        result.jitter = static_cast<float>(crusherJitter.getValue());
        std::uint32_t parsedSeed = 0;
        if (readCrusherSeed(parsedSeed)) result.jitterSeed = parsedSeed;
    }
    if (result.version == 9)
    {
        result.middle = {{static_cast<float>(tanhDrive.getValue()), static_cast<float>(tanhOutput.getValue()), middleTanhBypass.getToggleState()},
            {static_cast<float>(middleFolderDrive.getValue()), static_cast<float>(folderThreshold.getValue()),
             static_cast<float>(middleFolderOutput.getValue()), middleFolderBypass.getToggleState()}};
    }
    if (result.version == 8)
    {
        result.gainBypass = gainBypass.getToggleState();
        result.crusherBypass = crusherBypass.getToggleState();
    }
    result.bypass = result.version==11 ? false : bypass.getToggleState();
    result.monitorDb = static_cast<float>(monitor.getValue()); result.muted = muted.getToggleState();
    result.observations = observations.getText();
    if (audio.sampleRate() > 0) result.sampleRate = audio.sampleRate();
    if (audio.channelCount() > 0) result.channels = audio.channelCount();
    if (audio.blockSize() > 0) result.blockSize = audio.blockSize();
    result.midiDevice.clear();
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        if (devices.isMidiInputDeviceEnabled(device.identifier)) result.midiDevice += device.name + "; ";
    return result;
}

bool MainComponent::readCrusherSeed(std::uint32_t& result) const
{
    double value = 0;
    if (!number(record.version==11 ? fullSeed : jitterSeed, value) || value < 0 || value > 4294967295.0 || std::floor(value) != value)
        return false;
    result = static_cast<std::uint32_t>(value);
    return true;
}

void MainComponent::updateCaptureRateMapping()
{
    const auto host = captureRateMappingHost(audio.sampleRate());
    if (host == mappedCaptureHost) return;
    const auto requested = captureRateHz.getValue();
    updatingCaptureMapping = true;
    captureRateHz.setNormalisableRange(captureRange(host));
    captureRateHz.setValue(requested, juce::dontSendNotification);
    updatingCaptureMapping = false;
    mappedCaptureHost = host;
}

bool MainComponent::readSourceControls(ExperimentRecord& next)
{
    std::uint32_t parsedSeed = 0;
    if ((next.version == 3 || next.version == 4 || next.version == 8 || next.version == 11) && !readCrusherSeed(parsedSeed))
    { status("Jitter seed must be an unsigned 32-bit integer."); return false; }
    double sourceFrames = 0, renderFrames = 0, seedValue = 0;
    if (!number(frequency, next.source.frequency) || !number(frequency2, next.source.frequency2)
        || !number(amplitude, next.source.amplitude) || !number(phase, next.source.phase)
        || !number(duration, sourceFrames) || !number(renderDuration, renderFrames) || !number(seed, seedValue)
        || sourceFrames < 1 || sourceFrames > ExperimentRecord::maximumSamples || std::floor(sourceFrames) != sourceFrames
        || renderFrames < 1 || renderFrames > ExperimentRecord::maximumSamples || std::floor(renderFrames) != renderFrames
        || seedValue < 0 || seedValue > 4294967295.0 || std::floor(seedValue) != seedValue)
    { status("Enter finite numbers; source/render frames are positive integers up to 536870000, and seed is an unsigned integer."); return false; }
    next.source.durationSamples = static_cast<std::uint64_t>(sourceFrames);
    next.renderDurationSamples = static_cast<std::uint64_t>(renderFrames);
    next.source.seed = static_cast<std::uint32_t>(seedValue);
    next.source.kind = static_cast<SourceKind>(source.getSelectedId() - 1);
    juce::String error;
    if (!next.validate(error)) { status(error); return false; }
    return true;
}

void MainComponent::configureRecord(const ExperimentRecord& next)
{
    devices.removeAudioCallback(&audio);
    record = next;
    running = false; play.setButtonText("Play"); keyboardState.reset();
    source.setSelectedId(static_cast<int>(record.source.kind) + 1, juce::dontSendNotification);
    const auto section = record.version == 8;
    const auto production = record.version == 2 || section;
    const auto crusher = record.version == 3 || record.version == 4 || section;
    const auto saturation = record.version == 5;
    const auto folder = record.version == 6;
    const auto outputSection = record.version == 10;
    const auto filtering = record.version == 7 || outputSection;
    const auto middle = record.version == 9;
    const auto driven = saturation || folder || middle;
    target.setSelectedId(record.version==11 ? 11 : outputSection ? 10 : middle ? 9 : section ? 8 : filtering ? (record.targetId=="high-pass" ? 6 : 7) : folder ? 5 : saturation ? 4 : crusher ? 3 : record.version, juce::dontSendNotification);
    gain.setRange(production ? -60.0 : 0.0, production ? 24.0 : 4.0, 0.0);
    gainLabel.setText(outputSection ? "Output section: high-pass then low-pass | independent responses and bypasses"
        : middle ? "Middle candidate: tanh then ADAA folder | 4x FIR | 92 samples | sound acceptance pending"
        : filtering ? "Output filter: cutoff and response; 10 ms transitions"
        : folder ? "Wave folder: drive, threshold and wet output trim"
        : saturation ? "Tanh saturation: drive and wet output trim"
        : section ? "Input section: gain (dB) then bit crusher"
        : production ? "Module gain (dB)" : "Module gain (linear amplitude)", juce::dontSendNotification);
    gain.setValue(production ? record.gainDb : record.gain, juce::dontSendNotification);
    gain.setEnabled((!crusher || section) && !driven && !filtering);
    gainLabel.setEnabled(!crusher || section);
    gain.setVisible(!driven && !filtering);
    moduleMuted.setVisible(!driven && !filtering);
    filterCutoffLabel.setText(outputSection ? "High-pass cutoff (Hz)" : "Cutoff (Hz)",juce::dontSendNotification);
    filterResponseLabel.setText(outputSection ? "High-pass response" : "Response",juce::dontSendNotification);
    filterCutoff.setValue(outputSection ? record.output.highPass.cutoffHz : record.cutoffHz,juce::dontSendNotification);
    filterResponse.setSelectedId(static_cast<int>(outputSection ? record.output.highPass.response : record.response)+1,juce::dontSendNotification);
    outputLowCutoff.setValue(record.output.lowPass.cutoffHz,juce::dontSendNotification);
    outputLowResponse.setSelectedId(static_cast<int>(record.output.lowPass.response)+1,juce::dontSendNotification);
    outputHighBypass.setToggleState(record.output.highPass.bypass,juce::dontSendNotification);
    outputLowBypass.setToggleState(record.output.lowPass.bypass,juce::dontSendNotification);
    for (juce::Component* control : std::initializer_list<juce::Component*>{&outputLowCutoff,&outputLowResponse,
        &outputLowCutoffLabel,&outputLowResponseLabel,&outputHighBypass,&outputLowBypass})
    {control->setVisible(outputSection);control->setEnabled(outputSection);}
    for(juce::Component* control:std::initializer_list<juce::Component*>{&filterCutoff,&filterResponse,&filterCutoffLabel,&filterResponseLabel,&filterInfo})
    {control->setVisible(filtering);control->setEnabled(filtering);}
    tanhDriveLabel.setText(middle ? "Tanh drive (dB)" : "Drive (dB)", juce::dontSendNotification);
    tanhOutputLabel.setText(middle ? "Tanh trim (dB)" : "Output trim (dB)", juce::dontSendNotification);
    tanhDrive.setValue(middle ? record.middle.tanh.driveDb : record.driveDb, juce::dontSendNotification);
    tanhOutput.setValue(middle ? record.middle.tanh.outputDb : record.outputDb, juce::dontSendNotification);
    folderThreshold.setValue(middle ? record.middle.folder.threshold : record.threshold, juce::dontSendNotification);
    middleFolderDrive.setValue(record.middle.folder.driveDb, juce::dontSendNotification);
    middleFolderOutput.setValue(record.middle.folder.outputDb, juce::dontSendNotification);
    middleTanhBypass.setToggleState(record.middle.tanh.bypass, juce::dontSendNotification);
    middleFolderBypass.setToggleState(record.middle.folder.bypass, juce::dontSendNotification);
    for (juce::Component* control : std::initializer_list<juce::Component*>{&middleFolderDrive,
        &middleFolderOutput, &middleFolderDriveLabel, &middleFolderOutputLabel, &middleTanhBypass, &middleFolderBypass})
    { control->setVisible(middle); control->setEnabled(middle); }
    for (juce::Component* control : std::initializer_list<juce::Component*>{&tanhDrive, &tanhOutput, &tanhDriveLabel, &tanhOutputLabel})
    {
        control->setEnabled(driven);
        control->setVisible(driven);
    }
    folderThreshold.setEnabled(folder || middle);
    folderThreshold.setVisible(folder || middle);
    folderThresholdLabel.setEnabled(folder || middle);
    folderThresholdLabel.setVisible(folder || middle);
    bitDepth.setSelectedId(static_cast<int>(record.bitDepth), juce::dontSendNotification);
    updateCaptureRateMapping();
    captureRateHz.setValue(record.captureRateHz, juce::dontSendNotification);
    crusherMix.setValue(record.mix, juce::dontSendNotification);
    crusherCrush.setValue(record.crush, juce::dontSendNotification);
    crusherShape.setValue(record.shape, juce::dontSendNotification);
    crusherJitter.setValue(record.jitter, juce::dontSendNotification);
    jitterSeed.setText(juce::String(static_cast<juce::int64>(record.jitterSeed)), false);
    for (juce::Component* control : std::initializer_list<juce::Component*>{&bitDepth, &captureRateHz,
        &bitDepthLabel, &captureRateLabel, &effectiveRateLabel, &fullRateLabel,
        &crusherMix, &crusherCrush, &crusherShape, &crusherJitter, &jitterSeed,
        &crusherMixLabel, &crusherCrushLabel, &crusherShapeLabel, &crusherJitterLabel, &jitterSeedLabel})
    {
        control->setEnabled(crusher);
        control->setVisible(!driven && !filtering);
    }
    moduleMuted.setEnabled(production); moduleMuted.setToggleState(record.mute, juce::dontSendNotification);
    gainBypass.setVisible(section); crusherBypass.setVisible(section);
    gainBypass.setEnabled(section); crusherBypass.setEnabled(section);
    gainBypass.setToggleState(record.gainBypass, juce::dontSendNotification);
    crusherBypass.setToggleState(record.crusherBypass, juce::dontSendNotification);
    bypass.setToggleState(record.bypass, juce::dontSendNotification);
    monitor.setValue(record.monitorDb, juce::dontSendNotification); muted.setToggleState(record.muted, juce::dontSendNotification);
    frequency.setText(juce::String(record.source.frequency), false);
    frequency2.setText(juce::String(record.source.frequency2), false);
    amplitude.setText(juce::String(record.source.amplitude), false); phase.setText(juce::String(record.source.phase), false);
    duration.setText(juce::String(static_cast<juce::int64>(record.source.durationSamples)), false);
    renderDuration.setText(juce::String(static_cast<juce::int64>(record.renderDurationSamples)), false);
    seed.setText(juce::String(static_cast<juce::int64>(record.source.seed)), false);
    observations.setText(record.observations, false);
    fullViewport.setVisible(record.version==11);
    bypass.setVisible(record.version!=11);
    if(record.version==11)
    {
        for(juce::Component* control:std::initializer_list<juce::Component*>{&gain,&moduleMuted,&bypass,&bitDepth,&captureRateHz,
            &bitDepthLabel,&captureRateLabel,&effectiveRateLabel,&fullRateLabel,&crusherMix,&crusherCrush,&crusherShape,&crusherJitter,&jitterSeed,
            &crusherMixLabel,&crusherCrushLabel,&crusherShapeLabel,&crusherJitterLabel,&jitterSeedLabel}) control->setVisible(false);
        gainLabel.setText("Full chain | 4x FIR ADAA | aligned pre-input dry | final gain | scroll for all controls",juce::dontSendNotification);
        gainLabel.setEnabled(true);
        for(std::size_t i=0;i<fullChainControls.size();++i)
        {
            const auto value=getFullChainControl(record.fullChain,fullChainControls[i].id);
            fullSliders[i].setValue(value,juce::dontSendNotification);
            fullChoices[i].setSelectedId(static_cast<int>(value)+1,juce::dontSendNotification);
            fullToggles[i].setToggleState(value!=0.f,juce::dontSendNotification);
        }
        fullSeed.setText(juce::String(static_cast<juce::int64>(record.fullChain.input.crusher.jitterSeed)),false);
    }
    // Range changes may update the slider value. Publish the restored controls
    // after refreshing the widgets, while processing is still detached.
    audio.configure(record);
    devices.addAudioCallback(&audio);
    resized();
}

void MainComponent::applySource()
{
    if (!jobDone.load() || worker.joinable()) { status("Wait for the worker result before changing sources."); return; }
    auto next = snapshot();
    if (!readSourceControls(next)) return;
    if (next.source.kind == SourceKind::file)
    {
        if (next.filePath.isEmpty()) { status("Load a WAV/AIFF file first."); return; }
        loadAudio(juce::File(next.filePath), next, true);
    }
    else { configureRecord(next); status("Source applied; press Play. Generated sources end after their recorded duration."); }
}

void MainComponent::chooseFile(bool saving, const juce::String& pattern, std::function<void(const juce::File&)> callback)
{
    if (chooser != nullptr) { status("A file dialog is already open."); return; }
    chooser = std::make_unique<juce::FileChooser>(saving ? "Save experiment" : "Choose file", juce::File{}, pattern);
    const juce::Component::SafePointer<MainComponent> safe(this);
    chooser->launchAsync((saving ? juce::FileBrowserComponent::saveMode : juce::FileBrowserComponent::openMode)
        | juce::FileBrowserComponent::canSelectFiles | (saving ? juce::FileBrowserComponent::warnAboutOverwriting : 0),
        [safe, callback = std::move(callback)](const juce::FileChooser& dialog) {
            const auto selected = dialog.getResult();
            if (safe != nullptr && selected != juce::File{}) callback(selected);
            if (safe != nullptr) safe->chooser.reset();
        });
}

void MainComponent::beginJob(std::function<void()> operation)
{
    if (!jobDone.load()) { status("A worker is busy; wait for the current load/render to finish."); return; }
    if (worker.joinable()) worker.join();
    cancel.store(false);
    jobDone.store(false);
    setWorkerControlsEnabled(false);
    status("Working... audio loading, hashing, and rendering run in the background.");
    worker = std::thread([this, operation = std::move(operation)] {
        try { operation(); }
        catch (const std::exception& error) { std::lock_guard lock(jobMutex); jobMessage = "Worker failed: " + juce::String(error.what()); loopSucceeded = false; }
        catch (...) { std::lock_guard lock(jobMutex); jobMessage = "Worker failed."; loopSucceeded = false; }
        jobDone.store(true);
    });
}

void MainComponent::setWorkerControlsEnabled(bool enabled)
{
    fullViewport.setEnabled(enabled);
    for (juce::Component* component : std::initializer_list<juce::Component*>{
        &target,&source,&apply,&load,&save,&restore,&render,&frequency,&frequency2,
        &amplitude,&phase,&duration,&renderDuration,&seed,&gain,&bitDepth,&captureRateHz,
        &crusherMix,&crusherCrush,&crusherShape,&crusherJitter,&jitterSeed,&tanhDrive,&tanhOutput,&folderThreshold,
        &tanhDriveLabel,&tanhOutputLabel,&folderThresholdLabel,
        &bypass,&moduleMuted,&gainBypass,&crusherBypass,&observations})
        component->setEnabled(enabled);
    gain.setEnabled(enabled && (record.version < 3 || record.version == 8));
    bitDepth.setEnabled(enabled && (record.version == 3 || record.version == 4 || record.version == 8));
    captureRateHz.setEnabled(enabled && (record.version == 3 || record.version == 4 || record.version == 8));
    for (juce::Component* control : std::initializer_list<juce::Component*>{
        &crusherMix,&crusherCrush,&crusherShape,&crusherJitter,&jitterSeed})
        control->setEnabled(enabled && (record.version == 3 || record.version == 4 || record.version == 8));
    moduleMuted.setEnabled(enabled && (record.version == 2 || record.version == 8));
    gainBypass.setEnabled(enabled && record.version == 8);
    crusherBypass.setEnabled(enabled && record.version == 8);
    for (juce::Component* control : std::initializer_list<juce::Component*>{
        &tanhDrive,&tanhOutput,&tanhDriveLabel,&tanhOutputLabel})
        control->setEnabled(enabled && (record.version == 5 || record.version == 6 || record.version == 9));
    folderThreshold.setEnabled(enabled && (record.version == 6 || record.version == 9));
    folderThresholdLabel.setEnabled(enabled && (record.version == 6 || record.version == 9));
    for (juce::Component* control : std::initializer_list<juce::Component*>{&middleFolderDrive,
        &middleFolderOutput, &middleFolderDriveLabel, &middleFolderOutputLabel, &middleTanhBypass, &middleFolderBypass})
        control->setEnabled(enabled && record.version == 9);
    filterCutoff.setEnabled(enabled && (record.version == 7 || record.version == 10));
    filterResponse.setEnabled(enabled && (record.version == 7 || record.version == 10));
    for (juce::Component* control : std::initializer_list<juce::Component*>{&outputLowCutoff,&outputLowResponse,
        &outputLowCutoffLabel,&outputLowResponseLabel,&outputHighBypass,&outputLowBypass})
        control->setEnabled(enabled && record.version == 10);
    // Monitoring remains available during worker activity. Its current settings
    // are retained when a newly loaded source replaces the old one.
}

void MainComponent::loadAudio(const juce::File& file, ExperimentRecord next, bool verifyHash, bool restoring)
{
    if (!jobDone.load()) { status("Wait for the current worker before loading another source."); return; }
    const auto rate = audio.sampleRate(); const auto channels = audio.channelCount();
    if (rate <= 0 || (channels != 1 && channels != 2)) { status("Choose a working mono/stereo audio device first."); return; }
    lastConversionAttemptRate = rate;
    lastConversionAttemptChannels = channels;
    loopJob = true; loopSucceeded = false;
    restoringLoopRecord = restoring;
    next.sampleRate = rate; next.channels = channels; next.source.kind = SourceKind::file;
    const auto* previousLoop = audio.loadedLoop();
    const auto residentBytes = previousLoop != nullptr ? previousLoop->residentMemoryBytes() : 0;
    beginJob([this, file, next, verifyHash, rate, channels, residentBytes]() mutable {
        auto decoded = std::make_unique<LoopSource>(); juce::String error;
        auto ok = decoded->load(file, rate, channels, error, &cancel, residentBytes);
        if (ok && verifyHash && next.fileHash.isNotEmpty() && next.fileHash != decoded->fileHash())
        { ok = false; error = "Source content hash changed; load the file explicitly to start a new experiment."; }
        if (ok)
        {
            next.filePath = file.getFullPathName(); next.fileHash = decoded->fileHash();
            next.originalSampleRate = decoded->originalSampleRate(); next.originalChannels = decoded->originalChannels();
        }
        std::lock_guard lock(jobMutex);
        loopSucceeded = ok; jobMessage = ok ? "File loaded. Press Play; loops wrap exactly without crossfades." : error;
        completedRecord = next; completionRate = rate; completionChannels = channels;
        completedLoop = ok ? std::move(decoded) : nullptr;
    });
}

void MainComponent::saveExperiment()
{
    std::uint32_t parsedSeed = 0;
    if ((record.version == 3 || record.version == 4 || record.version == 8 || record.version == 11) && !readCrusherSeed(parsedSeed))
    { status("Jitter seed must be an unsigned 32-bit integer."); return; }
    auto next = snapshot();
    juce::String error;
    if (!next.validate(error)) { status(error); return; }
    chooseFile(true, "*.json", [this, next](const auto& file) {
        juce::String error;
        status(next.save(file, error) ? "Experiment saved: " + file.getFullPathName() : error);
    });
}

void MainComponent::loadExperiment()
{
    chooseFile(false, "*.json", [this](const auto& file) {
        if (!jobDone.load() || worker.joinable()) { status("Wait for the worker result before restoring settings."); return; }
        ExperimentRecord next; juce::String error;
        if (!ExperimentRecord::load(file, next, error)) { status(error); return; }
        if (next.sampleRate != audio.sampleRate() || next.channels != audio.channelCount() || next.blockSize != audio.blockSize())
        { status("Device rate, channels, or block size differ from the record. Match them in Audio / MIDI devices before loading."); return; }
        if (next.source.kind == SourceKind::file) loadAudio(juce::File(next.filePath), next, true, true);
        else { configureRecord(next); status("Experiment restored stopped; press Play."); }
    });
}

void MainComponent::renderExperiment()
{
    std::uint32_t parsedSeed = 0;
    if ((record.version == 3 || record.version == 4 || record.version == 8 || record.version == 11) && !readCrusherSeed(parsedSeed))
    { status("Jitter seed must be an unsigned 32-bit integer."); return; }
    auto next = snapshot();
    juce::String error;
    if (!next.validate(error)) { status(error); return; }
    if (!jobDone.load()) { status("Wait for the current worker before rendering."); return; }
    if (chooser != nullptr) { status("A file dialog is already open."); return; }
    chooser = std::make_unique<juce::FileChooser>("Choose parent folder for a new render", juce::File{}, "*");
    const juce::Component::SafePointer<MainComponent> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe, next](const juce::FileChooser& dialog) {
            const auto parent = dialog.getResult();
            if (safe != nullptr && parent != juce::File{})
            {
                const auto output = parent.getNonexistentChildFile("disdorktion-render", {}, false);
                safe->loopJob = false;
                auto* owner = safe.getComponent();
                // Destructor cancels and joins this worker before member storage dies.
                owner->beginJob([owner, next, output] {
                    const auto result = harness::renderExperiment(next, output, &owner->cancel);
                    std::lock_guard lock(owner->jobMutex);
                    owner->jobMessage = result.status == RenderStatus::success ? "Render complete: " + output.getFullPathName() : result.message;
                });
            }
            if (safe != nullptr) safe->chooser.reset();
        });
}

void MainComponent::showDevices()
{
    if (deviceDialog != nullptr) { deviceDialog->toFront(true); return; }
    audio.allNotesOff(); keyboardState.reset();
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new juce::AudioDeviceSelectorComponent(devices, 0, 0, 1, 2, true, false, true, false));
    options.content->setSize(600, 520);
    options.dialogTitle = "Audio / MIDI devices"; options.dialogBackgroundColour = juce::Colour(0xff20252c);
    options.escapeKeyTriggersCloseButton = true; options.useNativeTitleBar = true; options.resizable = true;
    deviceDialog = options.launchAsync();
}

void MainComponent::timerCallback()
{
    updateCaptureRateMapping();
    if(record.version==10)
    {
        const auto rate=audio.sampleRate()>0 ? audio.sampleRate() : record.sampleRate;
        filterInfo.setText("Effective HP " + juce::String(std::min(filterCutoff.getValue(),.45*rate),1)
            + " Hz | LP " + juce::String(std::min(outputLowCutoff.getValue(),.45*rate),1)
            + " Hz | 0 samples added",juce::dontSendNotification);
    }
    if(record.version==7)
    {
        const auto rate=audio.sampleRate()>0 ? audio.sampleRate() : record.sampleRate;
        const auto effective=std::min(filterCutoff.getValue(),.45*rate);
        const auto mode=filterResponse.getSelectedId();
        filterInfo.setText("Effective cutoff " + juce::String(effective,1) + " Hz (half power)"
            + (effective<filterCutoff.getValue() ? "  | clamped below Nyquist" : "")
            + (mode==2 ? "  | 1 dB passband ripple" : mode==3 ? "  | 40 dB stopband floor" : "  | 12 dB/octave"),juce::dontSendNotification);
    }
    if (running && !audio.isPlaying())
    {
        running = false;
        play.setButtonText("Play");
        status("Playback finished. Press Play to play the source again.");
    }
    const auto meter = audio.engine().meters();
    const auto overload = audio.engine().consumeOverload();
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (overload) overloadVisibleUntil = now + 2.0;
    const auto appFocused = juce::Process::isForegroundProcess();
    if (applicationHadFocus && !appFocused) { audio.allNotesOff(); keyboardState.reset(); }
    applicationHadFocus = appFocused;
    meterLabel.setText("Input peak " + juce::String(meter.inputPeak, 4) + "   RMS " + juce::String(meter.inputRms, 4)
        + "       Output peak " + juce::String(meter.outputPeak, 4) + "   RMS " + juce::String(meter.outputRms, 4)
        + (record.version == 8 || record.version==11 ? "   Post-gain " + juce::String(meter.postGainPeak, 4) : juce::String())
        + (record.version==11 ? " HP " + juce::String(meter.postHighPassPeak,3) + " Wet " + juce::String(meter.wetPeak,3) : juce::String())
        + (now < overloadVisibleUntil ? "   OVERLOAD" : ""), juce::dontSendNotification);
    deviceLabel.setText(juce::String(audio.sampleRate(), 0) + " Hz   " + juce::String(audio.channelCount())
        + " channels   " + juce::String(audio.blockSize()) + " frames   " + (audio.isReady() ? "Ready" : "Stopped / source unavailable"), juce::dontSendNotification);
    fullRateLabel.setText("Full rate " + juce::String(mappedCaptureHost, 0) + " Hz", juce::dontSendNotification);
    effectiveRateLabel.setText("Effective " + juce::String(std::min(captureRateHz.getValue(), mappedCaptureHost), 6)
        + " Hz", juce::dontSendNotification);
    if (audio.consumeMidiOverflow()) status("MIDI queue overflow: notes cleared. Reduce event traffic.");
    if (jobDone.load() && worker.joinable())
    {
        worker.join();
        std::lock_guard lock(jobMutex);
        if (loopJob && loopSucceeded)
        {
            if (completionRate == audio.sampleRate() && completionChannels == audio.channelCount()
                && (!restoringLoopRecord || completedRecord.blockSize == audio.blockSize()))
            {
                devices.removeAudioCallback(&audio);
                audio.replaceLoop(std::move(completedLoop), completionRate, completionChannels);
                if (!restoringLoopRecord)
                {
                    completedRecord.monitorDb = static_cast<float>(monitor.getValue());
                    completedRecord.muted = muted.getToggleState();
                }
                // configureRecord's removal is idempotent; re-add only once there.
                configureRecord(completedRecord);
            }
            else { completedLoop.reset(); jobMessage = "Device changed during loading; reload source at the new rate."; }
        }
        status(jobMessage); loopJob = false; loopSucceeded = false;
        setWorkerControlsEnabled(true);
    }
    if (audio.fileNeedsConversion() && record.source.kind == SourceKind::file && jobDone.load()
        && !worker.joinable() && record.filePath.isNotEmpty()
        && (lastConversionAttemptRate != audio.sampleRate() || lastConversionAttemptChannels != audio.channelCount()))
        loadAudio(juce::File(record.filePath), snapshot(), true);
}
}
