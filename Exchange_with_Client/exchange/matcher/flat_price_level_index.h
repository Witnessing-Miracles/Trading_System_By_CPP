#pragma once

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "me_order.h"

namespace Exchange {

/// Price-level index backed by a sorted, contiguous vector of pointers.
/// BUY levels are ordered best-to-worst (descending); SELL levels are
/// ordered best-to-worst (ascending). Insertion/erasure shifts later entries
/// in O(n); lookup is O(log n), and traversal is contiguous/cache-friendly.
class FlatPriceLevelIndex final {
 public:
  using Entry = MEOrdersAtPrice*;
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
    return it != levels_.end() && (*it)->price_ == price ? *it : nullptr;
  }

  auto find(Price price) const noexcept -> const MEOrdersAtPrice* {
    const auto it = lowerBound(price);
    return it != levels_.end() && (*it)->price_ == price ? *it : nullptr;
  }

  /// Inserts by price while preserving best-to-worst order. Duplicate prices
  /// are rejected; caller owns the pointed-to level object.
  auto insert(MEOrdersAtPrice* level) -> bool {
    if (!level || level->side_ != side_) return false;
    const auto it = lowerBound(level->price_);
    if (it != levels_.end() && (*it)->price_ == level->price_) return false;
    levels_.insert(it, level);
    return true;
  }

  auto erase(Price price) noexcept -> bool {
    const auto it = lowerBound(price);
    if (it == levels_.end() || (*it)->price_ != price) return false;
    levels_.erase(it);
    return true;
  }

  /// Best price-level pointer, or nullptr when this side is empty.
  auto best() const noexcept -> MEOrdersAtPrice* {
    return levels_.empty() ? nullptr : levels_.front();
  }

  auto size() const noexcept -> std::size_t { return levels_.size(); }
  auto empty() const noexcept -> bool { return levels_.empty(); }
  auto reserve(std::size_t capacity) { levels_.reserve(capacity); }
  auto begin() noexcept { return levels_.begin(); }
  auto end() noexcept { return levels_.end(); }
  auto begin() const noexcept { return levels_.begin(); }
  auto end() const noexcept { return levels_.end(); }

 private:
  auto comesBefore(Price lhs, Price rhs) const noexcept -> bool {
    return side_ == Side::BUY ? lhs > rhs : lhs < rhs;
  }

  auto lowerBound(Price price) noexcept -> Storage::iterator {
    return std::lower_bound(levels_.begin(), levels_.end(), price,
        [this](const MEOrdersAtPrice* level, Price key) {
          return comesBefore(level->price_, key);
        });
  }

  auto lowerBound(Price price) const noexcept -> Storage::const_iterator {
    return std::lower_bound(levels_.begin(), levels_.end(), price,
        [this](const MEOrdersAtPrice* level, Price key) {
          return comesBefore(level->price_, key);
        });
  }

  Side side_;
  Storage levels_;
};

}  // namespace Exchange
