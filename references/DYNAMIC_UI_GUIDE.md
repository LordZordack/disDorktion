# DisDorktion dynamic UI implementation guide

This guide describes the live parts of the v009 face in a JUCE editor. The [placement map](layout.json) supplies the geometry. The upstream 2× assets, preview compositor, and preset glyph script belong to the artwork source project.

The [assembled 4× front render](disdorktion_interface@4x.png) includes knobs and example values. Use it to compare artwork, placement, and styling with the live editor.

## Artwork and coordinates

Draw the static faceplate first. It includes the enclosure, labels, signal-flow diagram, fixed arrowheads, knob collars, display bezels, scope graticules, and filter grooves. Draw the knob caps and live content on top. Each PNG preserves alpha. The straight-on artwork shows the plugin front.

| Assets | Scale | Faceplate size | Atlas size | Tile size | Source pivot |
| --- | --- | --- | --- | --- | --- |
| Upstream faceplate and atlas | 2× | 2080 × 1896 | 2112 × 2112 | 192 × 192 | (96, 96) |
| [Faceplate](faceplate@4x.png) and [atlas](knob_atlas@4x.png) | 4× | 4160 × 3792 | 4224 × 4224 | 384 × 384 | (192, 192) |

- Use a **1040 × 948** logical canvas with the origin at the upper left. Positive x points right and positive y points down.
- Read each rectangle as `[x, y, width, height]`. Its `cornerRadius` is also in logical pixels. Treat these rectangles as the glass interiors. Clip live content to their rounded paths and let the static PNG supply the bezel and glass color.
- The JSON describes the 2× assets. When using 4× artwork, double the **source** atlas tile and pivot coordinates. Keep all logical positions, rectangles, and radii from the JSON. To resize the editor, apply one uniform transform to the full composition and fit it within the available bounds.
- Use the IDs in `controls` to map the 27 artwork positions to plugin parameters. The file does not define DSP parameter IDs, ranges, defaults, units, or normalization curves. Values under `preview*` are examples.
- The preview uses active phosphor **#A3EEAF** and dim segments **#28403D**. The scope adds a halo at RGBA **(48, 112, 71, 220)**. These are overlay colors. The exported art already supplies the background colors.

## Rotary knobs

Each atlas has **121 frames** packed into an **11 × 11 row-major grid**. The white pointer follows a clockwise 270° sweep. At frame 0 it sits 135° counterclockwise from noon, at frame 60 it points to noon, and at frame 120 it sits 135° clockwise from noon.

```text
u = clamp(parameterNormalized, 0, 1)
frame = floor(120 * u + 0.5)
column = frame % 11
row = frame // 11
source = (column * tile, row * tile, tile, tile)
destination = (pivot.x - 48, pivot.y - 48, 96, 96)
```

Use the normalized position supplied by JUCE to the rotary renderer. This follows logarithmic slider travel for frequency and the selected step for stepped controls. Draw the tile with alpha and center it on `controls[i].pivot`. The atlas already includes the rendered rotation, recessed marker, and lighting. Use the logical destination rectangle for a rotary control's hit area. Format the readout separately so it can show the parameter's actual units or choice name.

## Numeric seven-segment readouts

Use `controls[i].valueWindow` for the readout below each knob and `meters[name].valueWindow` for the dB displays. Use the static blue-gray glass as the background. Draw seven separate strokes with gaps at the joins. The reference compositor gives the leading numeric characters a seven-segment treatment and uses plain sans-serif text for suffixes and named states.

```text
      a
   f     b
      g
   e     c
      d
```

| Segment | Start (x, y) | End (x, y) |
| --- | --- | --- |
| a | (1.4, 0) | (5.6, 0) |
| b | (7, 1.4) | (7, 4.6) |
| c | (7, 7.4) | (7, 10.6) |
| d | (1.4, 12) | (5.6, 12) |
| e | (0, 7.4) | (0, 10.6) |
| f | (0, 1.4) | (0, 4.6) |
| g | (1.4, 6) | (5.6, 6) |

Use these local coordinates in logical pixels. The numeric mask table is `0=abcdef, 1=bc, 2=abdeg, 3=abcdg, 4=bcfg, 5=acdfg, 6=acdefg, 7=abc, 8=abcdefg, 9=abcdfg`. The minus sign (ASCII `-` or Unicode `−`) lights only `g`. Draw lit strokes in active phosphor and the other strokes in the dim color. The dot is a 1.5 × 1.5 px circle with its top-left at `(cursor, top + 10.8)`.

