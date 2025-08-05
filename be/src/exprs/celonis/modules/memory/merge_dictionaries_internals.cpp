#include "merge_dictionaries_internals.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <numeric>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <optional>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include "legacy_embedded_ctl/static_array.h"
#include "legacy_embedded_ctl/utility.h"
#include "modules/common/buffer_types.h"
#include "modules/common/date/celonis_date_storage.h"
#include "modules/common/exceptions.h"
#include "modules/common/shared_types.h"
#include "modules/memory/management/memory_checked_containers.h"
#include "modules/memory/merge_dictionaries.h"
#include "modules/memory/merge_dictionaries_internals.h"
#include "modules/memory/raw_dictionary.h"
#include "modules/memory/tracking/char_buffer_with_context_tracking.h"
#include "modules/memory/tracking/static_array_with_context_tracking.h"
#include "modules/memory/typed_dictionary.h"

namespace celonis::accelerator::memory {

namespace {

constexpr legacy_embedded_ctl::overloaded<std::less<cel_int_t>, std::less<cel_float_t>, std::less<cel_date_t>, cel_string_compare>
    dictionary_cmp{};

struct heap_merge_next {
  row_id offset{0};
  int dict_index{0};
};

template <class T>
struct merge_type_traits {
  memory::management::checked_vector_t<T> output;

  explicit merge_type_traits(const common::execution_context& context)
      : output(accelerator::memory::management::checked_allocator<decltype(output)>(
            context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG))) {}

  inline void push_back_output(const T& val) { output.push_back(val); }

  [[nodiscard]] inline size_t get_output_size() const { return output.size(); }

  details::dictionary_data<T> finalize(const common::execution_context& context) {
    auto output_array{memory::tracking::make_static_array_for_overwrite<T>(
        output.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};
    std::copy(output.begin(), output.end(), output_array.begin());

    return {std::move(output_array)};
  }

  struct cmp_data {
    const std::vector<typename legacy_embedded_ctl::array_view<const T>>& data;
    inline bool operator()(const heap_merge_next& left, const heap_merge_next& right) const {
      return data[left.dict_index][left.offset] > data[right.dict_index][right.offset];
    }
  };

  struct eq_data {
    const std::vector<typename legacy_embedded_ctl::array_view<const T>>& data;
    inline bool operator()(const heap_merge_next& left, const heap_merge_next& right) const {
      return data[left.dict_index][left.offset] == data[right.dict_index][right.offset];
    }
  };
};

template <>
struct merge_type_traits<cel_string_t> {
  memory::management::checked_vector_t<size_t> output;
  common::char_buffer string_buf{};
  size_t cur_offset{0};

  explicit merge_type_traits(const common::execution_context& context)
      : output(accelerator::memory::management::checked_allocator<decltype(output)>(
            context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG))),
        string_buf{memory::tracking::make_tracked_char_buffer(context)} {}

  inline void push_back_output(const cel_string_t& val) {
    size_t len_str = strlen(val);
    char* dest = string_buf.request(len_str + 1);
    std::copy_n(val, len_str + 1, dest);
    output.push_back(cur_offset);
    cur_offset += len_str + 1;
  }

  [[nodiscard]] inline size_t get_output_size() const { return output.size(); }

  details::dictionary_data<cel_string_t> finalize(const common::execution_context& context) {
    auto output_array{memory::tracking::make_static_array_for_overwrite<cel_string_t>(
        output.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};
    auto buffer{string_buf.reallocate_to_continuous_buffer()};
    cel_string_t buffer_begin{buffer.data()};

    row_id output_size = static_cast<row_id>(output.size());
    for (row_id i = 0; i < output_size; ++i) {
      output_array[i] = buffer_begin + output[i];
    }

    return {std::move(output_array), std::move(buffer)};
  }

  struct cmp_data {
    const std::vector<typename legacy_embedded_ctl::array_view<const cel_string_t>>& data;
    inline bool operator()(const heap_merge_next& left, const heap_merge_next& right) const {
      return std::strcmp(data[left.dict_index][left.offset], data[right.dict_index][right.offset]) > 0;
    }
  };

  struct eq_data {
    const std::vector<typename legacy_embedded_ctl::array_view<const cel_string_t>>& data;
    inline bool operator()(const heap_merge_next& left, const heap_merge_next& right) const {
      return std::strcmp(data[left.dict_index][left.offset], data[right.dict_index][right.offset]) == 0;
    }
  };
};

}  // namespace

namespace details {

template <typename T>
merged_partition_data<T> extend_dictionary_partition(const legacy_embedded_ctl::array_view<const T>& dict_view, const T& val,
                                                     const common::execution_context& context) {
  const size_t insert_at{
      legacy_embedded_ctl::cast<size_t>(std::distance(dict_view.begin(), std::ranges::lower_bound(dict_view, val, dictionary_cmp)))};
  const auto is_present{insert_at < dict_view.size() && !dictionary_cmp(val, dict_view[insert_at])};

  std::vector<legacy_embedded_ctl::static_array<row_id>> mappings{};
  mappings.reserve(2);
  mappings.emplace_back(memory::tracking::make_static_array_for_overwrite<row_id>(
      dict_view.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG), context));
  mappings.emplace_back(
      memory::tracking::make_static_array_for_overwrite<row_id>(1, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG), context));
  mappings.back().front() = insert_at;

