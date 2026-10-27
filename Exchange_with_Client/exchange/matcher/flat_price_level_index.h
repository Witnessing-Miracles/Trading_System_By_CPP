#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "me_order.h"

namespace Exchange {

/// Sorted contiguous price index. Each Entry stores the search key and level
/// pointer together, so binary search touches contiguous key metadata and only
/// dereferences the level pointer after a successful lookup. BUY is descending;
/// SELL is ascending. Insert/erase shift entries in O(n).
class FlatPriceLevelIndex final {
 public:
  struct Entry {
    Price price_;
    MEOrdersAtPrice* level_;
  };
  using Storage = std::vector<Entry>;

  explicit FlatPriceLevelIndex(Side side) : side_(side) {
    if (side != Side::BUY && side != Side::SELL) {
      throw std::invalid_argument("FlatPriceLevelIndex requires BUY or SELL");
    }
  }

  FlatPriceLevelIndex(const FlatPriceLevelIndex&) = delete;
  FlatPriceLevelIndex& operator=(const FlatPriceLevelIndex&) = delete;

  auto find(Price price) noexcept -> MEOrdersAtPrice* {
    const auto it = lowerBound(price);
    return it != levels_.end() && it->price_ == price ? it->level_ : nullptr;
  }

  auto find(Price price) const noexcept -> const MEOrdersAtPrice* {
    const auto it = lowerBound(price);
    return it != levels_.end() && it->price_ == price ? it->level_ : nullptr;
  }

  /// Rejects null, wrong-side, or duplicate-price entries.
  auto insert(MEOrdersAtPrice* level) -> bool {
    if (!level || level->side_ != side_) return false;
    const auto it = lowerBound(level->price_);
    if (it != levels_.end() && it->price_ == level->price_) return false;
    levels_.insert(it, Entry{level->price_, level});
    return true;
  }

  auto erase(Price price) noexcept -> bool {
    const auto it = lowerBound(price);
    if (it == levels_.end() || it->price_ != price) return false;
    levels_.erase(it);
    return true;
  }

  /// Preallocate expected active price-level entries before replay/timing.
  auto reserve(std::size_t capacity) { levels_.reserve(capacity); }
  auto size() const noexcept -> std::size_t { return levels_.size(); }
  auto empty() const noexcept -> bool { return levels_.empty(); }

 private:
  auto comesBefore(Price lhs, Price rhs) const noexcept -> bool {
    return side_ == Side::BUY ? lhs > rhs : lhs < rhs;
  }

  auto lowerBound(Price price) noexcept -> Storage::iterator {
    return std::lower_bound(levels_.begin(), levels_.end(), price,
        [this](const Entry& entry, Price key) {
          return comesBefore(entry.price_, key);
        });
  }

  auto lowerBound(Price price) const noexcept -> Storage::const_iterator {
    return std::lower_bound(levels_.begin(), levels_.end(), price,
        [this](const Entry& entry, Price key) {
          return comesBefore(entry.price_, key);
        });
  }

  Side side_;
  Storage levels_;
};

}  // namespace Exchange
