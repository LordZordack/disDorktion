#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace disdorktion
{
// One audio/lifecycle producer and one message-thread consumer. Lifecycle calls
// are serialized with processing by the processor's callback-lock contract.
class WaveformHistory
{
public:
    struct Frame
    {
        float minimum = 0, maximum = 0;
        std::uint64_t sequence = 0, epoch = 0;
    };
    static constexpr std::size_t capacity = 512;

    void configure(double sampleRate) noexcept
    {
        samplesPerFrame = std::isfinite(sampleRate) && sampleRate >= 8000 && sampleRate <= 384000
            ? static_cast<unsigned>(std::max(1.0, std::round(sampleRate * 0.001))) : 48;
        resetEpoch();
    }

    void resetEpoch() noexcept
    {
        samplesAccumulated = 0;
        minimum = maximum = 0;
        ++producerEpoch;
        publishedEpoch.store(producerEpoch, std::memory_order_release);
        // Never reset the consumer cursor or overwrite its owned slots.
    }

    std::uint64_t epoch() const noexcept { return publishedEpoch.load(std::memory_order_acquire); }

    void push(const float* const* channels, int channelCount, int sampleCount) noexcept
    {
        for (int n = 0; n < sampleCount; ++n)
        {
            for (int c = 0; c < std::min(channelCount, 2); ++c)
            {
                const float value = std::isfinite(channels[c][n]) ? channels[c][n] : 0.0f;
                if (samplesAccumulated == 0 && c == 0) minimum = maximum = value;
                else { minimum = std::min(minimum, value); maximum = std::max(maximum, value); }
            }
            if (++samplesAccumulated == samplesPerFrame)
            {
                const Frame frame {minimum, maximum, nextSequence++, producerEpoch};
                const auto write = writeCursor.load(std::memory_order_relaxed);
                if (write - readCursor.load(std::memory_order_acquire) < capacity)
                {
                    frames[write % capacity] = frame;
                    writeCursor.store(write + 1, std::memory_order_release);
                }
                samplesAccumulated = 0;
            }
        }
    }

    // Consumer only. Snapshotting the producer cursor bounds work to capacity;
    // slots stay consumer-owned until the final release, including skipped slots.
    std::size_t drain(Frame* destination, std::size_t destinationCapacity) noexcept
    {
        const auto currentEpoch = epoch();
        auto read = readCursor.load(std::memory_order_relaxed);
        const auto write = writeCursor.load(std::memory_order_acquire);
        const auto available = write - read;
        const auto keep = std::min<std::size_t>(destination == nullptr ? 0 : destinationCapacity, capacity);
        if (available > keep) read += available - keep;
        std::size_t count = 0;
        for (; read != write; ++read)
        {
            const auto frame = frames[read % capacity];
            if (frame.epoch == currentEpoch) destination[count++] = frame;
        }
        readCursor.store(write, std::memory_order_release);
        return epoch() == currentEpoch ? count : 0;
    }

    void discardPending() noexcept
    {
        readCursor.store(writeCursor.load(std::memory_order_acquire), std::memory_order_release);
    }

private:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "Waveform counters must be lock free");
    std::array<Frame, capacity> frames {};
    std::atomic<std::uint64_t> writeCursor {0}, readCursor {0}, publishedEpoch {0};
    std::uint64_t producerEpoch = 0, nextSequence = 0;
    unsigned samplesPerFrame = 48, samplesAccumulated = 0;
    float minimum = 0, maximum = 0;
};
}