  std::iota(std::begin(mappings.front()), std::next(std::begin(mappings.front()), insert_at), row_id{});

  merge_type_traits<T> output{context};
  for (size_t i{0}; i != insert_at; ++i) {
    output.push_back_output(dict_view[i]);
  }
  if (is_present) {
    std::iota(std::next(std::begin(mappings.front()), insert_at), std::end(mappings.front()), insert_at);
  } else {
    output.push_back_output(val);
    std::iota(std::next(std::begin(mappings.front()), insert_at), std::end(mappings.front()), insert_at + 1);
  }
  for (size_t i{insert_at}; i != dict_view.size(); ++i) {
    output.push_back_output(dict_view[i]);
  }

  return {std::move(output.finalize(context)), std::move(mappings)};
}

template <class T>
merged_partition_data<T> merge_n_dictionary_partitions(const std::vector<legacy_embedded_ctl::array_view<const T>>& dict_views,
                                                       const common::execution_context& context) {
  std::vector<row_id> dict_sizes;
  std::vector<legacy_embedded_ctl::static_array<row_id>> mapping;
  for (const auto& dict_view : dict_views) {
    const auto dict_size{dict_view.size()};
    dict_sizes.push_back(dict_size);
    mapping.emplace_back(memory::tracking::make_static_array_for_overwrite<row_id>(
        dict_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG), context));
  }

  // pair of (offset pointer, dictionary index)
  std::vector<heap_merge_next> heap;
  for (size_t dict_index{0}; dict_index < dict_sizes.size(); ++dict_index) {
    heap_merge_next h{0, static_cast<int>(dict_index)};
    heap.push_back(h);
  }

  merge_type_traits<T> traits{context};

  // required type-specific comparisons (heap-order, equivalence)
  typename merge_type_traits<T>::cmp_data cmp{dict_views};
  typename merge_type_traits<T>::eq_data eq_data{dict_views};

  // make heap out of heap vector
  std::make_heap(heap.begin(), heap.end(), cmp);

  if (!heap.empty()) {
    bool first{true};
    auto last_smallest{*heap.begin()};

    // while there is at least one unprocessed element
    while (!heap.empty()) {
      auto smallest{*heap.begin()};

      // if current value was not already pushed to the output dictionary
      if (first || !eq_data(last_smallest, smallest)) {
        traits.push_back_output(dict_views[smallest.dict_index][smallest.offset]);
        first = false;
      }

      // record how to map source col pointer to output col ptr
      mapping[smallest.dict_index][smallest.offset] = static_cast<row_id>(traits.get_output_size()) - 1;

      // "remove" top (just processed) element from heap - put it to back of heap vector
      std::pop_heap(heap.begin(), heap.end(), cmp);

      if (smallest.offset + 1 != dict_sizes[smallest.dict_index]) {
        // there more entries in the dictionary to process - increment offset and push it again to the heap
        heap.back().offset++;
        std::push_heap(heap.begin(), heap.end(), cmp);
      } else {
        // no more entries for this dictionary - remove from heap vector (it was just moved to the back)
        heap.pop_back();
      }
      last_smallest = smallest;
    }
  }

  return {std::move(traits.finalize(context)), std::move(mapping)};
}

template <typename T>
merged_partition_data<T> merge_2_dictionary_partitions(const legacy_embedded_ctl::array_view<const T>& lhs,
                                                       const legacy_embedded_ctl::array_view<const T>& rhs,
                                                       const common::execution_context& context) {
  if (rhs.size() == 1) {
    return extend_dictionary_partition(lhs, rhs[0], context);
  }
  if (lhs.size() == 1) {
    auto result{extend_dictionary_partition(rhs, lhs[0], context)};
    std::swap(result.mappings.front(), result.mappings.back());
    return result;
  }

  merge_type_traits<T> output{context};
  const auto* lhs_first{lhs.begin()};
  const auto* rhs_first{rhs.begin()};
  const auto* const lhs_last{lhs.end()};
  const auto* const rhs_last{rhs.end()};
  std::vector<legacy_embedded_ctl::static_array<row_id>> mappings{};
  mappings.reserve(2);
  mappings.emplace_back(
      memory::tracking::make_static_array_for_overwrite<row_id>(lhs.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG), context));
  mappings.emplace_back(
      memory::tracking::make_static_array_for_overwrite<row_id>(rhs.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG), context));
  row_id lhs_mapping_index{};
  row_id rhs_mapping_index{};
  while (lhs_first != lhs_last && rhs_first != rhs_last) {
    if (dictionary_cmp(*lhs_first, *rhs_first)) {
      mappings.front()[lhs_mapping_index++] = static_cast<row_id>(output.get_output_size());
      output.push_back_output(*lhs_first);
      ++lhs_first;
    } else if (dictionary_cmp(*rhs_first, *lhs_first)) {
      mappings.back()[rhs_mapping_index++] = static_cast<row_id>(output.get_output_size());
      output.push_back_output(*rhs_first);
      ++rhs_first;
    } else {
      mappings.front()[lhs_mapping_index++] = static_cast<row_id>(output.get_output_size());
      mappings.back()[rhs_mapping_index++] = static_cast<row_id>(output.get_output_size());
      output.push_back_output(*lhs_first);
      ++lhs_first;
      ++rhs_first;
    }
  }
  const auto current_size{output.get_output_size()};
  // we've exhausted at least one of the two dictionaries,
  // so we can simply append the remaining elements from the other dictionary (if there are any)
  std::iota(std::next(std::begin(mappings.front()), lhs_mapping_index), std::end(mappings.front()), current_size);
  std::for_each(lhs_first, lhs_last, [&output](const auto& x) { output.push_back_output(x); });
  std::iota(std::next(std::begin(mappings.back()), rhs_mapping_index), std::end(mappings.back()), current_size);
  std::for_each(rhs_first, rhs_last, [&output](const auto& x) { output.push_back_output(x); });
  return {std::move(output.finalize(context)), std::move(mappings)};
}

