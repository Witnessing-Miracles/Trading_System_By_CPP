#pragma once

#include <cstddef>
#include <cstdint>
#include <bit>
#include <stdexcept>
#include <vector>

#include "me_order.h"

namespace Exchange {

/// Flat open-addressed price index. Slots are stored contiguously and
/// collisions are resolved by linear probing. Deletion closes the following
/// cluster, avoiding tombstones. The caller owns MEOrdersAtPrice objects.
class FlatHashPriceLevelIndex final {
 public:
  struct Slot {
    Price price_{};
    MEOrdersAtPrice* level_ = nullptr;
  };

  explicit FlatHashPriceLevelIndex(Side side) : side_(side) {
    if (side != Side::BUY && side != Side::SELL) {
      throw std::invalid_argument("FlatHashPriceLevelIndex requires BUY or SELL");
    }
  }

  FlatHashPriceLevelIndex(const FlatHashPriceLevelIndex&) = delete;
  FlatHashPriceLevelIndex& operator=(const FlatHashPriceLevelIndex&) = delete;

  auto find(Price price) noexcept -> MEOrdersAtPrice* {
    const auto index = findIndex(price);
    return index == npos ? nullptr : slots_[index].level_;
  }

  auto find(Price price) const noexcept -> const MEOrdersAtPrice* {
    const auto index = findIndex(price);
    return index == npos ? nullptr : slots_[index].level_;
  }

  auto insert(MEOrdersAtPrice* level) -> bool {
    if (!level || level->side_ != side_) return false;
    if (findIndex(level->price_) != npos) return false;
    if (slots_.empty()) rehash(16);
    if ((size_ + 1) * 10 > slots_.size() * 7) rehash(slots_.size() * 2);
    insertNoGrow(level->price_, level);
    return true;
  }

  auto erase(Price price) noexcept -> bool {
    auto hole = findIndex(price);
    if (hole == npos) return false;
    slots_[hole] = Slot{};
    --size_;

    // Reinsert the remainder of this probe cluster so lookups never need tombstones.
    auto scan = (hole + 1) & (slots_.size() - 1);
    while (slots_[scan].level_ != nullptr) {
      const auto saved_price = slots_[scan].price_;
      auto* const saved_level = slots_[scan].level_;
      slots_[scan] = Slot{};
      --size_;
      insertNoGrow(saved_price, saved_level);
      scan = (scan + 1) & (slots_.size() - 1);
    }
    return true;
  }

  auto reserve(std::size_t expected_entries) -> void {
    const auto required = expected_entries == 0 ? std::size_t{16}
        : (expected_entries * 10 + 6) / 7;
    auto capacity = std::size_t{16};
    while (capacity < required) capacity *= 2;
    if (capacity > slots_.size()) rehash(capacity);
  }

  auto size() const noexcept -> std::size_t { return size_; }
  auto empty() const noexcept -> bool { return size_ == 0; }
  auto capacity() const noexcept -> std::size_t { return slots_.size(); }
  auto storageBytes() const noexcept -> std::size_t { return slots_.capacity() * sizeof(Slot); }

 private:
  static constexpr std::size_t npos = static_cast<std::size_t>(-1);

  static auto hashPrice(Price price) noexcept -> std::uint64_t {
    // Fibonacci-style multiplication spreads nearby tick prices using one multiply.
    return static_cast<std::uint64_t>(price) * 11400714819323198485ULL;
  }

  auto bucketIndex(Price price) const noexcept -> std::size_t {
    const auto bits = std::countr_zero(slots_.size());
    return static_cast<std::size_t>(hashPrice(price) >> (64 - bits));
  }

  auto findIndex(Price price) const noexcept -> std::size_t {
    if (slots_.empty()) return npos;
    auto index = bucketIndex(price);
    for (std::size_t probes = 0; probes < slots_.size(); ++probes) {
      const auto& slot = slots_[index];
      if (slot.level_ == nullptr) return npos;
      if (slot.price_ == price) return index;
      index = (index + 1) & (slots_.size() - 1);
    }
    return npos;
  }

  auto insertNoGrow(Price price, MEOrdersAtPrice* level) noexcept -> void {
    auto index = bucketIndex(price);
    while (slots_[index].level_ != nullptr) index = (index + 1) & (slots_.size() - 1);
    slots_[index] = Slot{price, level};
    ++size_;
  }

  auto rehash(std::size_t capacity) -> void {
    std::vector<Slot> old_slots;
    old_slots.swap(slots_);
    slots_.resize(capacity);
    size_ = 0;
    for (const auto& slot : old_slots) {
      if (slot.level_ != nullptr) insertNoGrow(slot.price_, slot.level_);
    }
  }

  Side side_;
  std::vector<Slot> slots_;
  std::size_t size_ = 0;
};

}  // namespace Exchange
