#pragma once

#include <vector>
#include <atomic>
#include <cstddef>

#include "macros.h"

namespace Common {
  template<typename T>
  class LFQueue final {
  public:
    // Note: to tell "full" apart from "empty" without an extra counter,
    // usable capacity becomes num_elems - 1 (same trade-off as the SPSC
    // problem from today). If num_elems was previously passed as the
    // exact desired capacity, keep in mind the usable capacity is now
    // one less than what's passed in.
    explicit LFQueue(std::size_t num_elems) :
        store_(num_elems, T()) /* pre-allocation of vector storage. */ {
    }

    // Called by the producer thread. The original never checked for
    // full — if the producer got ahead of the consumer it would just
    // overwrite data that hadn't been read yet. Added that check here:
    // returns nullptr when full.
    auto getNextToWriteTo() noexcept -> T * {
      const auto write_index = next_write_index_.load(std::memory_order_relaxed);
      if (advance(write_index) == next_read_index_.load(std::memory_order_acquire)) {
        return nullptr; // queue full
      }
      return &store_[write_index];
    }

    auto updateWriteIndex() noexcept {
      const auto write_index = next_write_index_.load(std::memory_order_relaxed);
      // release: makes sure the data the caller just wrote into
      // store_[write_index] is visible before the consumer observes
      // the new write_index
      next_write_index_.store(advance(write_index), std::memory_order_release);
    }

    auto getNextToRead() const noexcept -> const T * {
      const auto read_index = next_read_index_.load(std::memory_order_relaxed);
      // acquire: once we observe the producer's released write_index,
      // we're also guaranteed to see the data it wrote into
      // store_[read_index]
      if (read_index == next_write_index_.load(std::memory_order_acquire)) {
        return nullptr; // empty
      }
      return &store_[read_index];
    }

    auto updateReadIndex() noexcept {
      const auto read_index = next_read_index_.load(std::memory_order_relaxed);
      next_read_index_.store(advance(read_index), std::memory_order_release);
    }

    // For monitoring/logging only, never on the producer/consumer's
    // hot path — so there's no need for a separate num_elements_
    // atomic like the original had. That counter was written by both
    // sides, making it an unnecessary third piece of shared state and
    // extra cacheline contention. Two indices are enough to compute size.
    auto size() const noexcept {
      const auto write_index = next_write_index_.load(std::memory_order_acquire);
      const auto read_index = next_read_index_.load(std::memory_order_acquire);
      return (write_index + store_.size() - read_index) % store_.size();
    }

    // Deleted default, copy & move constructors and assignment-operators.
    LFQueue() = delete;

    LFQueue(const LFQueue &) = delete;

    LFQueue(const LFQueue &&) = delete;

    LFQueue &operator=(const LFQueue &) = delete;

    LFQueue &operator=(const LFQueue &&) = delete;

  private:
    auto advance(std::size_t index) const noexcept {
      return (index + 1) % store_.size();
    }

    std::vector<T> store_;

    static constexpr std::size_t kCacheLine = 64;

    // The original packed next_write_index_/next_read_index_/
    // num_elements_ right next to each other — producer, consumer,
    // and size() all poking at the same small chunk of memory.
    // False sharing (and for num_elements_, actual true sharing) was
    // unavoidable. Now there are only two atomics left, each padded
    // to own its own cacheline.
    alignas(kCacheLine) std::atomic<std::size_t> next_write_index_ = {0};
    char pad1_[kCacheLine - sizeof(std::atomic<std::size_t>)];

    alignas(kCacheLine) std::atomic<std::size_t> next_read_index_ = {0};
    char pad2_[kCacheLine - sizeof(std::atomic<std::size_t>)];
  };
}