template <typename T>
std::vector<typed_dictionary<T>*> to_typed_dictionaries(const std::vector<non_null_dictionary>& dictionaries,
                                                        const std::string& op_name) {
  std::vector<typed_dictionary<T>*> typed_dictionaries;
  typed_dictionaries.reserve(dictionaries.size());
  std::transform(dictionaries.cbegin(), dictionaries.cend(), std::back_inserter(typed_dictionaries),
                 [&op_name, &dictionaries](const auto& non_null_dict) {
                   static constexpr data_type DATA_TYPE_VAL{get_matching_data_type<T>()};
                   const auto& dict{non_null_dict.dict};
                   const auto& dict_name{non_null_dict.dict_name};
                   if (dict->type != DATA_TYPE_VAL) {
                     throw common::cpm_exception{
                         "{}: Cannot merge column {} of type {} with column {} of different type {}.",
                         op_name,
                         dictionaries[0].dict_name,
                         convert_to_string(DATA_TYPE_VAL),
                         dict_name,
                         convert_to_string(dict->type)};
                   }
                   return dynamic_cast<typed_dictionary<T>*>(dict.get());
                 });
  return typed_dictionaries;
}

template std::vector<typed_dictionary<cel_int_t>*> to_typed_dictionaries<cel_int_t>(
    const std::vector<non_null_dictionary>&, const std::string&);
template std::vector<typed_dictionary<cel_float_t>*> to_typed_dictionaries<cel_float_t>(
    const std::vector<non_null_dictionary>&, const std::string&);
template std::vector<typed_dictionary<cel_date_t>*> to_typed_dictionaries<cel_date_t>(
    const std::vector<non_null_dictionary>&, const std::string&);
template std::vector<typed_dictionary<cel_string_t>*> to_typed_dictionaries<cel_string_t>(
    const std::vector<non_null_dictionary>&, const std::string&);

template <typename T>
std::vector<management::const_data_accessor<T>> get_dictionary_accessors(const std::vector<typed_dictionary<T>*>& dicts,
                                                                         const common::execution_context& context) {
  std::vector<management::const_data_accessor<T>> dict_accessors;
  dict_accessors.reserve(dicts.size());
  std::ranges::transform(dicts, std::back_inserter(dict_accessors),
                         [&context](const auto& dict) { return dict->get_const_data(context); });

  return dict_accessors;
}

template <typename T>
size_t get_cross_dictionary_size_until_pivot(const std::vector<input_partition<T>>& input_partitions, T pivot) {
  size_t cross_partition_size{0};
  for (const auto& partition : input_partitions) {
    const auto* lower_bound{std::ranges::lower_bound(partition.view, pivot, dictionary_cmp)};
    const auto size_until_pivot{lower_bound - partition.view.begin()};
    cross_partition_size += size_until_pivot;
  }
  return cross_partition_size;
}

template size_t get_cross_dictionary_size_until_pivot(const std::vector<input_partition<cel_int_t>>&, cel_int_t pivot);
template size_t get_cross_dictionary_size_until_pivot(const std::vector<input_partition<cel_float_t>>&,
                                                      cel_float_t pivot);
template size_t get_cross_dictionary_size_until_pivot(const std::vector<input_partition<cel_date_t>>&,
                                                      cel_date_t pivot);
template size_t get_cross_dictionary_size_until_pivot(const std::vector<input_partition<cel_string_t>>&,
                                                      cel_string_t pivot);

template <typename T>
T find_pivot(const std::vector<input_partition<T>>& input_partitions, const size_t granularity,
             const size_t acceptable_granularity_diff) {
  size_t current_pivot_diff{std::numeric_limits<size_t>::max()};
  T current_pivot{};

  for (const auto& partition : input_partitions) {
    const auto* right_pivot{std::ranges::lower_bound(
        partition.view, granularity, std::ranges::less{},
        std::bind_front(get_cross_dictionary_size_until_pivot<T>, std::ref(input_partitions)))};

    const auto* left_pivot{right_pivot - 1};

    size_t right_pivot_diff{std::numeric_limits<size_t>::max()};
    if (right_pivot != partition.view.end()) {
      right_pivot_diff = get_cross_dictionary_size_until_pivot(input_partitions, *right_pivot) - granularity;
    }

    size_t left_pivot_diff{std::numeric_limits<size_t>::max()};
    if (right_pivot != partition.view.begin()) {
      left_pivot_diff = granularity - get_cross_dictionary_size_until_pivot(input_partitions, *left_pivot);
    }

    if (left_pivot_diff <= right_pivot_diff) {
      if (left_pivot_diff < current_pivot_diff) {
        current_pivot = *left_pivot;
        current_pivot_diff = left_pivot_diff;
      }
    } else {
      if (right_pivot_diff < current_pivot_diff) {
        current_pivot = *right_pivot;
        current_pivot_diff = right_pivot_diff;
      }
    }

    // Early exit when we have found a pivot element which would produce a good cross-partition size.
    if (current_pivot_diff <= acceptable_granularity_diff) {
      return current_pivot;
    }
  }

  // In this case, we haven't found a "good" pivot element, but return just the best one that we have found.
  return current_pivot;
}