- Use about **1.2 px** stroke width, **9 px** advance per digit or minus, and **4 px** advance per dot. Set `top = y + h/2 - 6`.
- Measure the complete line before drawing. Center the sum of the numeric advances, the suffix width, and a **3 px** gap when both are present. Place the suffix at the vertical center in an **11 px** sans-serif font. The Pillow preview uses `x` for the multiplication sign.
- Examples include `−6.0 dB`, `20 kHz`, `100%`, and `25.0 ms`. Text-only strings such as `Off` and `Butterworth` are centered as text. For longer or localized strings, choose a shorter display format or fit the text within the window. The meter window can use a short floor value or a text-only `−∞ dB` state if the plugin exposes that state.

## Input and output waveforms

Use `waveforms.input` for the left scope and `waveforms.output` for the right scope. Both use the same dimensions and trace styling. The graticule and centerline are in the faceplate. Draw the waveform on a transparent overlay, then clip it to that scope's rounded glass path.

For sample points indexed across the visible time window, let `t` run from 0 to 1 and let `a` be the signed sample amplitude. At the reference scale:

```text
x_screen = x + w * (0.038 + 0.924 * t)
y_screen = y + h * (0.5 - 0.298 * a)
```

Connect the points. Use a **4 px** halo followed by a **2 px** bright core. A silent signal is flat on the centerline. Use the same display gain and time window for both traces if you want users to compare the two directly. For a reproducible mockup, the supplied compositor uses the following synthetic traces:

```text
phase = 2π * (2.70 * t - 0.055)
input = sin(phase) + 0.23 * sin(2 * phase + 0.5)
output = 0.91 * tanh(2.15 * input) + 0.13 * sin(4 * phase)
```

For the live editor, capture the intended pre- and post-processing taps on the audio side. Transfer a bounded snapshot to the editor without locking the audio thread, and update from a UI timer (for example, 30–60 Hz). When displaying more samples than horizontal pixels, preserve per-column minima and maxima so narrow peaks remain visible. Triggering on a consistent input crossing is an option for stabilizing repetitive signals. The artwork leaves the time window, gain, trigger, and stereo-to-trace policy to the plugin.

## Input and output dB meters

The input meter is above the output meter. Read `meters.input` and `meters.output`; each has a `track` and a separate `valueWindow`. Use this reference geometry for the single green bar per track:

```text
amount = clamp(meterNormalized, 0, 1)
bar = (track.x + 3, track.y + 4, (track.w - 6) * amount, track.h - 8)
radius = min(3, bar.width/2, bar.height/2)
```

Draw the bar left to right in active phosphor, skipping it when the amount is zero. Clip it to the track. The preview uses input **0.82** and output **0.68**, with `0.1 dB` and `0.0 dB` as sample labels. These values are independent mock data.

When a meter overloads, its readout turns red and shows an exclamation mark. Activate that readout to clear its latch on the next audio callback.

For a live level meter, choose and document the measurement (peak or RMS), the stereo policy, and whether the label holds a peak. One approach is to measure each tap's maximum absolute sample over the update period across channels, compute `20 * log10(max(amplitude, epsilon))`, and map the resulting dBFS value linearly between a chosen display floor and ceiling. Apply fast attack and slower release or peak hold at UI update time. Format the measured level independently of the bar position, typically to one decimal place. The faceplate does not specify a calibrated range or a clip/hold indicator.

## Preset name

Use `preset.nameWindow` and `preset.maxCharacters` (**12**). The reference draws `DEFAULT` in the preview. Uppercase the display copy and truncate it to 12 characters while retaining the full name in preset state. The approximation has digits, A–Z, space, hyphen, and underscore. Unsupported glyphs use the middle stroke. Some letters share shapes, and letters such as B and D resemble lowercase forms.

The JUCE editor uses a [local seven-segment alphabet](../src/plugin/FaceplateDrawing.h) based on the upstream style. Draw active strokes at **1.5 px**. Use the following geometry for the centered fixed-pitch layout.

```text
pitch = min(0.65 * h, w / (maxCharacters + 1))
s = min(h, pitch / 0.65)
left = x + (w - (length - 1) * pitch - 0.35 * s) / 2
top = y + (h - 0.69 * s) / 2
glyphOrigin = (left + index * pitch, top)
```

Multiply each endpoint from `STROKES` by `s` and offset it by `glyphOrigin`. An empty name draws no strokes. The name window represents display text. Any rename control, menu, tooltip, or editing behavior belongs to the editor.

## Die face

Use `preset.diceWindow`, immediately right of the name display. The square glass and bezel come from the faceplate. Draw active pips as filled phosphor circles, with no outlines. The preview value is `preset.previewDiceValue = 5`.

Position pips using `(x + w * (0.5 + dx), y + h * (0.5 + dy))` and radius `0.063 * min(w, h)`. Clamp or validate the input to the integer range 1–6 before selecting a mask.

