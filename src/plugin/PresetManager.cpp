#include "PresetManager.h"
#include <charconv>
#include <limits>
#include <string_view>

namespace disdorktion {
namespace {
void attribute(juce::XmlElement& xml, const char* id, float value) {
    char text[64];
    const auto result=std::to_chars(text,text+sizeof(text),value,std::chars_format::general,std::numeric_limits<float>::max_digits10);
    xml.setAttribute(id,juce::String::fromUTF8(text,static_cast<int>(result.ptr-text)));
}
bool scalar(const juce::XmlElement& xml,const FullChainControl& control,float& value) {
    if(!xml.hasAttribute(control.id)) return false;
    const auto text=xml.getStringAttribute(control.id).toStdString();
    if(control.discrete && control.minimum==0 && control.maximum==1) {
        if(text!="0" && text!="1") return false;
        value=text=="1" ? 1.0f : 0.0f; return true;
    }
    const auto bound=[](float value) {
        char buffer[64]; const auto written=std::to_chars(buffer,buffer+sizeof(buffer),value);
        double result=0; std::from_chars(buffer,written.ptr,result); return result;
    };
    double source=0; const auto parsed=std::from_chars(text.data(),text.data()+text.size(),source);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(source)
       || source<bound(control.minimum) || source>bound(control.maximum)
       || (control.discrete && source!=std::floor(source))) return false;
    const auto narrowed=std::from_chars(text.data(),text.data()+text.size(),value);
    return narrowed.ec==std::errc{} && narrowed.ptr==text.data()+text.size() && std::isfinite(value);
}
}
std::unique_ptr<juce::XmlElement> PresetManager::encode(const PresetValues& values) {
    auto xml=std::make_unique<juce::XmlElement>("DisDorktionState"); xml->setAttribute("version","5");
    for(std::size_t i=0;i<values.size();++i) attribute(*xml,fullChainControls[i].id,values[i]);
    return xml;
}
juce::Result PresetManager::decode(const juce::XmlElement& xml,PresetValues& destination) {
    if(!xml.hasTagName("DisDorktionState") || xml.getStringAttribute("version")!="5")
        return juce::Result::fail("Unsupported preset format (expected DisDorktion version 5).");
    PresetValues values{}; FullChainSettings settings;
    for(std::size_t i=0;i<values.size();++i)
        if(!scalar(xml,fullChainControls[i],values[i]) || !setFullChainControl(settings,fullChainControls[i].id,values[i]))
            return juce::Result::fail("Missing or invalid parameter: "+juce::String(fullChainControls[i].id));
    if(!isValidFullChainSettings(settings)) return juce::Result::fail("Invalid preset settings.");
    destination=values; return juce::Result::ok();
}
juce::Result PresetManager::readFile(const juce::File& file,PresetValues& values) {
    if(!file.existsAsFile()) return juce::Result::fail("Preset file does not exist.");
    if(file.getSize()>1024*1024) return juce::Result::fail("Preset file is too large.");
    const auto xml=juce::XmlDocument::parse(file);
    if(!xml) return juce::Result::fail("Unable to read preset XML.");
    return decode(*xml,values);
}
juce::Result PresetManager::writeFile(const juce::File& file,const PresetValues& values,bool overwriteExisting) {
    if(file==juce::File{} || file.isDirectory()) return juce::Result::fail("Choose a preset file.");
    const auto xml=encode(values); PresetValues validated;
    if(auto result=decode(*xml,validated); result.failed()) return result;
    // POSIX process locks do not exclude threads in the same process. Serialize
    // local writers as well as plugin instances running in separate hosts.
    static juce::CriticalSection localWriters;
    const juce::ScopedLock localLock(localWriters);
    auto path=file.getFullPathName();
    if(!juce::File::areFileNamesCaseSensitive()) path=path.toLowerCase();
    juce::InterProcessLock processLock("DisDorktionPreset-"+juce::String::toHexString(path.hashCode64()));
    const juce::InterProcessLock::ScopedLockType processGuard(processLock);
    if(!processGuard.isLocked()) return juce::Result::fail("Unable to lock preset file for saving.");
    // The existence check and final replacement share both locks for every
    // writer. A factory Save must never replace another instance's user file.
    if(!overwriteExisting && file.exists())
        return juce::Result::fail("A user preset with this name already exists. Use Save preset as to choose or replace a file.");
    if(auto result=file.getParentDirectory().createDirectory(); result.failed()) return result;
    juce::TemporaryFile temporary(file);
    if(!temporary.getFile().replaceWithText(xml->toString())) return juce::Result::fail("Unable to write temporary preset file.");
    if(!temporary.overwriteTargetFileWithTemporary()) return juce::Result::fail("Unable to replace preset file.");
    return juce::Result::ok();
}
juce::File PresetManager::defaultDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("DisDorktion/Presets");
}
juce::Array<juce::File> PresetManager::list(const juce::File& folder) {
    auto files=folder.findChildFiles(juce::File::findFiles,false,"*.disdorktionpreset");
    files.sort(); return files;
}
}

