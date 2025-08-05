#pragma once

#include <memory>
#include <variant>

#include "legacy_embedded_ctl/array_view.h"
#include "legacy_embedded_ctl/static_array.h"
#include "modules/common/execution_context.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/memory/merge_dictionaries.h"
#include "modules/memory/row_id.h"
#include "modules/memory/typed_dictionary.h"

namespace celonis::accelerator::memory::details {

constexpr size_t NULL_ELEMENT_SIZE{1};

template <typename T>
struct input_partition {
  T min;
  T max;
  legacy_embedded_ctl::array_view<const T> view;
  size_t original_dictionary_idx;
  size_t original_dictionary_offset;
};

template <typename T>
class disjunct_partition : public legacy_embedded_ctl::array_view<const input_partition<T>> {
  using base = legacy_embedded_ctl::array_view<const input_partition<T>>;
  using base::base;

 public:
  [[nodiscard]] bool is_mergeable() const { return base::size() > 1; }
};

template <typename T>
class result_partition {
 public:
  result_partition() = default;

  explicit result_partition(legacy_embedded_ctl::array_view<const T> dictionary_view) : dictionary_view_{dictionary_view} {};

  result_partition(legacy_embedded_ctl::array_view<const T> dictionary_view, std::vector<legacy_embedded_ctl::array_view<const row_id>>&& mapping_views)
      : dictionary_view_{dictionary_view}, mapping_views_{std::move(mapping_views)} {};

  [[nodiscard]] const legacy_embedded_ctl::array_view<const T>& dictionary_view() const { return dictionary_view_; }

  [[nodiscard]] const std::vector<legacy_embedded_ctl::array_view<const row_id>>& mapping_views() const { return mapping_views_; }

  [[nodiscard]] bool has_mappings() const { return !mapping_views_.empty(); }

 private:
  legacy_embedded_ctl::array_view<const T> dictionary_view_;
  std::vector<legacy_embedded_ctl::array_view<const row_id>> mapping_views_;
};

template <typename T>
struct dictionary_data {
  legacy_embedded_ctl::static_array<T> dictionary;
};

template <>
struct dictionary_data<cel_string_t> {
  legacy_embedded_ctl::static_array<cel_string_t> dictionary;
  legacy_embedded_ctl::static_array<char> dictionary_buffer;
};

template <typename T>
struct merged_partition_data {
  dictionary_data<T> dict_data;
  std::vector<legacy_embedded_ctl::static_array<row_id>> mappings;
};

template <typename T>
struct merge_results {
  legacy_embedded_ctl::static_array<result_partition<T>> result_partitions;
  legacy_embedded_ctl::static_array<merged_partition_data<T>> merge_data;
};

template <typename T>
struct sizes_and_offsets {
  legacy_embedded_ctl::static_array<size_t> partition_sizes{};
  legacy_embedded_ctl::static_array<size_t> partition_offsets{};
};

template <>
struct sizes_and_offsets<cel_string_t> {
  legacy_embedded_ctl::static_array<size_t> partition_sizes{};
  legacy_embedded_ctl::static_array<size_t> partition_offsets{};
  legacy_embedded_ctl::static_array<size_t> buffer_sizes{};
  legacy_embedded_ctl::static_array<size_t> buffer_offsets{};
};

struct flattened_partition {
  size_t disjunct_partitions_i{};
  size_t disjunct_partitions_j{};