| Value | Pips |
| --- | --- |
| 1 | Center (0, 0) |
| 2 | Upper left (−0.22, −0.22), lower right (+0.22, +0.22) |
| 3 | Face 2 plus center |
| 4 | Face 2 plus upper right (+0.22, −0.22) and lower left (−0.22, +0.22) |
| 5 | Face 4 plus center |
| 6 | Face 4 plus middle left (−0.22, 0) and middle right (+0.22, 0) |

The artwork defines a visual target for the die. If the die becomes a clickable randomize control, bind its hit area and accessibility label in the editor and define what it randomizes in plugin code.

## Integration and verification

1. Load the faceplate, atlas, and placement map once. Draw within the shared logical coordinate space. Cache asset and clip data outside the paint loop.
2. Read control and preset state on the UI thread. Feed the scope and meter displays from safe snapshots. Use the UI timer to refresh live graphics rather than repainting from the audio callback.
3. Test the knobs at 0, midpoint, and maximum. Check waveform shapes at silence and on a known input, test clipped peaks, and check empty and 12-character names plus all six die values.
4. Run the project adapter tests to validate the installed 4× assets and placement metadata. Compare a 1040 × 948 editor capture with the [assembled reference](disdorktion_interface@4x.png). Compare live values against their corresponding parameters and signals.

## JUCE drawing examples

These C++ snippets are intended for the editor's paint layer. Pass in a graphics context already transformed into the logical coordinates above. Parse the placement map once and cache the resulting rectangles, pivots, and rounded clip paths. Load assets via `juce::ImageFileFormat::loadFrom` or embed them as binary resources. See the JUCE references for [Graphics](https://docs.juce.com/master/classjuce_1_1Graphics.html) and [Path](https://docs.juce.com/master/classjuce_1_1Path.html).

| Role | Web hex | JUCE ARGB literal |
| --- | --- | --- |
| Active segments, readout text, scope core, meter fill, preset lettering, die pips | `#A3EEAF` | `0xFFA3EEAF` |
| Inactive numeric segments | `#28403D` | `0xFF28403D` |
| Scope halo | `#307047DC` (RGBA) | `0xDC307047` |
| Readout, preset and meter glass | `#273236` (sampled from the raster) | `0xFF273236` |
| Scope glass between grid lines | `#263135` (sampled from the raster) | `0xFF263135` |

The last two values are useful for mockups. The final editor should show the glass and its shading from the faceplate.

```cpp
#include <JuceHeader.h>
#include <cmath>

using Rect = juce::Rectangle<float>;
const juce::Colour phosphor { 0xFFA3EEAF };
const juce::Colour dimSegment { 0xFF28403D };
const juce::Colour scopeHalo { 0xDC307047 };

juce::Path glassClip (Rect r, float radius)
{
    juce::Path clip;
    clip.addRoundedRectangle (r, radius);
    return clip;
}
```

### Numeric display

Pass a formatted numeric string and its separately formatted units to the painter. A text-only state uses an empty numeric string and puts the entire label in `suffix`. Keeping these two values separate also makes localization and special states such as negative infinity easier to handle.

```cpp
void drawReadout (juce::Graphics& g, Rect r, const juce::Path& clip,
                  juce::String number, const juce::String& suffix)
{
    juce::Graphics::ScopedSaveState state (g);
    if (! g.reduceClipRegion (clip))
        return;

    // Bits go from segment a (bit 0) to g (bit 6).
    constexpr unsigned digitMask[] = {
        0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
    };
    constexpr float segment[][4] = {
        {1.4f, 0, 5.6f, 0},      {7, 1.4f, 7, 4.6f},
        {7, 7.4f, 7, 10.6f},    {1.4f, 12, 5.6f, 12},
        {0, 7.4f, 0, 10.6f},    {0, 1.4f, 0, 4.6f},
        {1.4f, 6, 5.6f, 6}
    };

    number = number.replaceCharacter (0x2212, '-');
    float span = 0.0f;
    const auto count = number.length();
    for (int i = 0; i < count; ++i)
        span += number[i] == '.' ? 4.0f : 9.0f;

    g.setFont (11.0f);
    juce::GlyphArrangement suffixGlyphs;
    suffixGlyphs.addLineOfText (g.getCurrentFont(), suffix, 0.0f, 0.0f);
    const auto suffixWidth = std::ceil (
        suffixGlyphs.getBoundingBox (0, -1, true).getWidth());
    const auto gap = number.isNotEmpty() && suffix.isNotEmpty() ? 3.0f : 0.0f;
    auto x = r.getCentreX() - (span + gap + suffixWidth) * 0.5f;
    const auto y = r.getCentreY() - 6.0f;

    for (int character = 0; character < count; ++character)
    {
        const auto c = number[character];
        if (c == '.')
        {
            g.setColour (phosphor);
            g.fillEllipse (x, y + 10.8f, 1.5f, 1.5f);
            x += 4.0f;
            continue;
        }

        const auto mask = c >= '0' && c <= '9' ? digitMask[c - '0']
                        : c == '-' ? 0x40u : 0u;
        for (int i = 0; i < 7; ++i)
        {
            const auto& s = segment[i];
            g.setColour ((mask & (1u << i)) != 0 ? phosphor : dimSegment);
            g.drawLine (x + s[0], y + s[1], x + s[2], y + s[3], 1.2f);
        }
        x += 9.0f;
    }

    if (suffix.isNotEmpty())
    {
        g.setColour (phosphor);
        g.drawText (suffix, Rect { x + gap, r.getY(), suffixWidth, r.getHeight() },
                    juce::Justification::centredLeft, false);
    }
}
```

