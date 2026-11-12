#include <catch2/catch_test_macros.hpp>
#include "plugin/PluginProcessor.h"
#include "plugin/FactoryPresets.h"
#include <atomic>
#include <thread>
#include <limits>

namespace {
struct Library {
    juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("disdorktion-presets-test", "", false);
    Library() { REQUIRE(folder.createDirectory().wasOk()); }
    ~Library() { folder.deleteRecursively(); }
};
void edit(disdorktion::PluginProcessor& p, float value) {
    auto* parameter=p.parameterState().getParameter("driver");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
}
TEST_CASE("User presets retain identity across edits and save load delete", "[adapter][preset][files]") {
    Library library; disdorktion::PluginProcessor p;
    REQUIRE(p.getPresetInfo().type==disdorktion::PresetType::factory);
    REQUIRE(p.loadFactoryPreset(2));
    REQUIRE(p.savePreset(library.folder).wasOk());
    const auto saved=p.getPresetInfo();
    REQUIRE(saved.type==disdorktion::PresetType::user);
    REQUIRE(saved.file.existsAsFile()); REQUIRE(saved.name=="Crunch");
    edit(p,2); REQUIRE(p.getPresetInfo().modified); REQUIRE(p.getPresetInfo().file==saved.file);
    REQUIRE(p.savePreset(library.folder).wasOk()); REQUIRE_FALSE(p.getPresetInfo().modified);
    disdorktion::PluginProcessor other;
    REQUIRE(other.loadUserPreset(saved.file).wasOk()); REQUIRE(other.getPresetInfo().name=="Crunch");
    REQUIRE(other.deleteUserPreset().wasOk()); REQUIRE_FALSE(saved.file.exists());
    REQUIRE(other.getPresetInfo().factoryIndex==0);
}
TEST_CASE("Preset file rejection collisions and stale operations preserve sound and identity", "[adapter][preset][files]") {
    Library library; disdorktion::PluginProcessor p;
    REQUIRE(p.savePreset(library.folder).wasOk());
    const auto original=p.getPresetInfo();
    REQUIRE(p.loadFactoryPreset(0));
    REQUIRE(p.savePreset(library.folder).failed());
    const auto token=p.beginPresetOperation(); REQUIRE(p.loadFactoryPreset(1));
    REQUIRE(p.loadUserPreset(original.file,token).failed());
    auto invalid=library.folder.getChildFile("invalid.disdorktionpreset"); REQUIRE(invalid.replaceWithText("<DisDorktionState version=\"5\"/>"));
    REQUIRE(p.loadUserPreset(invalid).failed()); REQUIRE(p.getPresetInfo().factoryIndex==1);
    REQUIRE(p.deleteUserPreset().failed());
    REQUIRE(p.resetToDefaults().wasOk());
    REQUIRE(p.parameterState().getRawParameterValue("tanhDriveDb")->load()==0);
    REQUIRE(p.parameterState().getRawParameterValue("folderDriveDb")->load()==0);
    REQUIRE(p.listUserPresets(library.folder).size()==2);
}
TEST_CASE("Host identity metadata detaches legacy and missing files", "[adapter][preset][state][files]") {
    Library library; disdorktion::PluginProcessor p;
    auto file=library.folder.getChildFile("external.disdorktionpreset"); REQUIRE(p.savePresetAs(file).wasOk());
    edit(p,3); juce::MemoryBlock state; p.getStateInformation(state);
    disdorktion::PluginProcessor restored; restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    REQUIRE(restored.getPresetInfo().type==disdorktion::PresetType::user); REQUIRE(restored.getPresetInfo().modified);
    auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),static_cast<int>(state.getSize()));
    xml->deleteAllChildElements(); juce::AudioProcessor::copyXmlToBinary(*xml,state);
    restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    REQUIRE(restored.getPresetInfo().type==disdorktion::PresetType::detached);
    REQUIRE(restored.savePreset(library.folder).failed()); REQUIRE(restored.deleteUserPreset().failed());
}
TEST_CASE("Shared preset codec rejects invalid complete files transactionally", "[adapter][preset][files][state]") {
    Library library; disdorktion::PluginProcessor p;
    auto file=library.folder.getChildFile("bad.disdorktionpreset");
    juce::MemoryBlock bytes; p.getStateInformation(bytes);
    auto valid=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),static_cast<int>(bytes.getSize()));
    for(const auto* value : {"nan","inf","1tail"," 1","1.0000000001"}) {
        auto bad=*valid; bad.setAttribute("globalMix",value); REQUIRE(file.replaceWithText(bad.toString()));
        REQUIRE(p.loadUserPreset(file).failed()); REQUIRE(p.getPresetInfo().factoryIndex==0);
        juce::MemoryBlock after; p.getStateInformation(after); REQUIRE(after==bytes);
    }
    for(const auto* version : {"4","6","5tail"}) {
        auto bad=*valid; bad.setAttribute("version",version); REQUIRE(file.replaceWithText(bad.toString()));
        REQUIRE(p.loadUserPreset(file).failed());
    }
    auto missing=*valid; missing.removeAttribute("driver"); REQUIRE(file.replaceWithText(missing.toString())); REQUIRE(p.loadUserPreset(file).failed());
    REQUIRE(p.savePresetAs(library.folder).failed()); REQUIRE(p.getPresetInfo().factoryIndex==0);
    REQUIRE(p.loadUserPreset(library.folder.getChildFile("missing.disdorktionpreset")).failed());
}
TEST_CASE("Preset baseline clears modified state and distinguishes identical user files", "[adapter][preset][files]") {
    Library library; disdorktion::PluginProcessor p;
    const auto a=library.folder.getChildFile("a.disdorktionpreset"), b=library.folder.getChildFile("b.disdorktionpreset");
    REQUIRE(p.savePresetAs(a).wasOk()); edit(p,2); REQUIRE(p.getPresetInfo().modified);
    edit(p,1); REQUIRE_FALSE(p.getPresetInfo().modified);
    REQUIRE(p.savePresetAs(b).wasOk()); REQUIRE(p.getPresetInfo().file==b);
    REQUIRE(p.loadUserPreset(a).wasOk()); REQUIRE(p.getPresetInfo().file==a);
    auto token=p.beginPresetOperation(); p.beginPresetOperation(); REQUIRE(p.savePresetAs(b,token).failed());
    token=p.beginPresetOperation(); juce::MemoryBlock state; p.getStateInformation(state);
    p.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    REQUIRE(p.deleteUserPreset(token).failed()); REQUIRE(a.existsAsFile());
    REQUIRE(a.deleteFile());
    REQUIRE(p.deleteUserPreset().failed()); REQUIRE(p.getPresetInfo().file==a);
    p.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    REQUIRE(p.getPresetInfo().type==disdorktion::PresetType::detached);
}
TEST_CASE("Plugin drive defaults leave shared harness defaults unchanged", "[adapter][preset][defaults]") {
    disdorktion::FullChainSettings shared;
    REQUIRE(disdorktion::getFullChainControl(shared,"tanhDriveDb")==-24);
    REQUIRE(disdorktion::getFullChainControl(shared,"folderDriveDb")==-24);
    disdorktion::PluginProcessor p;
    REQUIRE(p.parameterState().getParameter("tanhDriveDb")->getDefaultValue()==p.parameterState().getParameter("tanhDriveDb")->convertTo0to1(0));
    REQUIRE(p.parameterState().getParameter("folderDriveDb")->getDefaultValue()==p.parameterState().getParameter("folderDriveDb")->convertTo0to1(0));
}
TEST_CASE("Host restore serialization and preset identity refresh synchronize across threads", "[adapter][preset][state][concurrency]") {
    Library library; disdorktion::PluginProcessor p;
    REQUIRE(p.savePresetAs(library.folder.getChildFile("threaded.disdorktionpreset")).wasOk());
    juce::MemoryBlock original; p.getStateInformation(original);
    std::atomic<bool> done{false},valid{true};
    std::thread host([&] {
        for(int i=0;i<100;++i) {
            p.setStateInformation(original.getData(),static_cast<int>(original.getSize()));
            edit(p,static_cast<float>(1+i%4));
            juce::MemoryBlock state; p.getStateInformation(state);
            auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),static_cast<int>(state.getSize()));
            disdorktion::PresetValues values;
            if(!xml || disdorktion::PresetManager::decode(*xml,values).failed()) valid.store(false);
        }
        done.store(true);
    });
    do {
        const auto info=p.getPresetInfo();
        if(info.type!=disdorktion::PresetType::user || info.file.getFileName()!="threaded.disdorktionpreset") valid.store(false);
        juce::MemoryBlock state; p.getStateInformation(state);
    } while(!done.load());
    host.join(); REQUIRE(valid.load());
}

