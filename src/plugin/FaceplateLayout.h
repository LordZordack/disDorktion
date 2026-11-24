#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <optional>

namespace disdorktion
{
struct FaceplateLayout
{
    static constexpr int width = 1040;
    static constexpr int height = 948;
    static constexpr int columns = 11;
    static constexpr int frames = 121;
    static constexpr int logicalKnobSize = 96;
    static constexpr int atlasTileSize = 384;

    struct Window
    {
        juce::Rectangle<float> bounds;
        float radius = 0;
        juce::Path clipAt(juce::Point<int> origin) const;
    };

    struct Control
    {
        const char* parameter = nullptr;
        juce::Point<float> pivot;
        Window value;
        juce::Rectangle<float> knobBounds() const;
    };

    struct Meter
    {
        Window track, value;
    };

    std::array<Control, 27> controls;
    Window inputScope, outputScope, preset, die;
    Meter inputMeter, outputMeter;

    static std::optional<FaceplateLayout> parse(const juce::String&);
};

struct FaceplateAssets
{
    std::optional<FaceplateLayout> layout;
    juce::Image plate;
    std::array<juce::Image, FaceplateLayout::frames> knobs;

    static const FaceplateAssets& embedded();
};
}
