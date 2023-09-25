#pragma once

#include <cstddef>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/enumerable_thread_specific.h>
#include <oneapi/tbb/parallel_for.h>
#include <span>
#include <string_view>

#include "ctl/hash.h"
#include "ctl/hyperloglog.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/memory/null_flags.h"

namespace celonis::accelerator::memory {

namespace details {

template <typename T>
struct hyperloglog_hash {
  size_t operator()(const T& data) const {
    return ctl::hash_murmur_64a(std::string_view{reinterpret_cast<const char*>(&data), sizeof(data)});
  }
};

template <>
struct hyperloglog_hash<cel_string_t> {
  size_t operator()(const cel_string_t& data) const { return ctl::hash_murmur_64a(data); }
};

}  // namespace details

template <typename T>
size_t estimate_unique_value_count(std::span<const T> data, const null_flags_bitset_t& null_flags, size_t block_size) {
  const details::hyperloglog_hash<T> hasher{};

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