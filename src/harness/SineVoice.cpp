#include "SineVoice.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>

namespace disdorktion::harness
{
bool SineVoice::prepare(double sampleRate)
{
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0) return false;
    rate = sampleRate;
    rampLength = static_cast<unsigned>(std::max(1.0, std::round(rate * 0.005)));
    reset();
    return true;
}
void SineVoice::reset() noexcept
{
    count = 0; phase = 0.0; increment = 0.0; level = target = step = 0.0f; remaining = 0;
}
void SineVoice::setLevel(float newLevel) noexcept
{
    target = newLevel;
    remaining = rampLength;
    step = (target - level) / static_cast<float>(rampLength);
}
void SineVoice::selectNote() noexcept
{
    if (count == 0) { setLevel(0.0f); return; }
    const auto note = notes[count - 1];
    const auto frequency = 440.0 * std::exp2((static_cast<double>(note) - 69.0) / 12.0);
    increment = juce::MathConstants<double>::twoPi * frequency / rate;
    setLevel(frequency < rate * 0.5 ? 0.25f * velocities[static_cast<std::size_t>(note)] : 0.0f);
}
void SineVoice::noteOn(int note, float velocity) noexcept
{
    if (rate == 0.0 || note < 0 || note > 127 || !std::isfinite(velocity)) return;
    if (velocity <= 0.0f) { noteOff(note); return; }
    // Remove any older occurrence without changing the current envelope twice.
    for (std::size_t n = 0; n < count; ++n)
        if (notes[n] == note)
        {
            for (auto i = n + 1; i < count; ++i) notes[i - 1] = notes[i];
            --count; break;
        }
    notes[count++] = note;
    velocities[static_cast<std::size_t>(note)] = std::min(velocity, 1.0f);
    selectNote();
}
void SineVoice::noteOff(int note) noexcept
{
    if (note < 0 || note > 127) return;
    for (std::size_t n = 0; n < count; ++n)
        if (notes[n] == note)
        {
            const auto wasCurrent = n + 1 == count;
            for (auto i = n + 1; i < count; ++i) notes[i - 1] = notes[i];
            --count;
            if (wasCurrent) selectNote();
            break;
        }
}
void SineVoice::allNotesOff() noexcept { count = 0; setLevel(0.0f); }
float SineVoice::nextSample() noexcept
{
    if (rate == 0.0) return 0.0f;
    if (remaining > 0) { level += step; if (--remaining == 0) level = target; }
    const auto sample = level * static_cast<float>(std::sin(phase));
    phase += increment;
    if (phase >= juce::MathConstants<double>::twoPi)
        phase = std::fmod(phase, juce::MathConstants<double>::twoPi);
    return sample;
}
}