template cel_int_t find_pivot<cel_int_t>(const std::vector<input_partition<cel_int_t>>&, const size_t, const size_t);
template cel_float_t find_pivot<cel_float_t>(const std::vector<input_partition<cel_float_t>>&, const size_t,
                                             const size_t);
template cel_date_t find_pivot<cel_date_t>(const std::vector<input_partition<cel_date_t>>&, const size_t, const size_t);
template cel_string_t find_pivot<cel_string_t>(const std::vector<input_partition<cel_string_t>>&, const size_t,
                                               const size_t);

template <typename T>
std::vector<input_partition<T>> split_partitions(const std::vector<input_partition<T>>& input_partitions,
                                                 const size_t granularity, const size_t acceptable_granularity_diff) {
  size_t total_size{0};
  for (const auto& partition : input_partitions) {
    total_size += partition.view.size();
  }

  // Exit condition for the recursion.
  if (total_size <= granularity + acceptable_granularity_diff) {
    return input_partitions;
  }

  const T pivot{find_pivot(input_partitions, granularity, acceptable_granularity_diff)};
  std::vector<input_partition<T>> left_partitions;
  std::vector<input_partition<T>> right_partitions;

  for (const auto& partition : input_partitions) {
    const auto& view{partition.view};
    const auto* lower_bound{std::ranges::lower_bound(view, pivot, dictionary_cmp)};

    const auto split_idx{lower_bound - view.begin()};
    if (lower_bound != view.begin()) {
      auto left_view{view.sub_view(0, split_idx)};
      left_partitions.push_back({left_view.front(), left_view.back(), left_view, partition.original_dictionary_idx,
                                 partition.original_dictionary_offset});
    }

    if (lower_bound != view.end()) {
      auto right_view{view.sub_view(split_idx, view.size() - split_idx)};
      right_partitions.push_back({right_view.front(), right_view.back(), right_view, partition.original_dictionary_idx,
                                  partition.original_dictionary_offset + split_idx});
    }
  }

  auto right_split{split_partitions(right_partitions, granularity, acceptable_granularity_diff)};
  left_partitions.insert(left_partitions.end(), right_split.begin(), right_split.end());

  return left_partitions;
}

template <typename T>
std::vector<input_partition<T>> partition(const std::vector<management::const_data_accessor<T>>& dictionary_accessors,
                                          const size_t granularity, const double granularity_diff_factor) {
  std::vector<input_partition<T>> input_partitions;

  for (size_t dict_index{0}; dict_index < dictionary_accessors.size(); ++dict_index) {
    const auto& dictionary_accessor{dictionary_accessors[dict_index]};

    const size_t offset{details::NULL_ELEMENT_SIZE};  // skip the NULL element of each dictionary
    const size_t length{dictionary_accessor.size() - offset};
    auto view{legacy_embedded_ctl::array_view<const T>{&dictionary_accessor.get()[offset], length}};

    input_partitions.push_back({view.front(), view.back(), view, dict_index, offset});
  }

  const size_t acceptable_granularity_diff{
      static_cast<size_t>(granularity_diff_factor * static_cast<double>(granularity))};
  return split_partitions(input_partitions, granularity, acceptable_granularity_diff);
}

template std::vector<input_partition<cel_int_t>> partition(
    const std::vector<management::const_data_accessor<cel_int_t>>&, const size_t, const double);
template std::vector<input_partition<cel_float_t>> partition(
    const std::vector<management::const_data_accessor<cel_float_t>>&, const size_t, const double);
template std::vector<input_partition<cel_date_t>> partition(
    const std::vector<management::const_data_accessor<cel_date_t>>&, const size_t, const double);
template std::vector<input_partition<cel_string_t>> partition(
    const std::vector<management::const_data_accessor<cel_string_t>>&, const size_t, const double);

template <typename T>
std::vector<disjunct_partition<T>> group(std::vector<input_partition<T>>& input_partitions) {
  std::ranges::sort(input_partitions, dictionary_cmp, &input_partition<T>::min);

  std::vector<disjunct_partition<T>> disjunct_partitions;

  size_t first{0};
  size_t last{0};
  T max{input_partitions.at(last).max};
  for (size_t i{1}; i < input_partitions.size(); i++) {
    const T current_min{input_partitions[i].min};

    if (dictionary_cmp(max, current_min)) {
      disjunct_partitions.push_back({&input_partitions[first], &input_partitions[last] + 1});

      first = i;
      last = i;
    } else {
      ++last;
    }

    max = std::ranges::max(max, input_partitions[i].max, dictionary_cmp);
  }

  disjunct_partitions.push_back({&input_partitions[first], &input_partitions[last] + 1});

  return disjunct_partitions;
}

