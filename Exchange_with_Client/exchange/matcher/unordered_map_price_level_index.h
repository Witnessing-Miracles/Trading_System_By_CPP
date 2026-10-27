#pragma once

#include <cstddef>
#include <stdexcept>
#include <unordered_map>

#include "me_order.h"

namespace Exchange {

/// Price-to-level index matching the existing UnorderedMapMEOrderBook layout.
/// The order-book's ordered price-level list remains a separate structure;
/// this class isolates only the Price -> MEOrdersAtPrice lookup index.
class UnorderedMapPriceLevelIndex final {
 public:
  using Storage = std::unordered_map<Price, MEOrdersAtPrice*>;

  explicit UnorderedMapPriceLevelIndex(Side side) : side_(side) {
    if (side != Side::BUY && side != Side::SELL) {
      throw std::invalid_argument("UnorderedMapPriceLevelIndex requires BUY or SELL");
    }
  }

  UnorderedMapPriceLevelIndex(const UnorderedMapPriceLevelIndex&) = delete;
  UnorderedMapPriceLevelIndex& operator=(const UnorderedMapPriceLevelIndex&) = delete;

  auto find(Price price) noexcept -> MEOrdersAtPrice* {
    const auto it = levels_.find(price);
    return it == levels_.end() ? nullptr : it->second;
  }

  auto find(Price price) const noexcept -> const MEOrdersAtPrice* {
    const auto it = levels_.find(price);
    return it == levels_.end() ? nullptr : it->second;
  }

  auto insert(MEOrdersAtPrice* level) -> bool {
    if (!level || level->side_ != side_) return false;
    return levels_.emplace(level->price_, level).second;
  }

  auto erase(Price price) noexcept -> bool { return levels_.erase(price) != 0; }

  auto size() const noexcept -> std::size_t { return levels_.size(); }
  auto empty() const noexcept -> bool { return levels_.empty(); }
  auto reserve(std::size_t expected_entries) { levels_.reserve(expected_entries); }

 private:
  Side side_;
  Storage levels_;
};

}  // namespace Exchange
