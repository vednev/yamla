#pragma once

#include "arena_chain.hpp"
#include <cstddef>
#include <cassert>
#include <vector>
#include <queue>
#include <functional>
#include <algorithm>
#include <utility>

// ------------------------------------------------------------
//  ChunkVector<T>
//
//  A random-access container whose backing storage is allocated
//  from an ArenaChain in fixed-size chunks.  No single contiguous
//  array is required — chunks can live in different arena slabs.
//
//  Random access is O(1):
//      chunk_index = i / CHUNK_CAPACITY
//      offset      = i % CHUNK_CAPACITY
//
//  Within each chunk elements are contiguous, so iteration and
//  sorting within a chunk are cache-friendly.
//
//  Used for LogEntry storage so entries can exceed the 2 GB
//  limit of a single arena slab.
// ------------------------------------------------------------

template<typename T, size_t CHUNK_CAPACITY = 65536>
class ChunkVector {
public:
    struct Chunk {
        T*     data  = nullptr;
        size_t count = 0;
    };

    explicit ChunkVector(ArenaChain& chain)
        : chain_(&chain), total_(0)
    {}

    // Non-copyable
    ChunkVector(const ChunkVector&)            = delete;
    ChunkVector& operator=(const ChunkVector&) = delete;

    void push_back(const T& val) {
        if (chunks_.empty() || chunks_.back().count == CHUNK_CAPACITY)
            add_chunk();
        Chunk& c = chunks_.back();
        c.data[c.count++] = val;
        ++total_;
    }

    void push_back(T&& val) {
        if (chunks_.empty() || chunks_.back().count == CHUNK_CAPACITY)
            add_chunk();
        Chunk& c = chunks_.back();
        c.data[c.count++] = std::move(val);
        ++total_;
    }

    T& operator[](size_t i) {
        assert(i < total_);
        size_t ci = i / CHUNK_CAPACITY;
        size_t oi = i % CHUNK_CAPACITY;
        return chunks_[ci].data[oi];
    }

    const T& operator[](size_t i) const {
        assert(i < total_);
        size_t ci = i / CHUNK_CAPACITY;
        size_t oi = i % CHUNK_CAPACITY;
        return chunks_[ci].data[oi];
    }

    size_t size()  const { return total_; }
    bool   empty() const { return total_ == 0; }

    size_t chunk_count() const { return chunks_.size(); }

    // ---- Chunk-aware sort -----------------------------------
    //
    // 1. Sort each chunk independently (contiguous → cache-friendly)
    // 2. K-way merge across chunks using a min-heap
    //
    // Result: a sorted ChunkVector with the same chunks (sorted
    // in-place within each chunk, then merged into fresh chunks).
    template<typename Compare>
    void sort(ArenaChain& scratch_chain, Compare cmp) {
        if (total_ <= 1) return;

        // Step 1: sort each chunk in-place
        for (auto& c : chunks_)
            std::sort(c.data, c.data + c.count, cmp);

        if (chunks_.size() == 1) return; // already done

        // Step 2: K-way merge into a scratch ChunkVector
        ChunkVector<T, CHUNK_CAPACITY> merged(scratch_chain);

        // Min-heap entry: (value, chunk_idx, element_idx)
        using Entry = std::tuple<T, size_t, size_t>;
        auto heap_cmp = [&](const Entry& a, const Entry& b) {
            // min-heap: compare via cmp (ascending)
            return cmp(std::get<0>(b), std::get<0>(a));
        };
        std::priority_queue<Entry, std::vector<Entry>, decltype(heap_cmp)>
            heap(heap_cmp);

        // Seed the heap with the first element of each chunk
        for (size_t ci = 0; ci < chunks_.size(); ++ci) {
            if (chunks_[ci].count > 0)
                heap.push({ chunks_[ci].data[0], ci, 0 });
        }

        while (!heap.empty()) {
            auto [val, ci, ei] = heap.top();
            heap.pop();
            merged.push_back(val);
            size_t next = ei + 1;
            if (next < chunks_[ci].count)
                heap.push({ chunks_[ci].data[next], ci, next });
        }

        // Swap contents
        chunks_ = std::move(merged.chunks_);
        total_  = merged.total_;
        chain_  = merged.chain_;
    }

    // ---- Chunk access (for iteration) -----------------------
    const std::vector<Chunk>& chunks() const { return chunks_; }

    // ---- Simple iterator ------------------------------------
    class iterator {
    public:
        iterator(ChunkVector* cv, size_t idx) : cv_(cv), idx_(idx) {}
        T& operator*()  { return (*cv_)[idx_]; }
        iterator& operator++() { ++idx_; return *this; }
        bool operator!=(const iterator& o) const { return idx_ != o.idx_; }
    private:
        ChunkVector* cv_;
        size_t idx_;
    };

    class const_iterator {
    public:
        const_iterator(const ChunkVector* cv, size_t idx) : cv_(cv), idx_(idx) {}
        const T& operator*() const { return (*cv_)[idx_]; }
        const_iterator& operator++() { ++idx_; return *this; }
        bool operator!=(const const_iterator& o) const { return idx_ != o.idx_; }
    private:
        const ChunkVector* cv_;
        size_t idx_;
    };

    iterator       begin()       { return { this, 0 };      }
    iterator       end()         { return { this, total_ };  }
    const_iterator begin() const { return { this, 0 };      }
    const_iterator end()   const { return { this, total_ }; }

    void clear() {
        // Chunks' backing memory is in the arena — just reset counters
        for (auto& c : chunks_) c.count = 0;
        chunks_.clear();
        total_ = 0;
    }

private:
    void add_chunk() {
        Chunk c;
        c.data  = chain_->alloc_array<T>(CHUNK_CAPACITY);
        c.count = 0;
        chunks_.push_back(c);
    }

    ArenaChain*        chain_;
    std::vector<Chunk> chunks_;
    size_t             total_;
};
