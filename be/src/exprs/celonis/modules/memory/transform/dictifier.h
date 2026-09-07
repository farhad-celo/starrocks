#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <utility>

#include <ctl/bitset.h>

#include "modules/common/execution_context_fwd.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/dictionary_fwd.h"
#include "modules/memory/raw_dictionary.h"
#include "modules/memory/row_id.h"

/**
 * Materialized data is here converted to dictionary data
 */

namespace celonis::accelerator::memory::transform {

struct raw_dictionary_and_pointers {
  raw_dictionary_t dictionary;
  raw_column_ptrs_t column_pointers;

  [[nodiscard]] std::pair<dictionary_t, column_ptrs_t> to_swappable(const std::string& id,
                                                                    const std::string& description);
};

// Dictionary encode the given raw buffer. Accepted types are cel_int_t, cel_string_t, cel_float_t, cel_date_t, and
// cel_boolean_t. The description is used only for debug logs.
template <typename T>
[[nodiscard]] raw_dictionary_and_pointers dictify(std::span<const T> data, ctl::bitset_view_t null_flags,
                                                  const std::string& description, common::execution_context& context);

// Used internally to decide whether to use a sort or hash based strategy; exported so other users can do similar
// things.  Supported for non-bool data types and column pointer types.
template <typename T>
[[nodiscard]] size_t legacy_estimate_unique_value_count(std::span<const T> data, ctl::bitset_view_t null_flags,
                                                        const common::execution_context& context);

// For white-box testing.
namespace details {
// Implementation of dictify for a given unique value estimate
template <typename T>
[[nodiscard]] raw_dictionary_and_pointers dictify_impl(std::span<const T> data, ctl::bitset_view_t null_flags,
                                                       size_t null_flags_count, size_t estimated_unique_value_count,
                                                       const std::string& description,
                                                       common::execution_context& context, common::timer& timer);

// Dictionary encodes by sorting the data and removing duplicates. Chosen for small inputs and when many distinct
// elements are expected.  Accepts the same types as dictify except for bool.
template <typename T>
[[nodiscard]] raw_dictionary_and_pointers dictify_sort(std::span<const T> data, ctl::bitset_view_t null_flags,
                                                       row_id block_size, const common::execution_context& context);

// Dictionary encodes by hashing to calculate distinct elements before sorting. Chosen if few distinct elements are
// expected.  Accepts the same types as dictify except for bool.
template <typename T>
[[nodiscard]] raw_dictionary_and_pointers dictify_hash(std::span<const T> data, size_t estimated_unique_value_count,
                                                       ctl::bitset_view_t null_flags, size_t block_size,
                                                       const common::execution_context& context);

}  // namespace details

}  // namespace celonis::accelerator::memory::transform