template std::vector<disjunct_partition<cel_int_t>> group(std::vector<input_partition<cel_int_t>>&);
template std::vector<disjunct_partition<cel_float_t>> group(std::vector<input_partition<cel_float_t>>&);
template std::vector<disjunct_partition<cel_date_t>> group(std::vector<input_partition<cel_date_t>>&);
template std::vector<disjunct_partition<cel_string_t>> group(std::vector<input_partition<cel_string_t>>&);

template <typename T>
merge_results<T> merge(const std::vector<disjunct_partition<T>>& disjunct_partitions,
                       const common::execution_context& context) {
  auto merge_data_indices{legacy_embedded_ctl::make_static_array_for_overwrite<std::optional<size_t>>(
      disjunct_partitions.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG))};

  size_t merge_data_size{0};
  for (size_t disjunct_partitions_index{0}; disjunct_partitions_index < disjunct_partitions.size();
       ++disjunct_partitions_index) {
    if (disjunct_partitions[disjunct_partitions_index].is_mergeable()) {
      merge_data_indices[disjunct_partitions_index] = merge_data_size;
      ++merge_data_size;
    } else {
      merge_data_indices[disjunct_partitions_index] = std::nullopt;
    }
  }

  auto result_partitions{legacy_embedded_ctl::make_static_array_for_overwrite<result_partition<T>>(
      disjunct_partitions.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG))};
  auto merge_data{legacy_embedded_ctl::make_static_array_for_overwrite<merged_partition_data<T>>(
      merge_data_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG))};

  tbb::parallel_for(
      size_t{0}, disjunct_partitions.size(),
      [&result_partitions, &merge_data, &merge_results_indices = std::as_const(merge_data_indices),
       &disjunct_partitions = std::as_const(disjunct_partitions), &context](const size_t disjunct_partition_index) {
        if (disjunct_partitions[disjunct_partition_index].is_mergeable()) {
          std::vector<legacy_embedded_ctl::array_view<const T>> views;
          for (const auto& partition : disjunct_partitions[disjunct_partition_index]) {
            views.push_back(partition.view);
          }

          merged_partition_data<T> merge_result;
          if (views.size() == 2) {
            // Use an optimized version for the merging algorithm, which does not use a heap in case of
            // only two dictionaries
            merge_result = merge_2_dictionary_partitions<T>(views[0], views[1], context);
          } else {
            merge_result = merge_n_dictionary_partitions<T>(views, context);
          }

          const size_t merge_data_index{merge_results_indices[disjunct_partition_index].value()};
          merge_data[merge_data_index] = std::move(merge_result);

          // Create views for the results of the merge
          std::vector<legacy_embedded_ctl::array_view<const row_id>> mapping_views;
          for (const auto& mapping : merge_data[merge_data_index].mappings) {
            mapping_views.emplace_back(mapping);
          }
          legacy_embedded_ctl::array_view<const T> dictionary_view{merge_data[merge_data_index].dict_data.dictionary};

          result_partitions[disjunct_partition_index] = result_partition<T>{dictionary_view, std::move(mapping_views)};
        } else {
          // No merge happened, just use the initial view for further processing
          const auto& input_partition{disjunct_partitions[disjunct_partition_index].front()};

          // TODO(o.layer): Pass 'std::ranges::iota_view' and remove special handling in 'copy_mapping_partitions'
          // once clang supports it.
          result_partitions[disjunct_partition_index] = result_partition<T>{input_partition.view};
        }
      });

  return {std::move(result_partitions), std::move(merge_data)};
}

template merge_results<cel_int_t> merge(const std::vector<disjunct_partition<cel_int_t>>&,
                                        const common::execution_context&);
template merge_results<cel_float_t> merge(const std::vector<disjunct_partition<cel_float_t>>&,
                                          const common::execution_context&);
template merge_results<cel_date_t> merge(const std::vector<disjunct_partition<cel_date_t>>&,
                                         const common::execution_context&);
template merge_results<cel_string_t> merge(const std::vector<disjunct_partition<cel_string_t>>&,
                                           const common::execution_context&);

