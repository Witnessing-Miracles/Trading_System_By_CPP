#include "../exchange/matcher/flat_price_level_index.h"
#include "../exchange/matcher/unordered_map_price_level_index.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Exchange::FlatPriceLevelIndex;
using Exchange::UnorderedMapPriceLevelIndex;
using Exchange::MEOrdersAtPrice;
using Common::Price;
using Common::Side;

constexpr std::size_t kLookupCount = 200'000;
constexpr int kTrials = 9;
constexpr Price kBasePrice = 1'000'000;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

auto makeLevel(Side side, Price price) -> MEOrdersAtPrice {
  return MEOrdersAtPrice{side, price, nullptr, nullptr, nullptr};
}

template <typename Index>
auto semanticCheck(Side side) -> void {
  Index index(side);
  auto p100 = makeLevel(side, 100);
  auto p101 = makeLevel(side, 101);
  auto p99 = makeLevel(side, 99);
  auto p356 = makeLevel(side, 356);  // Same residue as 100 modulo ME_MAX_PRICE_LEVELS.

  require(index.empty(), "new index must be empty");
  require(index.find(100) == nullptr, "lookup in empty index must miss");
  require(index.insert(&p100), "first insert failed");
  require(index.insert(&p101), "second insert failed");
  require(index.insert(&p99), "third insert failed");
  require(index.insert(&p356), "price with same modulo residue must remain distinct");
  require(!index.insert(&p100), "duplicate price must be rejected");
  require(index.find(100) == &p100, "find returned wrong level");
  require(index.find(356) == &p356, "modulo-colliding price returned wrong level");
  require(index.find(777) == nullptr, "missing price must return nullptr");
  require(index.erase(100), "erase existing price failed");
  require(!index.erase(100), "erasing missing price must fail");
  require(index.find(100) == nullptr, "erased price is still present");
  require(index.find(356) == &p356, "erasing one price removed a modulo-colliding price");
  require(index.size() == 3, "wrong size after erase");
}

