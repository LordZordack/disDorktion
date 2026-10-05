#pragma once

#include <cstddef>
#include <cstdint>

namespace disdorktion::test
{
struct AllocationCounts
{
    std::uint64_t allocations = 0;
    std::uint64_t deallocations = 0;
};

// Counts only calls made by the current thread while the scope is active.
class ScopedAllocationTracking
{
public:
    ScopedAllocationTracking() noexcept;
    ~ScopedAllocationTracking() noexcept;

    ScopedAllocationTracking(const ScopedAllocationTracking&) = delete;
    ScopedAllocationTracking& operator=(const ScopedAllocationTracking&) = delete;

private:
    bool previousState;
};

AllocationCounts getAllocationCounts() noexcept;
}