TEST_CASE("Invalid optional host metadata detaches targets without rejecting valid sound", "[adapter][preset][state][files]") {
    Library library; disdorktion::PluginProcessor p;
    const auto file=library.folder.getChildFile("associated.disdorktionpreset"); REQUIRE(p.savePresetAs(file).wasOk()); edit(p,3);
    juce::MemoryBlock bytes; p.getStateInformation(bytes);
    const auto original=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),static_cast<int>(bytes.getSize()));
    for(int scenario=0;scenario<4;++scenario) {
        auto xml=*original; auto* metadata=xml.getChildByName("PresetIdentity"); REQUIRE(metadata);
        if(scenario==0) metadata->setAttribute("type","unsupported");
        if(scenario==1) metadata->setAttribute("file","relative.disdorktionpreset");
        if(scenario==2) metadata->getChildByName("DisDorktionState")->setAttribute("driver","nan");
        if(scenario==3) xml.addChildElement(new juce::XmlElement(*metadata));
        juce::AudioProcessor::copyXmlToBinary(xml,bytes); p.setStateInformation(bytes.getData(),static_cast<int>(bytes.getSize()));
        REQUIRE(p.getPresetInfo().type==disdorktion::PresetType::detached);
        REQUIRE(p.parameterState().getRawParameterValue("driver")->load()==3);
        REQUIRE(p.deleteUserPreset().failed()); REQUIRE(file.existsAsFile());
    }
}
TEST_CASE("Failed atomic preset writes preserve existing data and associations", "[adapter][preset][files]") {
    Library library; disdorktion::PluginProcessor p;
    const auto file=library.folder.getChildFile("protected.disdorktionpreset"); REQUIRE(p.savePresetAs(file).wasOk());
    const auto original=file.loadFileAsString(); disdorktion::PresetValues values;
    REQUIRE(disdorktion::PresetManager::readFile(file,values).wasOk()); values[0]=std::numeric_limits<float>::quiet_NaN();
    REQUIRE(disdorktion::PresetManager::writeFile(file,values).failed()); REQUIRE(file.loadFileAsString()==original);
    REQUIRE(p.savePresetAs(file.getChildFile("impossible.disdorktionpreset")).failed());
    REQUIRE(p.getPresetInfo().file==file); REQUIRE(file.loadFileAsString()==original);
}
TEST_CASE("Preset load baselines retain target values when host automation runs during publication", "[adapter][preset][state][files]") {
    struct Automation final : juce::AudioProcessorValueTreeState::Listener {
        disdorktion::PluginProcessor& processor;
        explicit Automation(disdorktion::PluginProcessor& p) : processor(p) { processor.parameterState().addParameterListener("outputGainDb",this); }
        ~Automation() override { processor.parameterState().removeParameterListener("outputGainDb",this); }
        void parameterChanged(const juce::String&,float) override { edit(processor,4); }
    };
    Library library;
    disdorktion::PluginProcessor factory;
    {
        Automation automation(factory); REQUIRE(factory.loadFactoryPreset(2));
        REQUIRE(factory.parameterState().getRawParameterValue("driver")->load()==4);
        REQUIRE(factory.getPresetInfo().modified);
        edit(factory,1); REQUIRE_FALSE(factory.getPresetInfo().modified);
    }
    const auto file=library.folder.getChildFile("target.disdorktionpreset"); REQUIRE(factory.savePresetAs(file).wasOk());
    disdorktion::PluginProcessor user;
    {
        Automation automation(user); REQUIRE(user.loadUserPreset(file).wasOk());
        REQUIRE(user.parameterState().getRawParameterValue("driver")->load()==4);
        REQUIRE(user.getPresetInfo().modified);
        edit(user,1); REQUIRE_FALSE(user.getPresetInfo().modified);
    }
}
TEST_CASE("Concurrent factory saves never replace the winning same-name user preset", "[adapter][preset][files][concurrency]") {
    Library library;
    for(int round=0;round<12;++round) {
        const auto folder=library.folder.getChildFile(juce::String(round));
        disdorktion::PluginProcessor first,second;
        REQUIRE(first.loadFactoryPreset(2)); REQUIRE(second.loadFactoryPreset(2));
        edit(first,2); edit(second,3);
        std::atomic<int> ready{0}; std::atomic<bool> go{false}; bool firstSaved=false,secondSaved=false;
        const auto save=[&](disdorktion::PluginProcessor& processor,bool& succeeded) {
            ready.fetch_add(1);
            while(!go.load()) std::this_thread::yield();
            succeeded=processor.savePreset(folder).wasOk();
        };
        std::thread a(save,std::ref(first),std::ref(firstSaved)),b(save,std::ref(second),std::ref(secondSaved));
        while(ready.load()!=2) std::this_thread::yield(); go.store(true); a.join(); b.join();
        REQUIRE(firstSaved!=secondSaved);
        const auto file=folder.getChildFile("Crunch.disdorktionpreset");
        disdorktion::PluginProcessor loaded; REQUIRE(loaded.loadUserPreset(file).wasOk());
        REQUIRE(loaded.parameterState().getRawParameterValue("driver")->load()==(firstSaved ? 2 : 3));
        REQUIRE((firstSaved ? first : second).getPresetInfo().type==disdorktion::PresetType::user);
        REQUIRE((firstSaved ? second : first).getPresetInfo().type==disdorktion::PresetType::factory);
    }
}
TEST_CASE("Host user metadata validates file contents before restoring a destructive target", "[adapter][preset][files][state]") {
    Library library; disdorktion::PluginProcessor source;
    const auto valid=library.folder.getChildFile("external.custom"); REQUIRE(source.savePresetAs(valid).wasOk()); edit(source,3);
    juce::MemoryBlock bytes; source.getStateInformation(bytes);
    const auto original=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),static_cast<int>(bytes.getSize()));
    const auto unrelated=library.folder.getChildFile("unrelated.wav"); const juce::String unrelatedBytes("unrelated file content");
    REQUIRE(unrelated.replaceWithText(unrelatedBytes));
    auto tampered=*original; tampered.getChildByName("PresetIdentity")->setAttribute("file",unrelated.getFullPathName());
    juce::AudioProcessor::copyXmlToBinary(tampered,bytes);
    disdorktion::PluginProcessor restored; restored.setStateInformation(bytes.getData(),static_cast<int>(bytes.getSize()));
    REQUIRE(restored.getPresetInfo().type==disdorktion::PresetType::detached);
    REQUIRE(restored.parameterState().getRawParameterValue("driver")->load()==3);
    REQUIRE(restored.savePreset(library.folder).failed()); REQUIRE(restored.deleteUserPreset().failed());
    REQUIRE(unrelated.loadFileAsString()==unrelatedBytes);
    juce::AudioProcessor::copyXmlToBinary(*original,bytes); restored.setStateInformation(bytes.getData(),static_cast<int>(bytes.getSize()));
    REQUIRE(restored.getPresetInfo().type==disdorktion::PresetType::user); REQUIRE(restored.getPresetInfo().file==valid);
    REQUIRE(restored.getPresetInfo().modified); REQUIRE(restored.parameterState().getRawParameterValue("driver")->load()==3);
}