template <typename T>
sizes_and_offsets<T> calculate_sizes_and_offsets(const legacy_embedded_ctl::static_array<result_partition<T>>& result_partitions) {
  sizes_and_offsets<T> result;
  auto& partition_sizes{result.partition_sizes};
  auto& partition_offsets{result.partition_offsets};

  const auto num_sizes{result_partitions.size()};
  const auto num_offsets{result_partitions.size() + NULL_ELEMENT_SIZE};

  partition_sizes = legacy_embedded_ctl::make_static_array_for_overwrite<size_t>(num_sizes, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG));
  partition_offsets = legacy_embedded_ctl::make_static_array_for_overwrite<size_t>(num_offsets, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG));
  partition_offsets[0] = NULL_ELEMENT_SIZE;

  if constexpr (std::is_same_v<T, cel_string_t>) {
    result.buffer_sizes =
        legacy_embedded_ctl::make_static_array_for_overwrite<size_t>(num_sizes, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG));
    result.buffer_offsets =
        legacy_embedded_ctl::make_static_array_for_overwrite<size_t>(num_offsets, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG));
    result.buffer_offsets[0] = NULL_STRING.size();
  }

  for (size_t result_partition_index{0}; result_partition_index < result_partitions.size(); ++result_partition_index) {
    const auto& partition_dictionary_view{result_partitions[result_partition_index].dictionary_view()};
    partition_sizes[result_partition_index] = partition_dictionary_view.size();

    if constexpr (std::is_same_v<T, cel_string_t>) {
      // TODO(o.layer): Make use of the fact that we know the buffer sizes of merged partitions (and could therefore
      // omit the loop below for merged partitions) and benchmark the performance difference.
      size_t buffer_size{0};
      for (cel_string_t element : partition_dictionary_view) {
        buffer_size += std::strlen(element) + 1;  // including null termination byte
      }

      result.buffer_sizes[result_partition_index] = buffer_size;
    }

    // Calculate the offset(s)
    partition_offsets[result_partition_index + NULL_ELEMENT_SIZE] =
        partition_offsets[result_partition_index] + partition_sizes[result_partition_index];

    if constexpr (std::is_same_v<T, cel_string_t>) {
      result.buffer_offsets[result_partition_index + NULL_ELEMENT_SIZE] =
          result.buffer_offsets[result_partition_index] + result.buffer_sizes[result_partition_index];
    }
  }

  return result;
}

template sizes_and_offsets<cel_int_t> calculate_sizes_and_offsets<cel_int_t>(
    const legacy_embedded_ctl::static_array<result_partition<cel_int_t>>&);
template sizes_and_offsets<cel_float_t> calculate_sizes_and_offsets<cel_float_t>(
    const legacy_embedded_ctl::static_array<result_partition<cel_float_t>>&);
template sizes_and_offsets<cel_date_t> calculate_sizes_and_offsets<cel_date_t>(
    const legacy_embedded_ctl::static_array<result_partition<cel_date_t>>&);
template sizes_and_offsets<cel_string_t> calculate_sizes_and_offsets<cel_string_t>(
    const legacy_embedded_ctl::static_array<result_partition<cel_string_t>>&);

template <typename T>
legacy_embedded_ctl::static_array<flattened_partition> flatten(const size_t num_input_partitions,
                                               const std::vector<disjunct_partition<T>>& disjunct_partitions) {
  auto flattened_partitions{legacy_embedded_ctl::make_static_array_for_overwrite<flattened_partition>(
      num_input_partitions, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG))};

  for (size_t disjunct_partition_i{0}, flattened_partition_index{0}; disjunct_partition_i < disjunct_partitions.size();
       ++disjunct_partition_i) {
    for (size_t disjunct_partition_j{0}; disjunct_partition_j < disjunct_partitions[disjunct_partition_i].size();
         ++disjunct_partition_j) {
      flattened_partitions[flattened_partition_index] = {disjunct_partition_i, disjunct_partition_j};
      ++flattened_partition_index;
    }
  }

  return flattened_partitions;
}

template legacy_embedded_ctl::static_array<flattened_partition> flatten<cel_int_t>(const size_t,
                                                                   const std::vector<disjunct_partition<cel_int_t>>&);
template legacy_embedded_ctl::static_array<flattened_partition> flatten<cel_float_t>(
    const size_t, const std::vector<disjunct_partition<cel_float_t>>&);
template legacy_embedded_ctl::static_array<flattened_partition> flatten<cel_date_t>(const size_t,
                                                                    const std::vector<disjunct_partition<cel_date_t>>&);
template legacy_embedded_ctl::static_array<flattened_partition> flatten<cel_string_t>(
    const size_t, const std::vector<disjunct_partition<cel_string_t>>&);

template <typename T>
void copy_dictionary_partition(const legacy_embedded_ctl::array_view<const T>& partition_view,
                               const sizes_and_offsets<T>& sizes_and_offsets, const size_t result_partitions_index,
                               dictionary_data<T>& result) {
  const size_t partition_offset{sizes_and_offsets.partition_offsets[result_partitions_index]};
  std::ranges::copy(partition_view, result.dictionary.begin() + partition_offset);
}

template <>
void copy_dictionary_partition(const legacy_embedded_ctl::array_view<const cel_string_t>& partition_view,
                               const sizes_and_offsets<cel_string_t>& sizes_and_offsets,
                               const size_t result_partitions_index, dictionary_data<cel_string_t>& result) {
  const size_t buffer_offset{sizes_and_offsets.buffer_offsets[result_partitions_index]};
  char* output_buffer_ptr{&result.dictionary_buffer[0] + buffer_offset};
  const size_t output_dict_offset{sizes_and_offsets.partition_offsets[result_partitions_index]};

  for (size_t i{0}; i < partition_view.size(); ++i) {
    // We must use strlen here, as we can not assume that the input string buffer is sorted. Else we could
    // just use the pointer difference in the buffer to infer the length of the string.
    // TODO(o.layer): Make use of the above mentioned optimization once we ensure string buffers are always sorted.
    const size_t buffer_entry_length{std::strlen(partition_view[i]) + 1};  // including null termination byte

    std::memcpy(output_buffer_ptr, partition_view[i], buffer_entry_length);
    result.dictionary[output_dict_offset + i] = output_buffer_ptr;

    output_buffer_ptr += buffer_entry_length;
  }
}

