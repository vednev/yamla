#pragma once

#include "arena.hpp"
#include <cstddef>
#include <cassert>
#include <utility>
#include <new>

// ------------------------------------------------------------
//  ArenaVector<T>
//
//  A std::vector-like container that allocates its backing
//  storage from an ArenaAllocator. It never frees individual
//  elements; releasing the arena frees everything.
//
//  Growth strategy: capacity doubles on each resize, but the
//  new block is always allocated from the arena (which means
//  the old block becomes dead weight until arena reset).
//  For predictable usage, reserve() up front.
//
//  The vector does NOT own the arena — the arena must outlive
//  all vectors backed by it.
// ------------------------------------------------------------

template<typename T>
class ArenaVector {
public:
    using value_type      = T;
    using size_type       = size_t;
    using reference       = T&;
    using const_reference = const T&;
    using pointer         = T*;
    using const_pointer   = const T*;
    using iterator        = T*;
    using const_iterator  = const T*;

    explicit ArenaVector(ArenaAllocator& arena, size_t initial_capacity = 64)
        : arena_(&arena), size_(0), capacity_(0), data_(nullptr)
    {
        reserve(initial_capacity);
    }

    // Non-copyable (tied to a specific arena)
    ArenaVector(const ArenaVector&)            = delete;
    ArenaVector& operator=(const ArenaVector&) = delete;

    // Movable
    ArenaVector(ArenaVector&& o) noexcept
        : arena_(o.arena_), size_(o.size_), capacity_(o.capacity_), data_(o.data_)
    {
        o.size_     = 0;
        o.capacity_ = 0;
        o.data_     = nullptr;
    }

    // ---- Capacity ------------------------------------------

    void reserve(size_t new_cap) {
        if (new_cap <= capacity_) return;
        T* new_data = arena_->alloc_array<T>(new_cap);
        if (data_ && size_ > 0) {
            // Move existing elements into new block
            for (size_t i = 0; i < size_; ++i) {
                new (new_data + i) T(std::move(data_[i]));
                data_[i].~T();
            }
        }
        data_     = new_data;
        capacity_ = new_cap;
    }

    size_t size()     const { return size_; }
    size_t capacity() const { return capacity_; }
    bool   empty()    const { return size_ == 0; }

    // ---- Element access ------------------------------------

    reference       operator[](size_t i)       { assert(i < size_); return data_[i]; }
    const_reference operator[](size_t i) const { assert(i < size_); return data_[i]; }

    reference       front()       { assert(size_ > 0); return data_[0]; }
    const_reference front() const { assert(size_ > 0); return data_[0]; }

    reference       back()        { assert(size_ > 0); return data_[size_ - 1]; }
    const_reference back()  const { assert(size_ > 0); return data_[size_ - 1]; }

    pointer       data()       { return data_; }
    const_pointer data() const { return data_; }

    // ---- Mutation ------------------------------------------

    void push_back(const T& val) {
        grow_if_needed();
        new (data_ + size_) T(val);
        ++size_;
    }

    void push_back(T&& val) {
        grow_if_needed();
        new (data_ + size_) T(std::move(val));
        ++size_;
    }

    template<typename... Args>
    T& emplace_back(Args&&... args) {
        grow_if_needed();
        T* slot = new (data_ + size_) T(std::forward<Args>(args)...);
        ++size_;
        return *slot;
    }

    void clear() {
        for (size_t i = 0; i < size_; ++i) {
            data_[i].~T();
        }
        size_ = 0;
    }

    // ---- Iterators -----------------------------------------

    iterator       begin()        { return data_; }
    const_iterator begin()  const { return data_; }
    iterator       end()          { return data_ + size_; }
    const_iterator end()    const { return data_ + size_; }

private:
    void grow_if_needed() {
        if (size_ < capacity_) return;
        size_t new_cap = capacity_ ? capacity_ * 2 : 64;
        reserve(new_cap);
    }

    ArenaAllocator* arena_;
    size_t          size_;
    size_t          capacity_;
    T*              data_;
};