auto runSemanticChecks() -> void {
  semanticCheck<UnorderedMapPriceLevelIndex>(Side::BUY);
  semanticCheck<UnorderedMapPriceLevelIndex>(Side::SELL);
  semanticCheck<FlatPriceLevelIndex>(Side::BUY);
  semanticCheck<FlatPriceLevelIndex>(Side::SELL);

  // Compare membership and size across both containers.
  for (const auto side : {Side::BUY, Side::SELL}) {
    UnorderedMapPriceLevelIndex hash_index(side);
    FlatPriceLevelIndex flat_index(side);
    std::vector<MEOrdersAtPrice> levels;
    levels.reserve(1024);
    for (Price i = 0; i < 1024; ++i) {
      levels.push_back(makeLevel(side, 10'000 + i * 3));
      require(hash_index.insert(&levels.back()), "unordered_map setup insert failed");
      require(flat_index.insert(&levels.back()), "flat setup insert failed");
    }
    require(hash_index.size() == flat_index.size(), "size mismatch");
    for (const auto& level : levels) {
      require(hash_index.find(level.price_) == flat_index.find(level.price_), "find mismatch");
    }
    for (Price i = 0; i < 1024; i += 2) {
      const Price price = 10'000 + i * 3;
      require(hash_index.erase(price) == flat_index.erase(price), "erase result mismatch");
    }
    require(hash_index.size() == flat_index.size(), "post-erase size mismatch");
  }
}

auto median(std::vector<double> values) -> double {
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

template <typename Index>
auto measureLookup(Side side, const std::vector<Price>& prices,
                   const std::vector<Price>& queries) -> double {
  Index index(side);
  index.reserve(prices.size());
  std::vector<MEOrdersAtPrice> levels;
  levels.reserve(prices.size());
  for (const auto price : prices) levels.push_back(makeLevel(side, price));
  for (auto& level : levels) {
    if (!index.insert(&level)) throw std::runtime_error("unexpected duplicate setup price");
  }

  std::vector<double> samples;
  samples.reserve(kTrials);
  std::uint64_t checksum = 0;
  for (int trial = 0; trial < kTrials; ++trial) {
    const auto start = std::chrono::steady_clock::now();
    for (const auto price : queries) {
      const auto* level = index.find(price);
      checksum += level ? static_cast<std::uint64_t>(level->price_) : 1;
    }
    const auto stop = std::chrono::steady_clock::now();
    const auto elapsed_ns = std::chrono::duration<double, std::nano>(stop - start).count();
    samples.push_back(elapsed_ns / static_cast<double>(queries.size()));
  }
  // Keep the lookup results observable to the compiler.
  if (checksum == 0) std::cerr << "unexpected zero checksum\n";
  return median(std::move(samples));
}

template <typename Index>
auto measureInsertErase(Side side, std::size_t active_levels,
                        const std::vector<Price>& transient_prices) -> double {
  Index index(side);
  index.reserve(active_levels + 1);
  std::vector<MEOrdersAtPrice> levels;
  levels.reserve(active_levels + 1);
  for (std::size_t i = 0; i < active_levels; ++i) {
    levels.push_back(makeLevel(side, kBasePrice + static_cast<Price>(i) * 4));
    require(index.insert(&levels.back()), "setup insert failed");
  }
  levels.push_back(makeLevel(side, transient_prices.front()));
  auto* transient = &levels.back();

  std::vector<double> samples;
  samples.reserve(kTrials);
  for (int trial = 0; trial < kTrials; ++trial) {
    const auto start = std::chrono::steady_clock::now();
    for (const auto price : transient_prices) {
      transient->price_ = price;
      if (!index.insert(transient)) throw std::runtime_error("transient insert failed");
      if (!index.erase(price)) throw std::runtime_error("transient erase failed");
    }
    const auto stop = std::chrono::steady_clock::now();
    const auto elapsed_ns = std::chrono::duration<double, std::nano>(stop - start).count();
    samples.push_back(elapsed_ns / static_cast<double>(transient_prices.size() * 2));
  }
  return median(std::move(samples));
}

enum class RunMode { COMPARE, UNORDERED_MAP_ONLY, FLAT_ONLY };

auto runBenchmarks(RunMode mode) -> void {
  std::mt19937_64 rng(0xC0FFEE);
  std::cout << "Correctness checks: PASS (BUY/SELL, insert/find/erase, parity)\n";
  std::cout << "Compiler: C++20; timer: steady_clock; statistic: median of " << kTrials << " trials\n";
  std::cout << "Units: nanoseconds/op; logging and order-book response queues are excluded.\n\n";
  if (mode == RunMode::COMPARE) {
    std::cout << "side,levels,hit_unordered_map_ns,hit_flat_ns,miss_unordered_map_ns,miss_flat_ns,insert_erase_unordered_map_ns,insert_erase_flat_ns\n";
  } else {
    std::cout << "side,levels,hit_ns,miss_ns,insert_erase_ns\n";
  }

  for (const auto side : {Side::BUY, Side::SELL}) {
    for (const std::size_t count : {16U, 64U, 128U, 256U}) {
    std::vector<Price> prices;
    prices.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
      prices.push_back(kBasePrice + static_cast<Price>(i) * 4);
    }

    std::vector<Price> hit_queries(kLookupCount);
    for (auto& query : hit_queries) {
      query = prices[static_cast<std::size_t>(rng() % prices.size())];
    }
    std::vector<Price> miss_queries(kLookupCount);
    for (auto& query : miss_queries) {
      query = kBasePrice + 1 + static_cast<Price>(rng() % (count + 1)) * 4;
    }

    double hash_hit = 0.0;
    double hash_miss = 0.0;
    double hash_insert_erase = 0.0;
    double flat_hit = 0.0;
    double flat_miss = 0.0;
    double flat_insert_erase = 0.0;

    if (mode != RunMode::FLAT_ONLY) {
      hash_hit = measureLookup<UnorderedMapPriceLevelIndex>(side, prices, hit_queries);
      hash_miss = measureLookup<UnorderedMapPriceLevelIndex>(side, prices, miss_queries);
    }
    if (mode != RunMode::UNORDERED_MAP_ONLY) {
      flat_hit = measureLookup<FlatPriceLevelIndex>(side, prices, hit_queries);
      flat_miss = measureLookup<FlatPriceLevelIndex>(side, prices, miss_queries);
    }

    std::vector<Price> transient_prices(20'000);
    for (std::size_t i = 0; i < transient_prices.size(); ++i) {
      // Keep transient keys unique from active levels and alternate their position.
      transient_prices[i] = kBasePrice + static_cast<Price>(count) * 4 + 10'000 +
                            static_cast<Price>((i * 7919) % 40'000);
    }
    if (mode != RunMode::FLAT_ONLY) {
      hash_insert_erase = measureInsertErase<UnorderedMapPriceLevelIndex>(side, count, transient_prices);
    }
    if (mode != RunMode::UNORDERED_MAP_ONLY) {
      flat_insert_erase = measureInsertErase<FlatPriceLevelIndex>(side, count, transient_prices);
    }

    const auto side_name = side == Side::BUY ? "BUY" : "SELL";
    if (mode == RunMode::COMPARE) {
      std::cout << side_name << ',' << count << ',' << hash_hit << ',' << flat_hit << ',' << hash_miss << ',' << flat_miss
                << ',' << hash_insert_erase << ',' << flat_insert_erase << '\n';
    } else if (mode == RunMode::UNORDERED_MAP_ONLY) {
      std::cout << side_name << ',' << count << ',' << hash_hit << ',' << hash_miss << ',' << hash_insert_erase << '\n';
    } else {
      std::cout << side_name << ',' << count << ',' << flat_hit << ',' << flat_miss << ',' << flat_insert_erase << '\n';
    }
    }
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    runSemanticChecks();
    auto mode = RunMode::COMPARE;
    if (argc == 3 && std::string(argv[1]) == "--only") {
      const std::string selected(argv[2]);
      if (selected == "unordered_map") mode = RunMode::UNORDERED_MAP_ONLY;
      else if (selected == "flat") mode = RunMode::FLAT_ONLY;
      else throw std::invalid_argument("usage: price_level_index_benchmark [--only unordered_map|flat]");
    } else if (argc != 1) {
      throw std::invalid_argument("usage: price_level_index_benchmark [--only unordered_map|flat]");
    }
    runBenchmarks(mode);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