### Scope, meter fill, and die

Build scope paths on the UI thread from the latest snapshot. For a single sample per column, call `startNewSubPath` on the first point and `lineTo` for each subsequent point using the scope coordinates above. If you bucket multiple samples per column, add a separate vertical subpath from the minimum to the maximum. Cache the path between snapshots.

```cpp
void drawScope (juce::Graphics& g, const juce::Path& trace,
                const juce::Path& clip)
{
    juce::Graphics::ScopedSaveState state (g);
    if (! g.reduceClipRegion (clip))
        return;
    g.setColour (scopeHalo);
    g.strokePath (trace, juce::PathStrokeType (4.0f));
    g.setColour (phosphor);
    g.strokePath (trace, juce::PathStrokeType (2.0f));
}

void drawMeterBar (juce::Graphics& g, Rect track,
                   const juce::Path& clip, float normalized)
{
    const auto bar = track.reduced (3.0f, 4.0f)
                          .withWidth ((track.getWidth() - 6.0f)
                                      * juce::jlimit (0.0f, 1.0f, normalized));
    juce::Graphics::ScopedSaveState state (g);
    if (bar.isEmpty() || ! g.reduceClipRegion (clip))
        return;
    g.setColour (phosphor);
    g.fillRoundedRectangle (bar, juce::jmin (3.0f, bar.getWidth() * 0.5f,
                                              bar.getHeight() * 0.5f));
}

void drawDie (juce::Graphics& g, Rect r, const juce::Path& clip, int value)
{
    // Pip mask order is upper left, upper right, center,
    // lower left, lower right, middle left, middle right.
    constexpr float offsets[][2] = {
        {-.22f, -.22f}, {.22f, -.22f}, {0, 0}, {-.22f, .22f},
        {.22f, .22f}, {-.22f, 0}, {.22f, 0}
    };
    constexpr unsigned masks[] = { 0x04, 0x11, 0x15, 0x1B, 0x1F, 0x7B };
    if (value < 1 || value > 6)
        return;

    juce::Graphics::ScopedSaveState state (g);
    if (! g.reduceClipRegion (clip))
        return;
    g.setColour (phosphor);
    const auto radius = 0.063f * juce::jmin (r.getWidth(), r.getHeight());
    for (int i = 0; i < 7; ++i)
        if ((masks[value - 1] & (1u << i)) != 0)
            g.fillEllipse (r.getCentreX() + offsets[i][0] * r.getWidth() - radius,
                           r.getCentreY() + offsets[i][1] * r.getHeight() - radius,
                           2.0f * radius, 2.0f * radius);
}
```

Call `drawReadout` with the meter's separate value window after painting its bar. For the preset name, draw the locally mapped segments at 1.5 px in phosphor inside the preset glass clip.

### Knob atlas and parameter binding

Compute the atlas source coordinates using the frame formula above. For fractional pivots, draw a clipped image into a floating-point destination rectangle. For a busy editor, cache the 121 image views once after loading the atlas.

```cpp
const auto frame = juce::jlimit (0, 120,
    juce::roundToInt (120.0f * juce::jlimit (0.0f, 1.0f, normalized)));
const auto sprite = atlas.getClippedImage ({ (frame % 11) * tileSize,
    (frame / 11) * tileSize, tileSize, tileSize });
g.setOpacity (1.0f);
g.drawImage (sprite, Rect { pivot.x - 48.0f, pivot.y - 48.0f, 96.0f, 96.0f });
```

If the editor uses `AudioProcessorValueTreeState`, a rotary `Slider` can supply the hit area, drag behavior, and parameter attachment. A custom look-and-feel can paint the selected frame. Use the same parameter state to refresh the visible value label. Scope snapshots and meter state can update on the UI timer independently of parameter changes.
