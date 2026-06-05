#pragma once
#include <array>
#include <atomic>
#ifndef PLATFORM_DEFS_H
#define PLATFORM_DEFS_H

#include <cstddef>
#include <new>

#if defined(__cpp_lib_hardware_interference_size)
// Use the standard if available
inline constexpr std::size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
// Fallback for macOS/other systems lacking the feature
// 64 is safe for x86_64 and Apple Silicon
inline constexpr std::size_t kCacheLineSize = 64;
#endif

#endif

// Its best not to split a template class into .h and .cpp because
// whenever an object is initialised, the compiler writes a version
// on the spot. Separating them can cause undefined references
template <typename T, size_t Capacity>
class LockFreeRingBuffer {
 private:
  // std::hardware_constructive_interference_size is the minimum constant distance between two
  // objects to avoid "False Sharing"
  // Forces tail and head to start on separate cache lines
  alignas(std::hardware_destructive_interference_size) std::atomic<size_t> tail_{0};
  alignas(std::hardware_destructive_interference_size) std::atomic<size_t> head_{0};

  // Fixed size storage array
  std::array<T, Capacity> slots_;

 public:
  static_assert((Capacity != 0) && ((Capacity & (Capacity - 1)) == 0),
                "CRITICAL: RingBuffer Capacity must be a strict power of 2!");
  bool push(const T& item) {
    // .load is needed to access the value for atomic reads
    // Atomic operations introduce guardrails against reordering
    // std::memory_order_relaxed is used to prevent those guardrails
    size_t current_tail = tail_.load(std::memory_order_relaxed);
    // We use relaxed here because it is faster and does not

    // acquire implies that we cannot order a write that is before it, to be after it.
    size_t current_head = head_.load(std::memory_order_acquire);

    if (current_tail - current_head == Capacity) {
      return false;
    }

    // Since our size is planned to be a power of two,
    // the mod operator can be written like this as well
    slots_[current_tail & (Capacity - 1)] = item;

    // Release implies We cannot reorder a read that is after this
    // to be before this
    tail_.store(current_tail + 1, std::memory_order_release);

    return true;
  }

  bool pop(T& item) {
    size_t current_head = head_.load(std::memory_order_relaxed);

    size_t current_tail = tail_.load(std::memory_order_acquire);

    if (current_head == current_tail) {
      return false;
    }

    item = slots_[current_head & (Capacity - 1)];

    head_.store(current_head + 1, std::memory_order_release);

    return true;
  }
};
