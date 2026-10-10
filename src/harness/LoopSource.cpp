#include "LoopSource.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace disdorktion::harness
{
namespace
{
class CancellableStream final : public juce::InputStream
{
public:
    CancellableStream(juce::InputStream& source, const std::atomic<bool>* flag) : input(source), cancel(flag) {}
    juce::int64 getTotalLength() override { return input.getTotalLength(); }
    juce::int64 getPosition() override { return input.getPosition(); }
    bool setPosition(juce::int64 position) override { return input.setPosition(position); }
    bool isExhausted() override { return cancelled() || input.isExhausted(); }
    int read(void* data, int bytes) override { return cancelled() ? 0 : input.read(data, std::min(bytes, 65536)); }
private:
    bool cancelled() const { return cancel != nullptr && cancel->load(std::memory_order_relaxed); }
    juce::InputStream& input;
    const std::atomic<bool>* cancel;
};
}

bool LoopSource::load(const juce::File& file, double targetRate, unsigned channels,
                      juce::String& error, const std::atomic<bool>* cancel, std::uint64_t externalResidentBytes)
{
    error.clear();
    const auto cancelled = [&] { return cancel != nullptr && cancel->load(std::memory_order_relaxed); };
    const auto fail = [&](const juce::String& reason) { error = reason; return false; };
    if (cancelled()) return fail("File loading cancelled.");
    if (!std::isfinite(targetRate) || targetRate < 8000.0 || targetRate > 384000.0 || (channels != 1 && channels != 2))
        return fail("Invalid loop destination sample rate or channel count.");
    if (!file.existsAsFile()) return fail("Audio file does not exist.");
    if (!file.hasFileExtension("wav;aif;aiff")) return fail("Only WAV and AIFF audio files are supported.");
    auto stream = file.createInputStream();
    if (stream == nullptr || stream->getStatus().failed()) return fail("Cannot open audio file.");
    const auto originalSize = stream->getTotalLength();
    const auto originalModified = file.getLastModificationTime();
    CancellableStream hashStream(*stream, cancel);
    const auto candidateHash = juce::SHA256(hashStream).toHexString();
    if (cancelled()) return fail("File loading cancelled.");
    if (stream->getStatus().failed() || stream->getPosition() != originalSize)
        return fail("Failed to hash the complete audio file.");
    if (!stream->setPosition(0)) return fail("Cannot rewind audio file.");
    juce::AudioFormatManager manager;
    manager.registerFormat(new juce::WavAudioFormat(), true);
    manager.registerFormat(new juce::AiffAudioFormat(), false);
    std::unique_ptr<juce::AudioFormatReader> reader(manager.createReaderFor(std::move(stream)));
    if (reader == nullptr) return fail("Audio file is unsupported or corrupt.");
    if (reader->lengthInSamples <= 0 || (reader->numChannels != 1 && reader->numChannels != 2)
        || !std::isfinite(reader->sampleRate) || reader->sampleRate <= 0.0)
        return fail("Audio file must contain nonempty finite-rate mono or stereo PCM.");
    const auto inputLength = static_cast<std::uint64_t>(reader->lengthInSamples);
    const auto convertedLength = std::round(static_cast<long double>(inputLength) * targetRate / reader->sampleRate);
    if (!std::isfinite(convertedLength) || convertedLength > std::numeric_limits<int>::max()
        || inputLength > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        return fail("Audio file sample count is too large.");
    const auto outputLength = static_cast<std::uint64_t>(std::max(1.0L, convertedLength));
    // Counts are now at most INT_MAX, so this arithmetic cannot overflow uint64.
    const auto decodedBytes = 4ULL * inputLength * reader->numChannels;
    const auto convertedBytes = 4ULL * outputLength * channels;
    // A fresh worker loader must also account for the controller's previous
    // resident loop. Subtract each term so arbitrary external sizes cannot wrap.
    auto availableBytes = memoryLimitBytes;
    for (const auto bytes : {externalResidentBytes, residentMemoryBytes(),
                             static_cast<std::uint64_t>(decodedBytes), static_cast<std::uint64_t>(convertedBytes)})
    {
        if (bytes > availableBytes) return fail("Resident, decoded and converted PCM exceeds the 256 MiB memory limit.");
        availableBytes -= bytes;
    }
    juce::AudioBuffer<float> decoded(static_cast<int>(reader->numChannels), static_cast<int>(inputLength));
    constexpr int chunkSize = 16384;
    for (std::uint64_t offset = 0; offset < inputLength; offset += chunkSize)
    {
        if (cancelled()) return fail("File loading cancelled.");
        const auto count = static_cast<int>(std::min<std::uint64_t>(chunkSize, inputLength - offset));
        std::array<float*, 2> pointers {};
        for (unsigned c = 0; c < reader->numChannels; ++c) pointers[c] = decoded.getWritePointer(static_cast<int>(c), static_cast<int>(offset));
        if (!reader->read(pointers.data(), static_cast<int>(reader->numChannels), static_cast<juce::int64>(offset), count))
            return fail("Failed to decode audio file PCM.");
        for (unsigned c = 0; c < reader->numChannels; ++c)
            for (int n = 0; n < count; ++n)
                if (!std::isfinite(pointers[c][n])) return fail("Audio file contains nonfinite PCM.");
    }
    juce::AudioBuffer<float> converted(static_cast<int>(channels), static_cast<int>(outputLength));
    for (std::uint64_t n = 0; n < outputLength; ++n)
    {
        if ((n % chunkSize) == 0 && cancelled()) return fail("File loading cancelled.");
        // Shared live/offline deterministic linear interpolation. Hold the last
        // sample at the endpoint; this is not an anti-aliasing resampler.
        const auto sourcePosition = std::min(static_cast<double>(inputLength - 1), static_cast<double>(n) * reader->sampleRate / targetRate);
        const auto left = static_cast<int>(sourcePosition);
        const auto right = std::min(left + 1, static_cast<int>(inputLength - 1));
        const auto fraction = sourcePosition - left;
        const auto interpolate = [&](unsigned c)
        {
            const auto a = static_cast<double>(decoded.getSample(static_cast<int>(c), left));
            const auto b = static_cast<double>(decoded.getSample(static_cast<int>(c), right));
            return a + (b - a) * fraction;
        };
        for (unsigned c = 0; c < channels; ++c)
        {
            const auto sample = reader->numChannels == 1 ? interpolate(0)
                              : channels == 1 ? 0.5 * interpolate(0) + 0.5 * interpolate(1) : interpolate(c);
            const auto value = static_cast<float>(sample);
            if (!std::isfinite(value)) return fail("Converted audio contains nonfinite PCM.");
            converted.setSample(static_cast<int>(c), static_cast<int>(n), value);
        }
    }
    if (cancelled()) return fail("File loading cancelled.");
    if (file.getSize() != originalSize || file.getLastModificationTime() != originalModified)
        return fail("Audio file changed during loading.");
    // Commit only a complete source; every failure leaves the previous loop intact.
    pcm = std::move(converted);
    hash = candidateHash;
    filePath = file.getFullPathName();
    sourceRate = reader->sampleRate;
    sourceChannels = reader->numChannels;
    reset();
    return true;
}

void LoopSource::process(juce::dsp::AudioBlock<float> block) noexcept
{
    const auto length = static_cast<std::size_t>(pcm.getNumSamples());
    if (length == 0 || block.getNumChannels() != static_cast<std::size_t>(pcm.getNumChannels())) { block.clear(); return; }
    std::size_t offset = 0;
    while (offset < block.getNumSamples())
    {
        const auto count = std::min(length - position, block.getNumSamples() - offset);
        for (std::size_t c = 0; c < block.getNumChannels(); ++c)
            std::copy_n(pcm.getReadPointer(static_cast<int>(c)) + position, count, block.getChannelPointer(c) + offset);
        offset += count;
        position += count;
        if (position == length) position = 0;
    }
}
}
