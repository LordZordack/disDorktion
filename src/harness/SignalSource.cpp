#include "SignalSource.h"
#include <cmath>

namespace disdorktion::harness
{
bool SignalSource::validate(const SourceSettings& s, double sampleRate, juce::String& error)
{
    error.clear();
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0)
        error = "Sample rate must be in [8000, 384000].";
    else if (!std::isfinite(s.frequency) || !std::isfinite(s.frequency2)
             || !std::isfinite(s.amplitude) || !std::isfinite(s.phase))
        error = "Source settings must be finite.";
    else if (s.amplitude < 0.0 || s.amplitude > 1.0)
        error = "Source peak amplitude must be in [0, 1].";
    else if (s.durationSamples == 0 || s.durationSamples > 9007199254740991ULL)
        error = "Source sample count must be positive and exactly representable.";
    else if (!(s.frequency > 0.0 && s.frequency < sampleRate * 0.5)
             || !(s.frequency2 > 0.0 && s.frequency2 < sampleRate * 0.5))
        error = "Source frequencies must be above zero and below Nyquist.";
    else if (s.kind == SourceKind::sweep && (s.frequency2 <= s.frequency || s.durationSamples < 2))
        error = "Sweep requires increasing endpoints and at least two samples.";
    else if (s.kind != SourceKind::impulse && s.kind != SourceKind::sine
             && s.kind != SourceKind::twoTone && s.kind != SourceKind::sweep && s.kind != SourceKind::noise)
        error = "This source kind cannot be generated.";
    return error.isEmpty();
}

bool SignalSource::prepare(const SourceSettings& s, double sampleRate, unsigned channels)
{
    juce::String error;
    if ((channels != 1 && channels != 2) || !validate(s, sampleRate, error)) return false;
    settings = s;
    rate = sampleRate;
    channelCount = channels;
    const auto relativeDifference = (s.frequency2 - s.frequency) / s.frequency;
    sweepLog = std::isfinite(relativeDifference) ? std::log1p(relativeDifference)
                                               : std::log(s.frequency2) - std::log(s.frequency);
    reset();
    return true;
}

void SignalSource::reset() noexcept
{
    position = 0;
    randomState = settings.seed;
}

void SignalSource::process(juce::dsp::AudioBlock<float> block) noexcept
{
    if (channelCount == 0 || block.getNumChannels() != channelCount) { block.clear(); return; }
    const auto tau = juce::MathConstants<double>::twoPi;
    for (std::size_t n = 0; n < block.getNumSamples(); ++n)
    {
        double sample = 0.0;
        if (position < settings.durationSamples)
        {
            const auto index = static_cast<double>(position);
            switch (settings.kind)
            {
                case SourceKind::impulse: sample = position == 0 ? settings.amplitude : 0.0; break;
                case SourceKind::sine:
                    sample = settings.amplitude * std::sin(settings.phase + tau * settings.frequency * index / rate); break;
                case SourceKind::twoTone:
                    sample = 0.5 * settings.amplitude * (std::sin(settings.phase + tau * settings.frequency * index / rate)
                             + std::sin(settings.phase + tau * settings.frequency2 * index / rate)); break;
                case SourceKind::sweep:
                {
                    // Continuous logarithmic frequency law reaches f2 at sample N-1.
                    const auto span = static_cast<double>(settings.durationSamples - 1);
                    const auto exponent = sweepLog * index / span;
                    const auto frequencyDifference = sweepLog < 1.0
                        ? settings.frequency * std::expm1(exponent)
                        : std::exp(std::log(settings.frequency) + exponent) - settings.frequency;
                    const auto cycles = frequencyDifference * span / rate / sweepLog;
                    sample = settings.amplitude * std::sin(settings.phase + tau * cycles);
                    break;
                }
                case SourceKind::noise:
                    // Numerical Recipes LCG modulo 2^32; upper 24 bits map to [-1,1).
                    // Zero is a valid seed. Conversion uses exactly representable integers.
                    randomState = randomState * 1664525u + 1013904223u;
                    sample = settings.amplitude * (static_cast<double>(randomState >> 8) / 8388608.0 - 1.0); break;
                default: break;
            }
            ++position;
        }
        for (std::size_t c = 0; c < block.getNumChannels(); ++c)
            block.getChannelPointer(c)[n] = static_cast<float>(sample);
    }
}
}
