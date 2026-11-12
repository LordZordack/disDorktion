#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace disdorktion
{
// Asynchronous UI boundary: native dialogs in production, controlled completions
// in tests. Completions may outlive the editor and must never retain it directly.
class PresetDialogs
{
public:
    virtual ~PresetDialogs() = default;
    virtual void showMenu(juce::PopupMenu, juce::Component&, std::function<void(int)>) = 0;
    virtual void chooseFile(bool save, const juce::File& suggested, std::function<void(juce::File)>) = 0;
    virtual void confirmOverwrite(const juce::File&, std::function<void(bool)>) = 0;
    virtual void showError(const juce::String&) = 0;
};
}
