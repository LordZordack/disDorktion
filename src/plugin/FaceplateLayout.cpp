#include "FaceplateLayout.h"
#include "DisDorktionFaceplateData.h"
#include "dsp/FullChainControls.h"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace disdorktion
{
namespace
{
struct Binding { const char* placement; const char* parameter; };
constexpr std::array bindings {
    Binding{"receiver_r1c1", "captureRateHz"}, Binding{"receiver_r1c2", "crush"},
    Binding{"receiver_r1c3", "bitDepth"}, Binding{"receiver_r2c1", "shape"},
    Binding{"receiver_r2c2", "jitter"}, Binding{"receiver_r2c3", "crusherMix"},
    Binding{"waveshaper_r1c1", "tanhDriveDb"}, Binding{"waveshaper_r1c2", "folderDriveDb"},
    Binding{"waveshaper_r1c3", "folderThreshold"}, Binding{"waveshaper_r2c1", "driver"},
    Binding{"waveshaper_r2c2", "waveshaperTrimDb"}, Binding{"waveshaper_r2c3", "waveshaperMix"},
    Binding{"refiner_r1c1", "combDelayMs"}, Binding{"refiner_r1c2", "dispersionFrequencyHz"},
    Binding{"refiner_r1c3", "combDampingHz"}, Binding{"refiner_r2c1", "combFeedforward"},
    Binding{"refiner_r2c2", "dispersionMix"}, Binding{"refiner_r2c3", "toneDb"},
    Binding{"refiner_r3c1", "combFeedback"}, Binding{"refiner_r3c2", "lpCutoffHz"},
    Binding{"refiner_r3c3", "lpResponse"}, Binding{"refiner_r4c1", "combMix"},
    Binding{"refiner_r4c2", "hpCutoffHz"}, Binding{"refiner_r4c3", "hpResponse"},
    Binding{"global_r1c1", "inputGainDb"}, Binding{"global_r1c2", "globalMix"},
    Binding{"global_r1c3", "outputGainDb"}
};

static_assert(bindings.size() == fullChainControls.size());
static_assert([] {
    for (const auto& control : fullChainControls)
        if (std::count_if(bindings.begin(), bindings.end(), [&](const auto& binding) {
                return std::string_view(binding.parameter) == control.id;
            }) != 1)
            return false;
    return true;
}());

bool isNumber(const juce::var& value)
{
    return (value.isInt() || value.isInt64() || value.isDouble())
        && std::isfinite(static_cast<double>(value));
}

template <std::size_t size>
std::optional<std::array<float, size>> coordinates(const juce::var& value)
{
    const auto* array = value.getArray();
    if (array == nullptr || array->size() != static_cast<int>(size))
        return std::nullopt;
    std::array<float, size> result;
    for (std::size_t index = 0; index < size; ++index)
    {
        const auto& entry = (*array)[static_cast<int>(index)];
        if (!isNumber(entry))
            return std::nullopt;
        result[index] = static_cast<float>(entry);
    }
    return result;
}

const juce::Rectangle<float> canvas(0, 0, FaceplateLayout::width, FaceplateLayout::height);

bool readWindow(const juce::var& value, FaceplateLayout::Window& result)
{
    const auto rect = coordinates<4>(value.getProperty("rect", {}));
    const auto radius = value.getProperty("cornerRadius", {});
    if (!rect || !isNumber(radius))
        return false;

    const auto& r = *rect;
    result = {{r[0], r[1], r[2], r[3]}, static_cast<float>(radius)};
    return !result.bounds.isEmpty() && canvas.contains(result.bounds)
        && result.radius >= 0 && 2 * result.radius <= juce::jmin(r[2], r[3]);
}

bool readMeter(const juce::var& value, FaceplateLayout::Meter& result)
{
    return readWindow(value.getProperty("track", {}), result.track)
        && readWindow(value.getProperty("valueWindow", {}), result.value);
}
}

juce::Path FaceplateLayout::Window::clipAt(juce::Point<int> origin) const
{
    juce::Path path;
    path.addRoundedRectangle(bounds - origin.toFloat(), radius);
    return path;
}

juce::Rectangle<float> FaceplateLayout::Control::knobBounds() const
{
    return juce::Rectangle<float>(logicalKnobSize, logicalKnobSize).withCentre(pivot);
}

std::optional<FaceplateLayout> FaceplateLayout::parse(const juce::String& json)
{
    const auto root = juce::JSON::parse(json);
    const auto logicalSize = coordinates<2>(root.getProperty("logicalSize", {}));
    const auto atlas = root.getProperty("knobAtlas", {});
    const auto tile = coordinates<2>(atlas.getProperty("tileSize", {}));
    const auto pivot = coordinates<2>(atlas.getProperty("pivot", {}));
    const auto controlData = root.getProperty("controls", {});
    const auto* entries = controlData.getArray();
    if (logicalSize != std::array<float, 2>{width, height}
        || root.getProperty("coordinateOrigin", {}).toString() != "top-left"
        || static_cast<int>(root.getProperty("imageScale", {})) != 2
        || tile != std::array<float, 2>{192, 192} || pivot != std::array<float, 2>{96, 96}
        || static_cast<int>(atlas.getProperty("columns", {})) != columns
        || static_cast<int>(atlas.getProperty("rows", {})) != columns
        || static_cast<int>(atlas.getProperty("frames", {})) != frames
        || atlas.getProperty("order", {}).toString() != "row-major"
        || entries == nullptr || entries->size() != static_cast<int>(bindings.size()))
        return std::nullopt;

    FaceplateLayout result;
    for (const auto& entry : *entries)
    {
        const auto id = entry.getProperty("id", {}).toString();
        const auto binding = std::find_if(bindings.begin(), bindings.end(),
            [&](const auto& candidate) { return id == candidate.placement; });
        const auto position = coordinates<2>(entry.getProperty("pivot", {}));
        if (binding == bindings.end() || !position)
            return std::nullopt;

        auto& control = result.controls[static_cast<std::size_t>(binding - bindings.begin())];
        if (control.parameter != nullptr)
            return std::nullopt;
        control.parameter = binding->parameter;
        control.pivot = {(*position)[0], (*position)[1]};
        if (!canvas.contains(control.knobBounds())
            || !readWindow(entry.getProperty("valueWindow", {}), control.value))
            return std::nullopt;
    }

    const auto scopes = root.getProperty("waveforms", {});
    const auto meters = root.getProperty("meters", {});
    const auto preset = root.getProperty("preset", {});
    if (!readWindow(scopes.getProperty("input", {}), result.inputScope)
        || !readWindow(scopes.getProperty("output", {}), result.outputScope)
        || !readMeter(meters.getProperty("input", {}), result.inputMeter)
        || !readMeter(meters.getProperty("output", {}), result.outputMeter)
        || !readWindow(preset.getProperty("nameWindow", {}), result.preset)
        || !readWindow(preset.getProperty("diceWindow", {}), result.die)
        || static_cast<int>(preset.getProperty("maxCharacters", {})) != 12)
        return std::nullopt;

    return result;
}

const FaceplateAssets& FaceplateAssets::embedded()
{
    static const auto assets = [] {
        using namespace DisDorktionFaceplateData;
        auto layout = FaceplateLayout::parse(juce::String::fromUTF8(layout_json, layout_jsonSize));
        if (!layout)
            return FaceplateAssets{};
        auto plate = juce::ImageFileFormat::loadFrom(faceplate4x_png, faceplate4x_pngSize);
        auto atlas = juce::ImageFileFormat::loadFrom(knob_atlas4x_png, knob_atlas4x_pngSize);
        constexpr auto atlasSize = FaceplateLayout::atlasTileSize * FaceplateLayout::columns;
        if (plate.getWidth() != 4 * FaceplateLayout::width || plate.getHeight() != 4 * FaceplateLayout::height
            || atlas.getWidth() != atlasSize || atlas.getHeight() != atlasSize)
            return FaceplateAssets{};

        FaceplateAssets result;
        result.layout = std::move(layout);
        result.plate = std::move(plate);
        for (int frame = 0; frame < FaceplateLayout::frames; ++frame)
        {
            constexpr auto tile = FaceplateLayout::atlasTileSize;
            const auto column = frame % FaceplateLayout::columns;
            const auto row = frame / FaceplateLayout::columns;
            result.knobs[static_cast<std::size_t>(frame)] = atlas.getClippedImage({column * tile, row * tile, tile, tile});
        }
        return result;
    }();
    return assets;
}
}