template <typename T>
dictionary_data<T> copy_dictionary_partitions(const legacy_embedded_ctl::static_array<result_partition<T>>& result_partitions,
                                              const sizes_and_offsets<T>& sizes_and_offsets,
                                              common::execution_context& context) {
  dictionary_data<T> result;

  const size_t output_dictionary_size{sizes_and_offsets.partition_offsets.back()};
  result.dictionary = memory::tracking::make_static_array_for_overwrite<T>(output_dictionary_size,
                                                                           LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG), context);

  if constexpr (std::is_same_v<T, cel_string_t>) {
    const size_t output_buffer_size{std::accumulate(sizes_and_offsets.buffer_sizes.begin(),
                                                    sizes_and_offsets.buffer_sizes.end(), NULL_STRING.size())};
    result.dictionary_buffer = memory::tracking::make_static_array_for_overwrite<char>(
        output_buffer_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG), context);

    std::strncpy(result.dictionary_buffer.begin(), NULL_STRING.data(), NULL_STRING.size());
    result.dictionary[0] = result.dictionary_buffer.begin();
  } else {
    result.dictionary[0] = T{};
  }

  tbb::parallel_for(
      size_t{0}, result_partitions.size(),
      [&result, &result_partitions = std::as_const(result_partitions),
       &sizes_and_offsets = std::as_const(sizes_and_offsets)](const size_t result_partitions_index) {
        const auto& dictionary_partition_view{result_partitions[result_partitions_index].dictionary_view()};

        copy_dictionary_partition(dictionary_partition_view, sizes_and_offsets, result_partitions_index, result);
      });

  return result;
}

template dictionary_data<cel_int_t> copy_dictionary_partitions<cel_int_t>(
    const legacy_embedded_ctl::static_array<result_partition<cel_int_t>>&, const sizes_and_offsets<cel_int_t>&,
    common::execution_context&);
template dictionary_data<cel_float_t> copy_dictionary_partitions<cel_float_t>(
    const legacy_embedded_ctl::static_array<result_partition<cel_float_t>>&, const sizes_and_offsets<cel_float_t>&,
    common::execution_context&);
template dictionary_data<cel_date_t> copy_dictionary_partitions<cel_date_t>(
    const legacy_embedded_ctl::static_array<result_partition<cel_date_t>>&, const sizes_and_offsets<cel_date_t>&,
    common::execution_context&);
template dictionary_data<cel_string_t> copy_dictionary_partitions<cel_string_t>(
    const legacy_embedded_ctl::static_array<result_partition<cel_string_t>>&, const sizes_and_offsets<cel_string_t>&,
    common::execution_context&);

template <typename T>
legacy_embedded_ctl::static_array<legacy_embedded_ctl::static_array<row_id>> copy_mapping_partitions(
    const std::vector<typed_dictionary<T>*>& typed_dictionaries,
    const std::vector<disjunct_partition<T>>& disjunct_partitions,
    const legacy_embedded_ctl::static_array<result_partition<T>>& result_partitions, const legacy_embedded_ctl::static_array<size_t>& partition_offsets,
    const legacy_embedded_ctl::static_array<flattened_partition>& flattened_partitions, common::execution_context& context) {
  auto output_mappings{memory::tracking::make_static_array_for_overwrite<legacy_embedded_ctl::static_array<row_id>>(
      typed_dictionaries.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG), context)};

  for (std::size_t i{0}; i < typed_dictionaries.size(); ++i) {
    auto output_mapping{memory::tracking::make_static_array_for_overwrite<row_id>(
        typed_dictionaries.at(i)->get_size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG), context)};
    output_mapping[0] = 0;  // NULL always maps to NULL
    output_mappings[i] = std::move(output_mapping);
  }

  tbb::parallel_for_each(flattened_partitions, [&output_mappings,
                                                &disjunct_partitions = std::as_const(disjunct_partitions),
                                                &partition_offsets = std::as_const(partition_offsets),
                                                &result_partitions =
                                                    std::as_const(result_partitions)](const auto& flattened_partition) {
    const auto& input_partition{
        disjunct_partitions[flattened_partition.disjunct_partitions_i][flattened_partition.disjunct_partitions_j]};

    const size_t original_dict{input_partition.original_dictionary_idx};
    const size_t original_dict_offset{input_partition.original_dictionary_offset};
    const size_t output_partition_offset{partition_offsets[flattened_partition.disjunct_partitions_i]};

    const auto& result_partition{result_partitions[flattened_partition.disjunct_partitions_i]};

    if (result_partition.has_mappings()) {
      const auto& partition_mapping_views{result_partition.mapping_views()[flattened_partition.disjunct_partitions_j]};

      for (size_t original_dict_index{original_dict_offset}, mapping_partition_index{0};
           mapping_partition_index < partition_mapping_views.size(); ++original_dict_index, ++mapping_partition_index) {
        output_mappings[original_dict][original_dict_index] =
            output_partition_offset + partition_mapping_views[mapping_partition_index];
      }
    } else {
      // This is a partition that was not merged, i.e. we do not have any mappings available. To compute the mapping
      // for this partition, just using the offsets previously generated + counting is necessary.
      row_id new_dict_index{static_cast<row_id>(output_partition_offset)};
      for (size_t original_dict_index{original_dict_offset};
           original_dict_index < original_dict_offset + input_partition.view.size();
           ++original_dict_index, ++new_dict_index) {
        output_mappings[original_dict][original_dict_index] = new_dict_index;
      }
    }
  });
  return output_mappings;
}