  // For testing
  auto operator<=>(const flattened_partition&) const = default;
};

struct non_null_dictionary {
  dictionary_t dict;
  std::string dict_name;
};

template <typename T>
[[nodiscard]] std::vector<typed_dictionary<T>*> to_typed_dictionaries(
    const std::vector<non_null_dictionary>& dictionaries, const std::string& op_name);

template <typename T>
[[nodiscard]] size_t get_cross_dictionary_size_until_pivot(const std::vector<input_partition<T>>& input_partitions,
                                                           T pivot);

template <typename T>
[[nodiscard]] T find_pivot(const std::vector<input_partition<T>>& input_partitions, size_t granularity,
                           size_t acceptable_granularity_diff);

/**
 * Partition the input dictionaries by finding pivot elements so that the cross-dictionary sizes are below a certain
 * threshold (= granularity). Pivot elements are found by using binary search on exactly that condition.
 * The search exits early if using a pivot element would yield a cross-partition size difference of at maximum
 * granularity_diff_factor percent compared to the given granularity.
 */
template <typename T>
[[nodiscard]] std::vector<input_partition<T>> partition(
    const std::vector<management::const_data_accessor<T>>& dictionary_accessors, size_t granularity = 1 << 17,
    double granularity_diff_factor = 0.1);

/**
 * Group the partitions (by finding overlaps) into disjunct partitions. Partitions without any overlap to another
 * partition do not have to be merged, but can be copied into the output dictionary later. Each disjunct
 * partition (consisting of multiple input dictionary partitions) will be merged in parallel in the merge step.
 */
template <typename T>
[[nodiscard]] std::vector<disjunct_partition<T>> group(std::vector<input_partition<T>>& input_partitions);

/**
 * Sequential merge of two dictionaries. In contrast to the method below, this method does not use a heap, but makes use
 * of the fact that only two dictionaries are merged.
 */
template <typename T>
[[nodiscard]] merged_partition_data<T> merge_2_dictionary_partitions(const legacy_embedded_ctl::array_view<const T>& lhs,
                                                                     const legacy_embedded_ctl::array_view<const T>& rhs,
                                                                     const common::execution_context& context);

/**
 * Sequential merge of n dictionaries. This merge uses a heap.
 */
template <class T>
[[nodiscard]] merged_partition_data<T> merge_n_dictionary_partitions(
    const std::vector<legacy_embedded_ctl::array_view<const T>>& dict_views, const common::execution_context& context);

/**
 * Merge the partitions which are overlapping. This uses the old, sequential algorithm, but does this in parallel for
 * every disjunct partition.
 */
template <typename T>
[[nodiscard]] merge_results<T> merge(const std::vector<disjunct_partition<T>>& disjunct_partitions,
                                     const common::execution_context& context);

/**
 * Calculate sizes and offsets of the partitions so that the results can be copied into the output dictionary and output
 * mappings later.
 */
template <typename T>
[[nodiscard]] sizes_and_offsets<T> calculate_sizes_and_offsets(
    const legacy_embedded_ctl::static_array<result_partition<T>>& result_partitions);

/**
 * Flattens the disjunct_partitions data structure. This is needed because we need to create an output mapping for every
 * dictionary later.
 */
template <typename T>
[[nodiscard]] legacy_embedded_ctl::static_array<flattened_partition> flatten(
    size_t num_input_partitions, const std::vector<disjunct_partition<T>>& disjunct_partitions);

/**
 * Copies the merged and unmerged dictionary partitions into the output dictionary using the sizes and offsets. For
 * unmerged partitions (= the ones that did not overlap with any other partition), the input is copied.
 */
template <typename T>
[[nodiscard]] dictionary_data<T> copy_dictionary_partitions(
    const legacy_embedded_ctl::static_array<result_partition<T>>& result_partitions, const sizes_and_offsets<T>& sizes_and_offsets,
    common::execution_context& context);

/**
 * Copies the mapping partitions into the output mappings using the sizes and offsets. For partitions that were not
 * merged, no mapping is available from the sequential algorithms, so the mapping is generated.
 */
template <typename T>
[[nodiscard]] legacy_embedded_ctl::static_array<legacy_embedded_ctl::static_array<row_id>> copy_mapping_partitions(
    const std::vector<typed_dictionary<T>*>& typed_dictionaries,
    const std::vector<disjunct_partition<T>>& disjunct_partitions,
    const legacy_embedded_ctl::static_array<result_partition<T>>& result_partitions, const legacy_embedded_ctl::static_array<size_t>& partition_offsets,
    const legacy_embedded_ctl::static_array<flattened_partition>& flattened_partitions, common::execution_context& context);

template <typename T>
[[nodiscard]] std::pair<raw_dictionary_t, legacy_embedded_ctl::static_array<legacy_embedded_ctl::static_array<row_id>>>
construct_and_merge_typed_dictionaries(const std::vector<non_null_dictionary>& dictionaries, const std::string& op_name,
                                       common::execution_context& context);

[[nodiscard]] merge_result merge_n_dictionaries_raw_internal(const std::vector<non_null_dictionary>& non_null_dict,
                                                             const std::string& op_name,
                                                             common::execution_context& context);
}  // namespace celonis::accelerator::memory::details
