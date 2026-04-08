#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstdlib>
#include <new>

// ------------------------------------------------------------
//  ArenaAllocator
//
//  A single contiguous memory slab. Allocations bump a pointer
//  forward; there is no per-allocation free — the entire arena
//  is released at once by calling reset() or via the destructor.
//
//  Alignment is always satisfied by rounding up the current
//  position to the requested alignment boundary.
//
//  On exhaustion the allocator asserts — callers must size the
//  arena appropriately before loading a cluster.
// ------------------------------------------------------------

class ArenaAllocator {
public:
    explicit ArenaAllocator(size_t capacity)
        : capacity_(capacity)
        , used_(0)
    {
        assert(capacity > 0);
        slab_ = static_cast<uint8_t*>(std::malloc(capacity));
        assert(slab_ != nullptr && "ArenaAllocator: out of system memory");
    }

    ~ArenaAllocator() {
        std::free(slab_);
    }

    // Non-copyable, movable
    ArenaAllocator(const ArenaAllocator&)            = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    ArenaAllocator(ArenaAllocator&& o) noexcept
        : slab_(o.slab_), capacity_(o.capacity_), used_(o.used_)
    {
        o.slab_     = nullptr;
        o.capacity_ = 0;
        o.used_     = 0;
    }

    // Allocate `size` bytes with `alignment`.
    // Aborts on exhaustion — size the arena correctly up front.
    void* alloc(size_t size, size_t alignment = alignof(std::max_align_t)) {
        size_t aligned_pos = align_up(used_, alignment);
        size_t new_used    = aligned_pos + size;
        assert(new_used <= capacity_ && "ArenaAllocator: slab exhausted");
        used_ = new_used;
        return slab_ + aligned_pos;
    }

    // Allocate and zero-initialise.
    void* alloc_zeroed(size_t size, size_t alignment = alignof(std::max_align_t)) {
        void* ptr = alloc(size, alignment);
        __builtin_memset(ptr, 0, size);
        return ptr;
    }

    // Typed helpers
    template<typename T>
    T* alloc_one() {
        return static_cast<T*>(alloc(sizeof(T), alignof(T)));
    }

    template<typename T>
    T* alloc_array(size_t count) {
        return static_cast<T*>(alloc(sizeof(T) * count, alignof(T)));
    }

    // Copy a string into the arena; returns a null-terminated pointer.
    const char* intern_string(const char* src, size_t len) {
        char* dst = static_cast<char*>(alloc(len + 1, 1));
        __builtin_memcpy(dst, src, len);
        dst[len] = '\0';
        return dst;
    }

    // Reset — free all allocations in O(1), keep slab.
    void reset() { used_ = 0; }

    size_t capacity() const { return capacity_; }
    size_t used()     const { return used_; }
    size_t remaining() const { return capacity_ - used_; }

private:
    static size_t align_up(size_t value, size_t alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    uint8_t* slab_    = nullptr;
    size_t   capacity_ = 0;
    size_t   used_     = 0;
};
