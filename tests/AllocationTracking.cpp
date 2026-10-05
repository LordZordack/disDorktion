#include "AllocationTracking.h"

#include <cstdlib>
#include <new>

#if defined(_WIN32)
 #include <malloc.h>
#endif

namespace
{
thread_local bool trackingEnabled = false;
thread_local disdorktion::test::AllocationCounts counts {};

void recordAllocation(void* pointer) noexcept
{
    if (trackingEnabled && pointer != nullptr)
        ++counts.allocations;
}

void recordDeallocation(void* pointer) noexcept
{
    if (trackingEnabled && pointer != nullptr)
        ++counts.deallocations;
}

void* allocate(std::size_t size)
{
    if (auto* pointer = std::malloc(size == 0 ? 1 : size))
    {
        recordAllocation(pointer);
        return pointer;
    }
    throw std::bad_alloc {};
}

void* allocateAligned(std::size_t size, std::size_t alignment)
{
    const auto requestedSize = size == 0 ? alignment : size;
#if defined(_WIN32)
    auto* pointer = _aligned_malloc(requestedSize, alignment);
#else
    const auto roundedSize = ((requestedSize + alignment - 1) / alignment) * alignment;
    auto* pointer = std::aligned_alloc(alignment, roundedSize);
#endif
    if (pointer != nullptr)
    {
        recordAllocation(pointer);
        return pointer;
    }
    throw std::bad_alloc {};
}

void freeAligned(void* pointer) noexcept
{
    recordDeallocation(pointer);
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}
}

namespace disdorktion::test
{
ScopedAllocationTracking::ScopedAllocationTracking() noexcept : previousState(trackingEnabled)
{
    if (!previousState)
        counts = {};
    trackingEnabled = true;
}

ScopedAllocationTracking::~ScopedAllocationTracking() noexcept
{
    trackingEnabled = previousState;
}

AllocationCounts getAllocationCounts() noexcept
{
    return counts;
}
}

void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* pointer) noexcept { recordDeallocation(pointer); std::free(pointer); }
void operator delete[](void* pointer) noexcept { recordDeallocation(pointer); std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { recordDeallocation(pointer); std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { recordDeallocation(pointer); std::free(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { recordDeallocation(pointer); std::free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { recordDeallocation(pointer); std::free(pointer); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate(size); } catch (...) { return nullptr; }
}

void* operator new(std::size_t size, std::align_val_t alignment) { return allocateAligned(size, static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return allocateAligned(size, static_cast<std::size_t>(alignment)); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try { return allocateAligned(size, static_cast<std::size_t>(alignment)); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try { return allocateAligned(size, static_cast<std::size_t>(alignment)); } catch (...) { return nullptr; }
}

void operator delete(void* pointer, std::align_val_t) noexcept { freeAligned(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { freeAligned(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { freeAligned(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { freeAligned(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { freeAligned(pointer); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { freeAligned(pointer); }
