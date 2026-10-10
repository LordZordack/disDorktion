#pragma once
#include <array>
#include <cstddef>

namespace disdorktion::harness
{
// Monophonic, last pressed note wins. 5ms attack/release; velocity scales a
// peak amplitude of 0.25. All event methods run on the audio consumer thread.
class SineVoice
{
public:
    bool prepare(double sampleRate);
    void reset() noexcept;
    void noteOn(int note, float velocity) noexcept;
    void noteOff(int note) noexcept;
    void allNotesOff() noexcept;
    float nextSample() noexcept;
private:
    void selectNote() noexcept;
    void setLevel(float) noexcept;
    std::array<int, 128> notes {};
    std::array<float, 128> velocities {};
    std::size_t count = 0;
    double rate = 0.0, phase = 0.0, increment = 0.0;
    float level = 0.0f, target = 0.0f, step = 0.0f;
    unsigned rampLength = 1, remaining = 0;
};
}
