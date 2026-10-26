#pragma once

#include <map>
#include <stdexcept>

#include "me_order.h"

namespace Exchange {

/// Price-level index backed by std::map. The map is ordered by ascending
/// price; best() selects rbegin() for bids and begin() for asks.
/// MEOrdersAtPrice and its per-price FIFO list remain unchanged.
class MapPriceLevelIndex final {
 public:
  using Storage = std::map<Price, MEOrdersAtPrice*>;

  explicit MapPriceLevelIndex(Side side) : side_(side) {
    if (side != Side::BUY && side != Side::SELL) {
      throw std::invalid_argument("MapPriceLevelIndex requires BUY or SELL");
    }
  }

  MapPriceLevelIndex(const MapPriceLevelIndex&) = delete;
  MapPriceLevelIndex& operator=(const MapPriceLevelIndex&) = delete;

  auto find(Price price) noexcept -> MEOrdersAtPrice* {
    const auto it = levels_.find(price);
    return it == levels_.end() ? nullptr : it->second;
  }

  auto find(Price price) const noexcept -> const MEOrdersAtPrice* {
    const auto it = levels_.find(price);
    return it == levels_.end() ? nullptr : it->second;
  }

  /// Rejects null, wrong-side, or duplicate-price entries.
  auto insert(MEOrdersAtPrice* level) -> bool {
    if (!level || level->side_ != side_) return false;
    return levels_.emplace(level->price_, level).second;
  }

  auto erase(Price price) noexcept -> bool { return levels_.erase(price) != 0; }

  /// Best price-level pointer, or nullptr when empty.
  auto best() const noexcept -> MEOrdersAtPrice* {
    if (levels_.empty()) return nullptr;
    return side_ == Side::BUY ? levels_.rbegin()->second : levels_.begin()->second;
  }

  auto size() const noexcept -> std::size_t { return levels_.size(); }
  auto empty() const noexcept -> bool { return levels_.empty(); }

 private:
  Side side_;
  Storage levels_;
};

}  // namespace Exchange
