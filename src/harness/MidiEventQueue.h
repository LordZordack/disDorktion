#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>

namespace disdorktion::harness
{
struct MidiEvent
{
    enum class Type { noteOn, noteOff, allOff };
    Type type = Type::allOff;
    int note = 0;
    float velocity = 0.0f;
    // Seconds in the juce::Time::getMillisecondCounterHiRes() monotonic domain.
    double timestampSeconds = 0.0;
};

// Bounded sequence-number ring based on Dmitry Vyukov's bounded MPMC queue,
// specialised to one consumer. Release/acquire publishes the non-atomic event.
// Producers reserve FIFO positions; a delayed producer may temporarily hide
// later events, so pop returns immediately instead of spinning. Producer CAS
// retries are capped. Contention/full failure requests emergency all-off.
// On consumeOverflow(): discard up to capacity queued events, then allNotesOff.
// Epoch tags also discard pre-overflow reservations published by a delayed
// producer after the drain; no producer writes audio voice state.
// Controller must clamp late timestamps and preserve queue order within a block.
class MidiEventQueue
{
public:
    static constexpr std::size_t capacity = 256;
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    MidiEventQueue() noexcept
    {
        for (std::size_t n = 0; n < capacity; ++n) cells[n].sequence.store(n, std::memory_order_relaxed);
    }
    bool push(const MidiEvent& event) noexcept
    {
        const auto eventEpoch = epoch.load(std::memory_order_acquire);
        auto position = enqueue.load(std::memory_order_relaxed);
        for (unsigned attempt = 0; attempt < 32; ++attempt)
        {
            auto& cell = cells[position % capacity];
            const auto sequence = cell.sequence.load(std::memory_order_acquire);
            if (sequence == position)
            {
                if (enqueue.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
                {
                    cell.event = event;
                    cell.epoch = eventEpoch;
                    cell.sequence.store(position + 1, std::memory_order_release);
                    return true;
                }
            }
            else if (sequence < position) break;
            else position = enqueue.load(std::memory_order_relaxed);
        }
        epoch.fetch_add(1, std::memory_order_acq_rel);
        overflow.store(true, std::memory_order_release);
        return false;
    }
    bool pop(MidiEvent& event) noexcept
    {
        for (std::size_t skipped = 0; skipped < capacity; ++skipped)
        {
            auto& cell = cells[dequeue % capacity];
            if (cell.sequence.load(std::memory_order_acquire) != dequeue + 1) return false;
            const auto current = cell.epoch == epoch.load(std::memory_order_acquire);
            if (current) event = cell.event;
            cell.sequence.store(dequeue + capacity, std::memory_order_release);
            ++dequeue;
            if (current) return true;
        }
        return false;
    }
    bool consumeOverflow() noexcept { return overflow.exchange(false, std::memory_order_acq_rel); }
    // Focus/source changes invalidate even producer reservations not yet published.
    // The controller also requests all-notes-off on its audio consumer thread.
    void invalidate() noexcept { epoch.fetch_add(1, std::memory_order_acq_rel); }
private:
    struct Cell { std::atomic<std::uint64_t> sequence { 0 }; MidiEvent event; std::uint64_t epoch = 0; };
    std::array<Cell, capacity> cells;
    alignas(64) std::atomic<std::uint64_t> enqueue { 0 };
    alignas(64) std::uint64_t dequeue = 0;
    std::atomic<bool> overflow { false };
    std::atomic<std::uint64_t> epoch { 0 };
};
}
