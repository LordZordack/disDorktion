#pragma once

#include "FaceplateDrawing.h"

namespace disdorktion::faceplate
{
class RotarySlider final : public juce::Slider
{
public:
    explicit RotarySlider(const FaceplateLayout::Control& placement) : geometry(placement)
    {
        const auto area = geometry.knobBounds().getUnion(geometry.value.bounds).getSmallestIntegerContainer();
        origin = area.getPosition();
        valueClip = geometry.value.clipAt(origin);
        setBounds(area);
    }

    juce::Rectangle<float> knobBounds() const { return geometry.knobBounds() - origin.toFloat(); }
    juce::Rectangle<float> valueBounds() const { return geometry.value.bounds - origin.toFloat(); }

    bool hitTest(int x, int y) override
    {
        const juce::Point<float> point(static_cast<float>(x), static_cast<float>(y));
        return knobBounds().contains(point) || valueBounds().contains(point);
    }

    const FaceplateLayout::Control& geometry;
    juce::Point<int> origin;
    juce::Path valueClip;
};

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit LookAndFeel(const FaceplateAssets& resources) : assets(resources)
    {
        setColour(juce::Slider::textBoxTextColourId, phosphor);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override
    {
        if (const auto* rotary = dynamic_cast<const RotarySlider*>(&slider))
            return {rotary->knobBounds().getSmallestIntegerContainer(), rotary->valueBounds().getSmallestIntegerContainer()};
        return juce::LookAndFeel_V4::getSliderLayout(slider);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float position,
                          float start, float end, juce::Slider& slider) override
    {
        if (const auto* rotary = dynamic_cast<const RotarySlider*>(&slider))
        {
            const auto frame = juce::roundToInt((FaceplateLayout::frames - 1) * juce::jlimit(0.0f, 1.0f, position));
            g.drawImage(assets.knobs[static_cast<std::size_t>(frame)], rotary->knobBounds());
            if (slider.hasKeyboardFocus(false))
            {
                g.setColour(phosphor);
                g.drawEllipse(rotary->knobBounds().reduced(4), 1.5f);
            }
            return;
        }
        juce::LookAndFeel_V4::drawRotarySlider(g, x, y, w, h, position, start, end, slider);
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        if (const auto* slider = dynamic_cast<const RotarySlider*>(label.getParentComponent()))
        {
            if (!label.isBeingEdited())
            {
                const juce::Graphics::ScopedSaveState saved(g);
                g.addTransform(juce::AffineTransform::translation(-static_cast<float>(label.getX()),
                                                                  -static_cast<float>(label.getY())));
                if (g.reduceClipRegion(slider->valueClip))
                    drawReadout(g, slider->valueBounds(), label.getText());
            }
            return;
        }
        juce::LookAndFeel_V4::drawLabel(g, label);
    }

private:
    const FaceplateAssets& assets;
};

class PresetButton final : public juce::TextButton
{
public:
    void setDisplay(juce::String name, bool isModified)
    {
        if (name == fullName && modified == isModified)
            return;
        fullName = std::move(name);
        modified = isModified;
        setTitle("Presets: " + fullName + (modified ? ", modified" : ""));
        setButtonText(fullName + (modified ? " *" : ""));
    }

    void paintButton(juce::Graphics& g, bool hovered, bool pressed) override
    {
        const juce::Graphics::ScopedSaveState saved(g);
        const auto box = window.bounds - getPosition().toFloat();
        if (!g.reduceClipRegion(window.clipAt(getPosition())))
            return;
        if (hovered || pressed || hasKeyboardFocus(false))
        {
            g.setColour(phosphor.withAlpha(pressed ? 0.2f : 0.55f));
            g.drawRoundedRectangle(box.reduced(1), window.radius, 1.5f);
        }
        drawPreset(g, box, fullName, modified);
    }

    FaceplateLayout::Window window;

private:
    juce::String fullName;
    bool modified = false;
};
}
