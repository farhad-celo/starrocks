#pragma once

#include <cstddef>
#include <random>
#include <span>
#include <string_view>

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/enumerable_thread_specific.h>
#include <oneapi/tbb/parallel_for.h>

#include <ctl/hash.h>
#include <ctl/hyperloglog.h>

#include "legacy_embedded_ctl/bitset_view.h"
#include "modules/common/shared_types_fwd.h"

namespace celonis::accelerator::memory {

namespace details {

template <typename T>
struct hyperloglog_hash {
  explicit hyperloglog_hash(uint64_t seed) : seed_{seed} {}
  size_t operator()(const T& data) const {
    return ctl::hash_murmur_64a(std::string_view{reinterpret_cast<const char*>(&data), sizeof(data)}, seed_);
  }

 private:
  const uint64_t seed_;
};

template <>
struct hyperloglog_hash<cel_string_t> {
  explicit hyperloglog_hash(uint64_t seed) : seed_{seed} {}
  size_t operator()(const cel_string_t& data) const { return ctl::hash_murmur_64a(data, seed_); }

 private:
  const uint64_t seed_;
};

// TODO(lwolf) Fully replace murmur2A with CRC32 after evaluation
template <>
struct hyperloglog_hash<cel_int_t> {
  explicit hyperloglog_hash(uint64_t /*seed*/) {}

  size_t operator()(const cel_int_t& data) const { return hash_impl(data); }

 private:
  ctl::hash_crc32<cel_int_t> hash_impl;
};

}  // namespace details

template <typename T>
size_t estimate_unique_value_count(std::span<const T> data, const legacy_embedded_ctl::bitset_view_t null_flags,
                                   size_t block_size) {
  std::random_device rd{};
  std::mt19937 mt{rd()};
  std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max());
  // We randomize the seed to increase the protection against adversarial data sets
  const uint64_t seed{dist(mt)};
  const details::hyperloglog_hash<T> hasher{seed};

  tbb::enumerable_thread_specific<ctl::hyperloglog> thread_local_hyperloglogs{};
  tbb::blocked_range<size_t> range{0, data.size(), block_size};

  tbb::parallel_for(range, [&hasher, &data, &null_flags, &thread_local_hyperloglogs](const auto& range) {
    auto& hyperloglog{thread_local_hyperloglogs.local()};

    null_flags.apply_on_unset_in_range(
        [&hasher, &hyperloglog, &data](size_t row) {
          const auto hash{hasher(data[row])};
          hyperloglog.insert(hash);
        },
        range.begin(), range.end());
  });

  ctl::hyperloglog global_hll{};
  for (const auto& thread_local_hyperloglog : thread_local_hyperloglogs) {
    global_hll += thread_local_hyperloglog;
  }

  return global_hll.cardinality();
}

}  // namespace celonis::accelerator::memory