#include "PluginProcessor.h"
#include "FactoryPresets.h"

namespace disdorktion {
PresetValues PluginProcessor::capturePresetValues() const {
    PresetValues values{};
    for(std::size_t i=0;i<values.size();++i) {
        auto value=chainControls[i]->load(std::memory_order_relaxed);
        if(std::string_view(fullChainControls[i].id)=="bitDepth") {
            const auto index=std::isfinite(value) && value>=0 && value<static_cast<float>(bitCrusherDepths.size()) ? static_cast<int>(value) : -1;
            value=std::isfinite(value) && value==index && index>=0 && index<static_cast<int>(bitCrusherDepths.size())
                ? static_cast<float>(bitCrusherDepths[static_cast<std::size_t>(index)]) : std::numeric_limits<float>::quiet_NaN();
        }
        values[i]=value;
    }
    return values;
}
void PluginProcessor::publishPresetValues(const PresetValues& values,bool gestures) {
    for(std::size_t i=0;i<values.size();++i) {
        auto* parameter=parameters.getParameter(fullChainControls[i].id);
        if(gestures) parameter->beginChangeGesture();
        publishControl(fullChainControls[i].id,values[i]);
        if(gestures) parameter->endChangeGesture();
    }
}
void PluginProcessor::associatePreset(PresetType type,const juce::String& name,const juce::File& file,int index) {
    presetIdentity.type=type; presetIdentity.name=name; presetIdentity.file=file; presetIdentity.factoryIndex=index;
    presetBaseline=capturePresetValues(); ++presetGeneration;
}
PresetInfo PluginProcessor::getPresetInfo() const {
    const juce::ScopedLock lock(presetIdentityLock);
    auto result=presetIdentity; result.generation=presetGeneration; result.modified=false;
    const auto current=capturePresetValues();
    for(std::size_t i=0;i<current.size();++i) {
        const auto* parameter=parameters.getParameter(fullChainControls[i].id);
        const auto host=[&](float value) {
            if(std::string_view(fullChainControls[i].id)=="bitDepth")
                for(std::size_t depth=0;depth<bitCrusherDepths.size();++depth)
                    if(value==static_cast<float>(bitCrusherDepths[depth])) return static_cast<float>(depth);
            return value;
        };
        if(!std::isfinite(current[i]) || std::abs(parameter->convertTo0to1(host(current[i]))-parameter->convertTo0to1(host(presetBaseline[i])))>2.0e-6f) {
            result.modified=true; break;
        }
    }
    return result;
}
std::uint64_t PluginProcessor::beginPresetOperation() {
    const juce::ScopedLock lock(presetIdentityLock); return ++presetGeneration;
}
juce::File PluginProcessor::defaultUserPresetDirectory() { return PresetManager::defaultDirectory(); }
juce::Array<juce::File> PluginProcessor::listUserPresets(const juce::File& folder) const { return PresetManager::list(folder); }
bool PluginProcessor::isPresetOperationCurrent(std::uint64_t expected) const {
    return expected==0 || expected==presetGeneration;
}
juce::Result PluginProcessor::savePreset(const juce::File& folder,std::uint64_t expected) {
    const juce::ScopedLock lock(presetIdentityLock);
    if(!isPresetOperationCurrent(expected)) return juce::Result::fail("Preset operation expired; the current preset changed.");
    if(presetIdentity.type==PresetType::detached) return juce::Result::fail("Use Save preset as to choose a file.");
    if(presetIdentity.type==PresetType::user) return savePresetAs(presetIdentity.file,expected);
    const auto file=folder.getChildFile(juce::File::createLegalFileName(presetIdentity.name)+".disdorktionpreset");
    const auto values=capturePresetValues();
    if(auto result=PresetManager::writeFile(file,values,false); result.failed()) return result;
    associatePreset(PresetType::user,file.getFileNameWithoutExtension(),file,-1);
    presetBaseline=values;
    return juce::Result::ok();
}
juce::Result PluginProcessor::savePresetAs(const juce::File& chosen,std::uint64_t expected) {
    const juce::ScopedLock lock(presetIdentityLock);
    if(!isPresetOperationCurrent(expected)) return juce::Result::fail("Preset operation expired; the current preset changed.");
    if(chosen==juce::File{} || chosen.isDirectory()) return juce::Result::fail("Choose a preset file.");
    const auto file=chosen.getFileExtension().isEmpty() ? chosen.withFileExtension(".disdorktionpreset") : chosen;
    const auto values=capturePresetValues();
    if(auto result=PresetManager::writeFile(file,values); result.failed()) return result;
    associatePreset(PresetType::user,file.getFileNameWithoutExtension(),file,-1);
    // Baseline is exactly the snapshot written, even if host automation continued during I/O.
    presetBaseline=values;
    return juce::Result::ok();
}
juce::Result PluginProcessor::loadUserPreset(const juce::File& file,std::uint64_t expected) {
    const juce::ScopedLock lock(presetIdentityLock);
    if(!isPresetOperationCurrent(expected)) return juce::Result::fail("Preset operation expired; the current preset changed.");
    PresetValues values;
    if(auto result=PresetManager::readFile(file,values); result.failed()) return result;
    publishPresetValues(values,true);
    associatePreset(PresetType::user,file.getFileNameWithoutExtension(),file,-1);
    // Automation may change an earlier parameter while later ones publish.
    // Retain the file's target, rather than treating that edit as saved.
    presetBaseline=values;
    return juce::Result::ok();
}
juce::Result PluginProcessor::resetToDefaults(std::uint64_t expected) {
    const juce::ScopedLock lock(presetIdentityLock);
    if(!isPresetOperationCurrent(expected)) return juce::Result::fail("Preset operation expired; the current preset changed.");
    return loadFactoryPreset(0) ? juce::Result::ok() : juce::Result::fail("Unable to reset defaults.");
}
juce::Result PluginProcessor::deleteUserPreset(std::uint64_t expected) {
    const juce::ScopedLock lock(presetIdentityLock);
    if(!isPresetOperationCurrent(expected)) return juce::Result::fail("Preset operation expired; the current preset changed.");
    if(presetIdentity.type!=PresetType::user) return juce::Result::fail("Only user presets can be deleted.");
    if(!presetIdentity.file.existsAsFile() || !presetIdentity.file.deleteFile()) return juce::Result::fail("Unable to delete preset file.");
    return resetToDefaults();
}
void PluginProcessor::writePresetIdentity(juce::XmlElement& state) const {
    if(presetIdentity.type==PresetType::detached) return;
    auto metadata=std::make_unique<juce::XmlElement>("PresetIdentity");
    if(presetIdentity.type==PresetType::factory) {
        metadata->setAttribute("type","factory");
        metadata->setAttribute("key",factoryPresets[static_cast<std::size_t>(presetIdentity.factoryIndex)].key);
    } else {
        metadata->setAttribute("type","user"); metadata->setAttribute("file",presetIdentity.file.getFullPathName());
    }
    metadata->addChildElement(PresetManager::encode(presetBaseline).release()); state.addChildElement(metadata.release());
}
void PluginProcessor::restorePresetIdentity(const juce::XmlElement& state) {
    associatePreset(PresetType::detached,"Restored state",{},-1);
    const auto* metadata=state.getChildByName("PresetIdentity");
    if(!metadata || metadata->getNextElementWithTagName("PresetIdentity")) return;
    const auto* baseline=metadata->getChildByName("DisDorktionState"); PresetValues values;
    if(!baseline || PresetManager::decode(*baseline,values).failed()) return;
    const auto type=metadata->getStringAttribute("type");
    if(type=="factory") {
        const auto key=metadata->getStringAttribute("key");
        for(std::size_t i=0;i<factoryPresets.size();++i) if(key==factoryPresets[i].key) {
            associatePreset(PresetType::factory,factoryPresets[i].name,{},static_cast<int>(i)); presetBaseline=values; return;
        }
    } else if(type=="user") {
        const auto path=metadata->getStringAttribute("file");
        if(!juce::File::isAbsolutePath(path)) return;
        const juce::File file(path);
        // Host metadata must not turn an unrelated existing file into a
        // destructive Save/Delete target. Extensions are intentionally free:
        // explicit Save as supports external presets with arbitrary names.
        PresetValues diskValues;
        if(PresetManager::readFile(file,diskValues).failed()) return;
        associatePreset(PresetType::user,file.getFileNameWithoutExtension(),file,-1); presetBaseline=values;
    }
}
}
