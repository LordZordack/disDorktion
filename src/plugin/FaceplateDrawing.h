#pragma once

#include "FaceplateLayout.h"

namespace disdorktion::faceplate
{
inline const juce::Colour phosphor{0xffa3eeaf};
inline const juce::Colour dimSegment{0xff28403d};
inline const juce::Colour scopeHalo{0xdc307047};
inline const juce::Colour overloadColour{0xffff6f75};

inline constexpr std::array<unsigned, 10> digits{0x3f, 0x06, 0x5b, 0x4f, 0x66,
                                                0x6d, 0x7d, 0x07, 0x7f, 0x6f};
inline constexpr std::array<std::array<float, 4>, 7> segments{{
    {0.2f, 0, 0.8f, 0}, {1, 0.12f, 1, 0.38f}, {1, 0.62f, 1, 0.88f},
    {0.2f, 1, 0.8f, 1}, {0, 0.62f, 0, 0.88f}, {0, 0.12f, 0, 0.38f},
    {0.2f, 0.5f, 0.8f, 0.5f}
}};

inline void drawSegments(juce::Graphics& g, juce::Rectangle<float> box, unsigned mask,
                         bool drawInactive, float stroke, juce::Colour active)
{
    for (std::size_t i = 0; i < segments.size(); ++i)
    {
        const auto lit = (mask & (1u << i)) != 0;
        if (!lit && !drawInactive)
            continue;
        const auto& p = segments[i];
        g.setColour(lit ? active : dimSegment);
        g.drawLine(box.getX() + p[0] * box.getWidth(), box.getY() + p[1] * box.getHeight(),
                   box.getX() + p[2] * box.getWidth(), box.getY() + p[3] * box.getHeight(), stroke);
    }
}

inline void drawReadout(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& value,
                        juce::Colour colour = phosphor)
{
    const auto text = value.trim().replaceCharacter(0x2212, '-');
    int length = 0;
    float numberWidth = 0;
    for (auto c : text)
    {
        if (c != '-' && c != '.' && (c < '0' || c > '9'))
            break;
        ++length;
        numberWidth += c == '.' ? 4.0f : 9.0f;
    }

    const auto suffix = text.substring(length).trim();
    g.setFont(11.0f);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(g.getCurrentFont(), suffix, 0, 0);
    const auto suffixWidth = glyphs.getBoundingBox(0, -1, true).getWidth() + 1.0f;
    const auto gap = length != 0 && suffix.isNotEmpty() ? 3.0f : 0.0f;
    const auto available = bounds.getWidth() - 4.0f;
    if (length == 0 || numberWidth + gap + suffixWidth > available)
    {
        g.setColour(colour);
        g.drawFittedText(text, bounds.getSmallestIntegerContainer().reduced(2, 0),
                         juce::Justification::centred, 1);
        return;
    }

    auto x = bounds.getCentreX() - (numberWidth + gap + suffixWidth) * 0.5f;
    const auto y = bounds.getCentreY() - 6.0f;
    for (int i = 0; i < length; ++i)
    {
        const auto c = text[i];
        if (c == '.')
        {
            g.setColour(colour);
            g.fillEllipse(x, y + 10.8f, 1.5f, 1.5f);
            x += 4.0f;
        }
        else
        {
            const auto mask = c == '-' ? 0x40u : digits[static_cast<std::size_t>(c - '0')];
            drawSegments(g, {x, y, 7.0f, 12.0f}, mask, true, 1.2f, colour);
            x += 9.0f;
        }
    }
    g.setColour(colour);
    g.drawText(suffix, juce::Rectangle<float>(x + gap, bounds.getY(), suffixWidth, bounds.getHeight()),
               juce::Justification::centredLeft, false);
}

inline unsigned presetMask(juce::juce_wchar c)
{
    // Local seven-segment approximation for the source artwork's alphabet.
    constexpr std::array<unsigned, 26> letters{0x77, 0x7c, 0x39, 0x5e, 0x79, 0x71, 0x3d,
        0x76, 0x06, 0x1e, 0x75, 0x38, 0x37, 0x54, 0x3f, 0x73, 0x67, 0x50, 0x6d,
        0x78, 0x3e, 0x1c, 0x2a, 0x76, 0x6e, 0x5b};
    if (c >= 'A' && c <= 'Z') return letters[static_cast<std::size_t>(c - 'A')];
    if (c >= '0' && c <= '9') return digits[static_cast<std::size_t>(c - '0')];
    if (c == ' ') return 0;
    if (c == '_') return 0x08;
    return 0x40;
}

inline void drawPreset(juce::Graphics& g, juce::Rectangle<float> bounds,
                       const juce::String& name, bool modified)
{
    const auto shown = name.toUpperCase().substring(0, 12);
    constexpr auto reserved = 13.0f;
    auto lettering = bounds;
    if (modified)
    {
        const auto marker = lettering.removeFromRight(reserved);
        g.setColour(phosphor);
        g.setFont(12.0f);
        g.drawText("*", marker, juce::Justification::centred, false);
    }
    if (shown.isEmpty())
        return;
    const auto pitch = juce::jmin(0.65f * lettering.getHeight(), lettering.getWidth() / 13.0f);
    const auto size = juce::jmin(lettering.getHeight(), pitch / 0.65f);
    const auto glyphWidth = 0.35f * size;
    const auto glyphHeight = 0.69f * size;
    auto x = lettering.getCentreX() - ((shown.length() - 1) * pitch + glyphWidth) * 0.5f;
    for (auto c : shown)
    {
        drawSegments(g, {x, lettering.getCentreY() - glyphHeight * 0.5f, glyphWidth, glyphHeight},
                     presetMask(c), false, 1.5f, phosphor);
        x += pitch;
    }
}
}
