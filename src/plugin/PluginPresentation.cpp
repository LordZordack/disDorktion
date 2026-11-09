#include "PluginProcessor.h"
#include "FactoryPresets.h"
#include <algorithm>
#include <string_view>

namespace disdorktion
{
namespace
{
float hostValue(const FullChainControl& control, float value)
{
    if (std::string_view(control.id) == "bitDepth")
        for (std::size_t i = 0; i < bitCrusherDepths.size(); ++i)
            if (value == static_cast<float>(bitCrusherDepths[i])) return static_cast<float>(i);
    return value;
}
}

bool PluginProcessor::loadFactoryPreset(int index)
{
    if (index < 0 || index >= static_cast<int>(factoryPresets.size())) return false;
    const auto& preset = factoryPresets[static_cast<std::size_t>(index)];
    if (!isValidFullChainSettings(preset.settings)) return false;
    const auto gesture = [this](const char* id, float value)
    {
        auto* parameter = parameters.getParameter(id);
        parameter->beginChangeGesture();
        publishControl(id, value);
        parameter->endChangeGesture();
    };
    for (const auto& control : fullChainControls)
    {
        gesture(control.id, getFullChainControl(preset.settings, control.id));
    }
    if (index == 0) { readMeters(); clearMeterOverloads(); }
    return true;
}

int PluginProcessor::matchingFactoryPreset() const
{
    const auto matches = [this](const char* id, float plain)
    {
        const auto* parameter = parameters.getParameter(id);
        return std::abs(parameter->getValue() - parameter->convertTo0to1(plain)) <= 2.0e-6f;
    };
    for (std::size_t preset = 0; preset < factoryPresets.size(); ++preset)
    {
        bool match = true;
        for (const auto& control : fullChainControls)
        {
            const auto value = getFullChainControl(factoryPresets[preset].settings, control.id);
            if (!matches(control.id, hostValue(control, value))) { match = false; break; }
        }
        if (match) return static_cast<int>(preset);
    }
    return -1;
}

PluginMeters PluginProcessor::readMeters() noexcept
{
    return {inputMeter.exchange(0, std::memory_order_relaxed),
            outputMeter.exchange(0, std::memory_order_relaxed),
            inputClip.load(std::memory_order_relaxed), outputClip.load(std::memory_order_relaxed)};
}

void PluginProcessor::resetMeters() noexcept
{
    inputMeter.store(0, std::memory_order_relaxed);
    outputMeter.store(0, std::memory_order_relaxed);
    inputClip.store(false, std::memory_order_relaxed);
    outputClip.store(false, std::memory_order_relaxed);
    clearOverloads.store(false, std::memory_order_relaxed);
}

void PluginProcessor::accumulateMeters(float input, float output) noexcept
{
    // One producer (audio) and one consumer (editor exchange). Load/store max
    // cannot lose a new peak: a concurrent exchange may repeat the previous
    // peak for one callback, which is harmless to display decay. No CAS spin.
    inputMeter.store(std::max(input, inputMeter.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    outputMeter.store(std::max(output, outputMeter.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    if (input > 1) inputClip.store(true, std::memory_order_relaxed);
    if (output > 1) outputClip.store(true, std::memory_order_relaxed);
}

}
