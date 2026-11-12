#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/FullChainControls.h"

namespace disdorktion {
using PresetValues = std::array<float, fullChainControls.size()>;
enum class PresetType { factory, user, detached };
struct PresetInfo {
    juce::String name;
    PresetType type = PresetType::detached;
    juce::File file;
    bool modified = false;
    std::uint64_t generation = 0;
    int factoryIndex = -1;
};
// Side-effect-free, shared version-5 host/file codec. Values use DSP units.
class PresetManager {
public:
    static std::unique_ptr<juce::XmlElement> encode(const PresetValues&);
    static juce::Result decode(const juce::XmlElement&, PresetValues&);
    static juce::Result readFile(const juce::File&, PresetValues&);
    static juce::Result writeFile(const juce::File&, const PresetValues&, bool overwriteExisting = true);
    static juce::File defaultDirectory();
    static juce::Array<juce::File> list(const juce::File&);
};
}
