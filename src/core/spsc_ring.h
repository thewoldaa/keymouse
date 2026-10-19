// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace km {

#if defined(_MSC_VER)
// The two indices below are aligned to separate cache lines on purpose, so the
// producer and the consumer never fight over one. MSVC reports the padding that
// adds as C4324, which is expected here and not worth reading past.
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

// Single-producer / single-consumer lock-free ring buffer.
//
// The keyboard hook callback is the producer: it must not allocate, lock, or
// block. The engine thread is the consumer. Windows silently removes a
// low-level hook whose callback takes too long, so this buffer is the only
// synchronization between the two.
template <typename T, size_t Capacity>
class SpscRing {
public:
    static_assert(Capacity >= 2, "Capacity must be at least 2");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

    // Producer side. Returns false when the buffer is full; the caller counts
    // the drop and moves on. Never blocks.
    bool push(const T& value) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & kMask;
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        buffer_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false when empty.
    bool pop(T& out) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        out = buffer_[tail];
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    bool empty() const {
        return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire);
    }

    // Approximate; only meaningful when the consumer is quiescent.
    size_t size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & kMask;
    }

    static constexpr size_t capacity() { return Capacity - 1; }

private:
    static constexpr size_t kMask = Capacity - 1;

    std::array<T, Capacity> buffer_{};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace km
