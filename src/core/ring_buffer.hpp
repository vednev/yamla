#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <optional>

// ------------------------------------------------------------
//  RingBuffer<T, Capacity>
//
//  Single-producer / single-consumer lock-free ring buffer.
//  For multi-producer scenarios wrap in a mutex or use one
//  ring buffer per producer thread and drain from the main
//  thread.
//
//  Capacity must be a power of two.
// ------------------------------------------------------------

template<typename T, size_t Capacity>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "RingBuffer: Capacity must be a power of two");

public:
    RingBuffer() : head_(0), tail_(0) {}

    // Producer side — returns false if buffer is full.
    bool try_push(const T& val) {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t next_h = (h + 1) & mask_;
        if (next_h == tail_.load(std::memory_order_acquire))
            return false; // full
        slots_[h] = val;
        head_.store(next_h, std::memory_order_release);
        return true;
    }

    bool try_push(T&& val) {
        size_t h = head_.load(std::memory_order_relaxed);
        size_t next_h = (h + 1) & mask_;
        if (next_h == tail_.load(std::memory_order_acquire))
            return false;
        slots_[h] = std::move(val);
        head_.store(next_h, std::memory_order_release);
        return true;
    }

    // Consumer side — returns empty optional if buffer is empty.
    std::optional<T> try_pop() {
        size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire))
            return std::nullopt; // empty
        T val = std::move(slots_[t]);
        tail_.store((t + 1) & mask_, std::memory_order_release);
        return val;
    }

    bool empty() const {
        return tail_.load(std::memory_order_acquire) ==
               head_.load(std::memory_order_acquire);
    }

    size_t size() const {
        size_t h = head_.load(std::memory_order_acquire);
        size_t t = tail_.load(std::memory_order_acquire);
        return (h - t) & mask_;
    }

    static constexpr size_t capacity() { return Capacity; }

private:
    static constexpr size_t mask_ = Capacity - 1;

    std::array<T, Capacity> slots_;
    std::atomic<size_t>     head_;
    std::atomic<size_t>     tail_;
};
