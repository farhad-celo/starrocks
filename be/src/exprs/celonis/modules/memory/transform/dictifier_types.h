#pragma once

#include <compare>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <boost/dynamic_bitset/dynamic_bitset.hpp>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/hash.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::memory::transform::details {
struct dictify_work_item final {
  row_id start;
  row_id end;
  row_id value_count;
  row_id start_value_count;
};

// works like a string slice; Is used as the key of the hash map
struct cel_string_key {
 public:
  struct hash {
    size_t operator()(const cel_string_key& key) const { return legacy_embedded_ctl::hash_murmur_64a(key.str_without_null_byte()); }
  };

  cel_string_key() = default;
  cel_string_key(cel_string_t str, size_t len) : str_{str, len} { legacy_embedded_debug_assert(str_.ends_with('\0')); }
  explicit cel_string_key(cel_string_t str) : cel_string_key{str, std::strlen(str) + 1} {}

  // NOLINTNEXTLINE(modernize-use-nullptr)
  [[nodiscard]] auto operator<=>(const cel_string_key&) const = default;

  [[nodiscard]] size_t str_len_with_null_byte() const { return str_.length(); }
  [[nodiscard]] cel_string_t str() const { return str_.data(); }

  [[nodiscard]] std::string_view str_without_null_byte() const {
    if (str_.empty()) {
      return str_;
    }
    legacy_embedded_debug_assert(str_.ends_with('\0'));
    return str_.substr(0, str_.length() - 1);
  }

 private:
  // N.B.: This string_view also contains the null byte (if not constructed by default ctor)
  std::string_view str_{};
};

// contains information about the distinct elements in a column
// returned by scan_sorted_data()
struct distinct_element_data {
  row_id total_distinct_element_count;
  size_t total_buffer_size;

  // contains information about the distinct elements in a block with respect to previous blocks and the NULL element!
  // e.g. the first block will always have a distinct_element_offset of 1 since the NULL element appears first in the
  // dictionary
  struct block_data {
    row_id distinct_element_offset{0};
    row_id distinct_element_count{0};

    size_t byte_buffer_offset{0};
    size_t buffer_size{0};

    // Bit n is true if the n-th element in this block is a distinct element. Set by scan_sorted_data
    boost::dynamic_bitset<> distinct_item_marker{};
  };

  std::vector<block_data> block_info;
};

}  // namespace celonis::accelerator::memory::transform::details