template legacy_embedded_ctl::static_array<legacy_embedded_ctl::static_array<row_id>> copy_mapping_partitions<cel_int_t>(
    const std::vector<typed_dictionary<cel_int_t>*>&, const std::vector<disjunct_partition<cel_int_t>>&,
    const legacy_embedded_ctl::static_array<result_partition<cel_int_t>>&, const legacy_embedded_ctl::static_array<size_t>&,
    const legacy_embedded_ctl::static_array<flattened_partition>&, common::execution_context&);
template legacy_embedded_ctl::static_array<legacy_embedded_ctl::static_array<row_id>> copy_mapping_partitions<cel_float_t>(
    const std::vector<typed_dictionary<cel_float_t>*>&, const std::vector<disjunct_partition<cel_float_t>>&,
    const legacy_embedded_ctl::static_array<result_partition<cel_float_t>>&, const legacy_embedded_ctl::static_array<size_t>&,
    const legacy_embedded_ctl::static_array<flattened_partition>&, common::execution_context&);
template legacy_embedded_ctl::static_array<legacy_embedded_ctl::static_array<row_id>> copy_mapping_partitions<cel_date_t>(
    const std::vector<typed_dictionary<cel_date_t>*>&, const std::vector<disjunct_partition<cel_date_t>>&,
    const legacy_embedded_ctl::static_array<result_partition<cel_date_t>>&, const legacy_embedded_ctl::static_array<size_t>&,
    const legacy_embedded_ctl::static_array<flattened_partition>&, common::execution_context&);
template legacy_embedded_ctl::static_array<legacy_embedded_ctl::static_array<row_id>> copy_mapping_partitions<cel_string_t>(
    const std::vector<typed_dictionary<cel_string_t>*>&, const std::vector<disjunct_partition<cel_string_t>>&,
    const legacy_embedded_ctl::static_array<result_partition<cel_string_t>>&, const legacy_embedded_ctl::static_array<size_t>&,
    const legacy_embedded_ctl::static_array<flattened_partition>&, common::execution_context&);

template <typename T>
std::pair<raw_dictionary_t, legacy_embedded_ctl::static_array<legacy_embedded_ctl::static_array<row_id>>> construct_and_merge_typed_dictionaries(
    const std::vector<non_null_dictionary>& dictionaries, const std::string& op_name,
    common::execution_context& context) {
  auto typed_dictionaries{details::to_typed_dictionaries<T>(dictionaries, op_name)};
  auto dictionary_accessors{details::get_dictionary_accessors(typed_dictionaries, context)};
  auto input_partitions{details::partition(dictionary_accessors)};
  auto disjunct_partitions{details::group(input_partitions)};
  auto [result_views, data]{details::merge(disjunct_partitions, context)};
  auto sizes_and_offsets{details::calculate_sizes_and_offsets(result_views)};
  auto output_data{details::copy_dictionary_partitions(result_views, sizes_and_offsets, context)};

  auto flattened_partitions{details::flatten(input_partitions.size(), disjunct_partitions)};
  auto output_mappings{details::copy_mapping_partitions(typed_dictionaries, disjunct_partitions, result_views,
                                                        sizes_and_offsets.partition_offsets, flattened_partitions,
                                                        context)};

  if constexpr (std::is_same_v<T, cel_string_t>) {
    return {std::make_unique<typed_raw_dictionary<T>>(std::move(output_data.dictionary),
                                                      std::move(output_data.dictionary_buffer)),
            std::move(output_mappings)};
  } else {
    return {std::make_unique<typed_raw_dictionary<T>>(std::move(output_data.dictionary)), std::move(output_mappings)};
  }
}

merge_result merge_n_dictionaries_raw_internal(const std::vector<non_null_dictionary>& non_null_dict,
                                               const std::string& op_name, common::execution_context& context) {
  std::pair<raw_dictionary_t, legacy_embedded_ctl::static_array<legacy_embedded_ctl::static_array<row_id>>> raw_result;
  switch (non_null_dict[0].dict->type) {
    case cel_int:
      raw_result = construct_and_merge_typed_dictionaries<cel_int_t>(non_null_dict, op_name, context);
      break;
    case cel_float:
      raw_result = construct_and_merge_typed_dictionaries<cel_float_t>(non_null_dict, op_name, context);
      break;
    case cel_date:
      raw_result = construct_and_merge_typed_dictionaries<cel_date_t>(non_null_dict, op_name, context);
      break;
    case cel_string:
      raw_result = construct_and_merge_typed_dictionaries<cel_string_t>(non_null_dict, op_name, context);
      break;
    default:
      throw common::cpm_exception{"{} : Merge dictionaries: Type [{}] not supported.", op_name,
                                  convert_to_string(non_null_dict[0].dict->type)};
  }
  auto dict_size{raw_result.first->get_size()};
  return {std::move(raw_result.first), std::move(raw_result.second), static_cast<row_id>(dict_size)};
}

}  // namespace details
}  // namespace celonis::accelerator::memory
