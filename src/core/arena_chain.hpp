#pragma once

#include "arena.hpp"
#include <vector>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <cassert>

// ------------------------------------------------------------
//  ArenaChain
//
//  A segmented arena: a linked list of fixed-size ArenaAllocator
//  slabs.  When the current slab is exhausted, a new one is
//  allocated transparently.  Total capacity is limited only by
//  available system RAM — no upfront cap required.
//
//  Individual allocations must fit within one slab (i.e. size
//  must be <= SLAB_SIZE). This is always true for our use cases:
//  the largest single allocation is an array of LogEntry structs
//  for one chunk (ChunkVector chunk), which is CHUNK_SIZE × 64
//  bytes = 4 MB, well within a 256 MB slab.
//
//  Strings interned via intern_string() may span slabs — each
//  string is allocated independently and is always smaller than
//  any reasonable slab size.
// ------------------------------------------------------------

class ArenaChain {
public:
    static constexpr size_t SLAB_SIZE = 256ull * 1024 * 1024; // 256 MB

    explicit ArenaChain() {
        add_slab();
    }

    // Non-copyable, non-movable (pointers into slabs must stay valid)
    ArenaChain(const ArenaChain&)            = delete;
    ArenaChain& operator=(const ArenaChain&) = delete;

    // Allocate `size` bytes with `alignment`.
    // Grows into a new slab automatically on exhaustion.
    void* alloc(size_t size, size_t alignment = alignof(std::max_align_t)) {
        assert(size <= SLAB_SIZE && "ArenaChain: single allocation exceeds slab size");
        // Try current slab first
        if (current_->remaining() >= size + alignment) {
            return current_->alloc(size, alignment);
        }
        // Current slab is too full — add a new one
        add_slab();
        return current_->alloc(size, alignment);
    }

    template<typename T>
    T* alloc_array(size_t count) {
        return static_cast<T*>(alloc(sizeof(T) * count, alignof(T)));
    }

    // Copy a string into the chain; returns stable null-terminated pointer.
    const char* intern_string(const char* src, size_t len) {
        char* dst = static_cast<char*>(alloc(len + 1, 1));
        __builtin_memcpy(dst, src, len);
        dst[len] = '\0';
        return dst;
    }

    // Reset all slabs (keeps first slab allocated, drops the rest)
    void reset() {
        slabs_.resize(1);
        slabs_[0]->reset();
        current_ = slabs_[0].get();
    }

    size_t slab_count() const { return slabs_.size(); }
    size_t total_capacity() const { return slabs_.size() * SLAB_SIZE; }
    size_t approximate_used() const {
        size_t u = 0;
        for (auto& s : slabs_) u += s->used();
        return u;
    }

private:
    void add_slab() {
        slabs_.push_back(std::make_unique<ArenaAllocator>(SLAB_SIZE));
        current_ = slabs_.back().get();
    }

    std::vector<std::unique_ptr<ArenaAllocator>> slabs_;
    ArenaAllocator* current_ = nullptr;
};
