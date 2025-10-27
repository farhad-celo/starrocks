#include "string_aggregation.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <string_view>
#include <vector>

#include <boost/functional/hash.hpp>
#include <bytell_hash_map.hpp>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for_each.h>
#include <tbb/parallel_sort.h>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/bits/half_open_interval.h"
#include "legacy_embedded_ctl/conversion.h"
#include "legacy_embedded_ctl/static_array.h"
#include "legacy_embedded_ctl/utils/allocation_messages.h"
#include "modules/common/aligned_blocked_range.h"
#include "modules/common/trace_types.h"
#include "modules/cube/variant_trace_cache_manager.h"
#include "modules/cube/variant_trace_utils.h"
#ifndef CELOSTAR
#include "modules/memory/builders/cache_column_from_dictionary.h"
#endif
#include "modules/memory/column.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/table.h"
#include "modules/memory/tracking/static_array_with_context_tracking.h"
#include "modules/operators/process/variant_operator_common.h"

namespace celonis::accelerator::operators::aggregation {

namespace {

constexpr row_id SKIP_ROW{-1};

template <typename STRING_COL_PTR_T>
using agg_group_handle = process::variant_set_handle<STRING_COL_PTR_T>;

template <typename OPERATOR_ACCESSOR, typename STRING_COL_PTR_T>
struct eq_agg_group_handle {
  const OPERATOR_ACCESSOR* accessor{};
  const STRING_COL_PTR_T* string_col_ptrs_ac{};

  eq_agg_group_handle(const OPERATOR_ACCESSOR* accessor, const STRING_COL_PTR_T* string_col_ptrs_ac)
      : accessor{accessor}, string_col_ptrs_ac{string_col_ptrs_ac} {}
  eq_agg_group_handle() = default;

  bool operator()(const agg_group_handle<const STRING_COL_PTR_T>& h1,
                  const agg_group_handle<const STRING_COL_PTR_T>& h2) {
    return accessor->equals(h1, h2, string_col_ptrs_ac);
  }
};

/**
 * Per-thread information for the first phase of the groups calculation
 */
template <class STRING_COL_PTR_T, class EQ_AGG_GROUP_SET_HANDLE>
struct deduplicate_local {
  using hashmap_type = ska::bytell_hash_map<agg_group_handle<STRING_COL_PTR_T>, row_id,
                                            process::get_hash<STRING_COL_PTR_T>, EQ_AGG_GROUP_SET_HANDLE>;

  std::array<hashmap_type, process::HASHMAPS> set;
  // initializes all next_id to 0
  std::array<row_id, process::HASHMAPS> next_id{};
  row_id thread_id{-1};

  deduplicate_local(process::get_hash<STRING_COL_PTR_T> hash, EQ_AGG_GROUP_SET_HANDLE eq_cmp, row_id thread_id)
      : thread_id{thread_id} {
    // Initialize the hashmaps with custom hash and comparator.
    for (auto& entry : set) {
      entry = hashmap_type{1024, hash, eq_cmp};  // 1024 buckets for each map
    }
  }
};

template <class STRING_PTR_TYPE, class EQ_AGG_GROUP_SET_HANDLE>
using thread_local_deduplicate_type =
    tbb::enumerable_thread_specific<deduplicate_local<STRING_PTR_TYPE, EQ_AGG_GROUP_SET_HANDLE>>;

/**
 * Struct to temporarily store an id and assigned slot for each group
 */
struct group_result {
  row_id agg_group_id{0};
  int slot{-1};
  row_id thread_id{0};
};

template <class STRING_COL_PTR_T, class EQ_GROUP_SET_HANDLE>
struct deduplicate_local_init {
  std::atomic<int>& thread_id;
  EQ_GROUP_SET_HANDLE eq_cmp;

  deduplicate_local_init(std::atomic<int>& thread_id, EQ_GROUP_SET_HANDLE eq_cmp)
      : thread_id{thread_id}, eq_cmp{eq_cmp} {}

  deduplicate_local<STRING_COL_PTR_T, EQ_GROUP_SET_HANDLE> operator()() {
    deduplicate_local<STRING_COL_PTR_T, EQ_GROUP_SET_HANDLE> deduplicate_local_instance{
        process::get_hash<STRING_COL_PTR_T>{}, eq_cmp, thread_id++};
    return deduplicate_local_instance;
  }
};

struct trace_buffer_sizes_and_offsets_type {
  int64_t trace_buffer_size;
  legacy_embedded_ctl::static_array<int64_t> slot_trace_buffer_offset;

  trace_buffer_sizes_and_offsets_type(const int64_t trace_buffer_size,
                                      legacy_embedded_ctl::static_array<int64_t> slot_trace_buffer_offset)
      : trace_buffer_size{trace_buffer_size}, slot_trace_buffer_offset{std::move(slot_trace_buffer_offset)} {}
};

/**
 * Return type containing all the offsets for parallel filling of the buffers.
 */
struct buffer_sizes_and_offsets_type {
  int64_t string_buffer_size;
  legacy_embedded_ctl::static_array<int64_t> slot_string_buffer_offset;
  // The following two members are specific to the VARIANT operator.
  int64_t trace_buffer_size;
  legacy_embedded_ctl::static_array<int64_t> slot_trace_buffer_offset;

  buffer_sizes_and_offsets_type(const int64_t string_buffer_size,
                                legacy_embedded_ctl::static_array<int64_t> slot_string_buffer_offset,
                                const int64_t trace_buffer_size,
                                legacy_embedded_ctl::static_array<int64_t> slot_trace_buffer_offset)
      : string_buffer_size{string_buffer_size},
        slot_string_buffer_offset{std::move(slot_string_buffer_offset)},
        trace_buffer_size{trace_buffer_size},
        slot_trace_buffer_offset{std::move(slot_trace_buffer_offset)} {}
};

/**
 * Struct to represent each unique agg_group_id during sorting
 */
struct sort_agg_group_handle {
  row_id length{};
  row_id orig_id{};
  char* aggregated_string{};
  // The following two members are specific to the VARIANT operator.
  int16_t* trace_pointer{};
  bool tainted{};

  sort_agg_group_handle(row_id length, row_id orig_pos, int16_t* trace_pointer)
      : length{length}, orig_id{orig_pos}, trace_pointer{trace_pointer} {}
  sort_agg_group_handle() = default;
};

/**
 * Struct to represent each unique agg_group_id during sorting only by row-id
 * without taking into account the aggregated string
 */
struct sort_row_ids_handle {
  row_id length{};
  row_id orig_id{};
  int16_t* trace_pointer{};
};

template <typename OPERATOR_ACCESSOR, typename PROJECTION_TYPE, class STRING_COL_PTR_T, class STRING_PTR_AC_TYPE>
void handle_aggregation_group(
    const OPERATOR_ACCESSOR& accessor, const row_id row, const row_id last_group_id,
    deduplicate_local<STRING_COL_PTR_T, eq_agg_group_handle<OPERATOR_ACCESSOR, STRING_COL_PTR_T>>& thread_local_ctx,
    agg_group_handle<STRING_COL_PTR_T>& current_agg_group, const STRING_PTR_AC_TYPE& string_ptrs_ac,
    legacy_embedded_ctl::static_array<row_id>& project_group_id_to_group_table,
    legacy_embedded_ctl::static_array<group_result>& result_per_group, const PROJECTION_TYPE projection_vector) {
  if (last_group_id == VALUE_NOT_FOUND) {
    return;
  }

  current_agg_group.length = row - current_agg_group.row_start;

  // negative length does not make sense
  legacy_embedded_debug_assert(current_agg_group.length > 0);

  project_group_id_to_group_table[last_group_id] = accessor.get_group_id(projection_vector, row - 1);

  std::tie(current_agg_group.hashvalue, current_agg_group.output_length) =
      accessor.compute_hash_and_output_length(current_agg_group, string_ptrs_ac);

  // handle NULL aggregation group
  if (current_agg_group.output_length == 0) {
    result_per_group[last_group_id].agg_group_id = 0;
    result_per_group[last_group_id].slot = -1;
    result_per_group[last_group_id].thread_id = 0;
    return;
  }

  // Compute slot based on bits 26 to 31 (zero-based indexing)
  const auto slot{current_agg_group.hashvalue >> 26 & ((1 << 6) - 1)};
  const auto [unique_variant_iter,
              is_new_variant]{thread_local_ctx.set[slot].emplace(current_agg_group, thread_local_ctx.next_id[slot])};

  // check if new variant
  if (is_new_variant) {
    thread_local_ctx.next_id[slot]++;
  }

  // Assign temporary aggregation group id
  result_per_group[last_group_id].agg_group_id = unique_variant_iter->second;  // local variant id within its thread
  result_per_group[last_group_id].slot = legacy_embedded_ctl::cast<int>(slot);
  result_per_group[last_group_id].thread_id = thread_local_ctx.thread_id;
}

/**
 * Populate the thread specific hash maps with the different variants encountered in the slice of the input column
 * (i.e. group/case aligned blocks) assigned to a thread.
 *
 * Each unique variant (unique from the perspective of the processing thread) encountered is only assigned a local id
 * at this stage.
 */
template <typename OPERATOR_ACCESSOR, typename PROJECTION_TYPE, class STRING_PTR_AC_TYPE>
void collect_aggregation_groups(
    const OPERATOR_ACCESSOR& accessor,
    const std::vector<legacy_embedded_ctl::half_open_interval<row_id>>& group_aligned_blocks,
    thread_local_deduplicate_type<
        std::remove_reference_t<decltype(std::declval<const STRING_PTR_AC_TYPE&>()[row_id{}])>,
        eq_agg_group_handle<OPERATOR_ACCESSOR,
                            std::remove_reference_t<decltype(std::declval<const STRING_PTR_AC_TYPE&>()[row_id{}])>>>&
        thread_local_deduplicate_step_1,
    const STRING_PTR_AC_TYPE& string_ptrs_ac,
    legacy_embedded_ctl::static_array<row_id>& project_group_id_to_group_table,
    legacy_embedded_ctl::static_array<group_result>& result_per_group, const PROJECTION_TYPE& projection) {
  using string_col_ptr_t = std::remove_reference_t<decltype(std::declval<const STRING_PTR_AC_TYPE&>()[row_id{}])>;

  // Deduplication loop - parallelization by group_aligned_blocks
  tbb::parallel_for_each(group_aligned_blocks, [&](legacy_embedded_ctl::half_open_interval<row_id> block_data) {
    bool exists{false};
    auto& thread_local_ctx{thread_local_deduplicate_step_1.local(exists)};

    row_id current_group_id{accessor.get_group_id(projection, block_data.begin())};
    auto last_group_id{current_group_id};

    agg_group_handle<string_col_ptr_t> current_agg_group{};
    accessor.init_agg_group_handle(current_agg_group, block_data.begin(), string_ptrs_ac);

    for (row_id i{block_data.begin()}; i < block_data.end(); i++) {
      current_group_id = accessor.get_group_id(projection, i);
      if (current_group_id != last_group_id) {
        // finish previous group. i is the first row of the new group (iterator end)
        handle_aggregation_group(accessor, i, last_group_id, thread_local_ctx, current_agg_group, string_ptrs_ac,
                                 project_group_id_to_group_table, result_per_group, projection);
        last_group_id = current_group_id;

        // Initialize next agg_group.
        accessor.init_agg_group_handle(current_agg_group, i, string_ptrs_ac);
      }
    }

    // finish last group in block. i is the first row of the new group (iterator end)
    handle_aggregation_group(accessor, block_data.end(), last_group_id, thread_local_ctx, current_agg_group,
                             string_ptrs_ac, project_group_id_to_group_table, result_per_group, projection);
  });
}

/**
 * Adjusts the local aggregation group ids (read variant id) by computing the id offsets for each slot and then using
 * these to assign a global variant id to each unique variant.
 *
 * i.e. imagine slot 0 has two variants with local ids = {0, 1} and slot 2 contains 1 variant with local ids = {0}.
 * We would then compute the following id offset array for the two slots [1 (due to NULL), 3] and map the local ids to
 * global ids like so local_id + slot_id_offset: slot_0 -> {1, 2} and slot_2 -> {3}
 */
template <typename OPERATOR_ACCESSOR, class STRING_PTR_TYPE>
row_id compute_offset_global_agg_group_id(
    size_t output_row_count, std::array<row_id, process::HASHMAPS>& id_offset,
    std::vector<std::vector<std::vector<row_id>>>& thread_slot_local_to_slot_local_maps,
    deduplicate_local<STRING_PTR_TYPE, eq_agg_group_handle<OPERATOR_ACCESSOR, STRING_PTR_TYPE>>& thread_local_front,
    group_result* result_per_group_ptr, const size_t grain_size) {
  row_id aggregation_domain_size{1};  // 1 for NULL

  // now loop over slots to get offset
  for (row_id slot{0}; slot < process::HASHMAPS; ++slot) {
    id_offset[slot] = aggregation_domain_size;
    aggregation_domain_size += thread_local_front.next_id[slot];
  }
  // NULL gets assigned agg_group_id 0. The first non-null group gets assigned 1.
  legacy_embedded_debug_assert(id_offset[0] == 1);

  // adjust agg_group_id
  tbb::parallel_for(
      tbb::blocked_range<row_id>{0, static_cast<row_id>(output_row_count), grain_size}, [&](const auto range) {
        for (row_id i{range.begin()}; i < range.end(); ++i) {
          const int slot{result_per_group_ptr[i].slot};
          // slot == -1 means that the case is a NULL trace case
          if (slot != -1) {
            legacy_embedded_debug_assert(
                static_cast<size_t>(result_per_group_ptr[i].agg_group_id) <
                thread_slot_local_to_slot_local_maps[slot][result_per_group_ptr[i].thread_id].size());
            result_per_group_ptr[i].agg_group_id =
                id_offset[slot] + thread_slot_local_to_slot_local_maps[slot][result_per_group_ptr[i].thread_id]
                                                                      [result_per_group_ptr[i].agg_group_id];
          } else {
            result_per_group_ptr[i].agg_group_id = 0;
          }
        }
      });

  return aggregation_domain_size;  // == dictionary size for unique variants, including NULL entry
}

trace_buffer_sizes_and_offsets_type compute_trace_buffer_sizes_and_offsets(auto& thread_local_front,
                                                                           const common::execution_context& context) {
  auto required_trace_buffer_sizes{memory::tracking::make_static_array_for_overwrite<int64_t>(
      process::HASHMAPS, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};

  legacy_embedded_debug_assert(thread_local_front.set.size() == required_trace_buffer_sizes.size());

  //     for each slot, aggregate the output length of each variant to find the size of the required trace buffer
  //     for the slot
  tbb::parallel_for(
      tbb::blocked_range<row_id>{0, static_cast<row_id>(thread_local_front.set.size())}, [&](const auto range) {
        auto input_begin_it{std::next(begin(thread_local_front.set), range.begin())};
        auto input_end_it{std::next(begin(thread_local_front.set), range.end())};
        auto output_begin_it{std::next(begin(required_trace_buffer_sizes), range.begin())};

        std::transform(input_begin_it, input_end_it, output_begin_it, [&](const auto& slot_map) {
          return std::accumulate(slot_map.begin(), slot_map.end(), decltype(required_trace_buffer_sizes)::value_type{0},
                                 [](const int64_t accumulated_value, const auto& map_entry) {
                                   return accumulated_value + map_entry.first.output_length;
                                 });
        });
      });

  // rename to reflect new function
  auto& slot_trace_buffer_offset{required_trace_buffer_sizes};

  const auto last_offset{required_trace_buffer_sizes.back()};

  // the offset for slot i into the trace buffer s given by accumulating the required_trace_buffer_sizes excluding
  // the size of the slot i itself
  std::exclusive_scan(slot_trace_buffer_offset.begin(), slot_trace_buffer_offset.end(),
                      slot_trace_buffer_offset.begin(), int64_t{0});

  const auto trace_buffer_size{last_offset + slot_trace_buffer_offset.back()};

  return {trace_buffer_size, std::move(slot_trace_buffer_offset)};
}

#ifndef CELOSTAR
/**
 * Compute buffer sizes for traces and strings. Additionally, compute the offsets into these buffer for each
 * slot's (only for 'thread_local_front') data.
 */
template <typename OPERATOR_ACCESSOR, typename STRING_COL_PTR_T, typename STRING_PTR_AC_TYPE>
buffer_sizes_and_offsets_type compute_buffer_sizes_and_offsets(
    const OPERATOR_ACCESSOR& accessor,
    deduplicate_local<STRING_COL_PTR_T, eq_agg_group_handle<OPERATOR_ACCESSOR, STRING_COL_PTR_T>>& thread_local_front,
    const legacy_embedded_ctl::static_array<int64_t>& string_lengths, const STRING_PTR_AC_TYPE& string_ptrs_ac,
    const std::string_view delimiter, const common::execution_context& context) {
  std::array<int64_t, process::HASHMAPS> required_trace_buffer_size{};  // value-initialize to zero
  std::array<int64_t, process::HASHMAPS> required_string_buffer_size{};

  tbb::parallel_for(tbb::blocked_range<row_id>{0, process::HASHMAPS, 1}, [&](const auto range) {
    for (row_id slot{range.begin()}; slot < range.end(); slot++) {
      int64_t slot_string_buffer_size{0};

      // for each slot, go over the (unique) variants
      for ([[maybe_unused]] const auto& [agg_group, _] : thread_local_front.set[slot]) {
        required_trace_buffer_size[slot] += agg_group.output_length;  // length excluding cycles
        int64_t string_buffer_size{0};
        const auto offset{static_cast<size_t>(agg_group.row_start)};
        for (size_t i{0}; i < static_cast<size_t>(agg_group.length); ++i) {
          const auto current_row{accessor.get_current_row(offset, i, string_ptrs_ac)};
          if (current_row == SKIP_ROW) {
            continue;
          }
          string_buffer_size += string_lengths[string_ptrs_ac[current_row]] + delimiter.size();
        }
        // + null byte at end, - the length of the delimiter for the last attached string
        string_buffer_size += 1 - legacy_embedded_ctl::cast<decltype(string_buffer_size)>(delimiter.size());
        // Ensure we at least reserve 1 byte for \0
        string_buffer_size = std::max<int64_t>(string_buffer_size, 1);
        slot_string_buffer_size += string_buffer_size;
      }
      required_string_buffer_size[slot] = slot_string_buffer_size;
    }
  });

  // compute the offsets for the slots in the trace_buffer and string_buffer
  int64_t trace_buffer_offset{0};
  auto slot_trace_buffer_offset{memory::tracking::make_static_array_for_overwrite<int64_t>(
      process::HASHMAPS, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};
  // for NULL, 5 chars are required
  int64_t string_buffer_offset{NULL_STRING.size()};
  auto slot_string_buffer_offset{memory::tracking::make_static_array_for_overwrite<int64_t>(
      process::HASHMAPS, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};

  for (int slot{0}; slot < process::HASHMAPS; ++slot) {
    slot_trace_buffer_offset[slot] = trace_buffer_offset;
    trace_buffer_offset += required_trace_buffer_size[slot];

    slot_string_buffer_offset[slot] = string_buffer_offset;
    string_buffer_offset += required_string_buffer_size[slot];
  }

  return buffer_sizes_and_offsets_type(string_buffer_offset, std::move(slot_string_buffer_offset), trace_buffer_offset,
                                       std::move(slot_trace_buffer_offset));
}
#endif

/**
 * Fills the trace buffer and the to_sort array.
 *
 * Note that we do not fill the trace_ptrs / trace_length arrays here. We only set the location of the NULL variant.
 * The trace_ptrs and trace_length arrays are filled once we have the sorted trace buffer.
 */
template <typename ACTIVITY_PTR_TYPE, typename OPERATOR_ACCESSOR>
void fill_trace_buffer_and_sort_array(
    legacy_embedded_ctl::static_array<trace_type>& trace_ptrs_array,
    legacy_embedded_ctl::static_array<trace_length_type>& trace_length_array,
    legacy_embedded_ctl::static_array<int16_t>& trace_buffer_data,
    const trace_buffer_sizes_and_offsets_type& buffer_sizes_and_offsets,
    const std::array<row_id, process::HASHMAPS>& id_offset,
    legacy_embedded_ctl::static_array<sort_row_ids_handle>& to_sort,
    const deduplicate_local<ACTIVITY_PTR_TYPE, eq_agg_group_handle<OPERATOR_ACCESSOR, ACTIVITY_PTR_TYPE>>&
        thread_local_front) {
  // variant_id == 0 represents NULL
  trace_ptrs_array[0] = trace_buffer_data.data();
  trace_length_array[0] = 0;

  to_sort.at(0).length = 0;
  to_sort.at(0).orig_id = 0;
  to_sort.at(0).trace_pointer = trace_buffer_data.get();

  tbb::parallel_for(tbb::blocked_range<row_id>{0, process::HASHMAPS, 1}, [&](const auto range) {
    for (row_id slot{range.begin()}; slot < range.end(); slot++) {
      int64_t slot_trace_offset{buffer_sizes_and_offsets.slot_trace_buffer_offset[slot]};
      int16_t* slot_trace_data_ptr{trace_buffer_data.get() + slot_trace_offset};

      for (const auto& [variant, next_id] : thread_local_front.set[slot]) {
        row_id variant_id{id_offset[slot] + next_id};

        legacy_embedded_debug_assert(variant_id != 0);

        auto* trace_start_ptr{slot_trace_data_ptr};

        auto* activity_ptr{variant.trace_pointer};
        slot_trace_data_ptr = std::copy_if(activity_ptr, std::next(activity_ptr, variant.length), slot_trace_data_ptr,
                                           [](auto a) { return a != 0; });

        to_sort.at(variant_id) = {.length = std::distance(trace_start_ptr, slot_trace_data_ptr),
                                  .orig_id = variant_id,
                                  .trace_pointer = trace_start_ptr};
      }
    }
  });
}

#ifndef CELOSTAR
/**
 * For each unique variant, fills the trace and string buffers. Additionally, fills the 'to_sort' array
 * that is used to sort the variant strings so as to build the string dictionary for the output column.
 *
 *  @param string_buffer_sa will contain the unique variant strings after a call to this function.
 *
 * @param trace_data_ptr_unsorted contain the trace data i.e. the row ids from the activity column, representing each
 * unique variant.
 *
 * @param to_sort to_sort[variant_id] will contain information (pointers into the string and trace buffers etc.)
 * regarding the variant with id 'variant_id' after termination of this function.
 */
template <typename ACTIVITY_PTR_TYPE, typename OPERATOR_ACCESSOR>
void fill_buffers_and_sort_array_with_traces(
    const memory::string_dictionary::const_data_accessor_t& activity_strings_data, char* string_buffer_sa,
    int16_t* trace_data_ptr_unsorted, trace_type* const traces, trace_length_type* const trace_lengths_data,
    sort_agg_group_handle* to_sort, const process::sort_mappers_result& string_sort_taint_maps,
    const buffer_sizes_and_offsets_type& buffer_sizes_and_offsets,
    const std::array<row_id, process::HASHMAPS>& id_offset, const std::string_view delimiter,
    const deduplicate_local<ACTIVITY_PTR_TYPE, eq_agg_group_handle<OPERATOR_ACCESSOR, ACTIVITY_PTR_TYPE>>&
        thread_local_front) {
  const auto& string_lengths{string_sort_taint_maps.string_sizes};
  strncpy(string_buffer_sa, NULL_STRING.data(), NULL_STRING.size());

  // pre-fill the "NULL" variant
  traces[0] = trace_data_ptr_unsorted;
  trace_lengths_data[0] = 0;

  // Null traces
  to_sort[0].orig_id = 0;
  to_sort[0].length = 0;
  to_sort[0].trace_pointer = trace_data_ptr_unsorted;
  to_sort[0].aggregated_string = string_buffer_sa;
  to_sort[0].tainted = false;

  const auto& taint_map{string_sort_taint_maps.tainted};

  // fill string and trace buffer, and the to_sort array
  tbb::parallel_for(tbb::blocked_range<row_id>{0, process::HASHMAPS, 1}, [&](const auto range) {
    for (row_id slot{range.begin()}; slot < range.end(); slot++) {
      int64_t slot_trace_offset{buffer_sizes_and_offsets.slot_trace_buffer_offset[slot]};
      int16_t* trace_data_ptr{trace_data_ptr_unsorted + slot_trace_offset};

      int64_t string_offset{buffer_sizes_and_offsets.slot_string_buffer_offset[slot]};
      char* string_ptr{string_buffer_sa + string_offset};

      for (const auto& [variant, slot_local_id] : thread_local_front.set[slot]) {
        row_id variant_id{id_offset[slot] + slot_local_id};
        to_sort[variant_id].aggregated_string = string_ptr;
        auto* trace_start_ptr{trace_data_ptr};
        bool tainted{false};

        auto* activity_ptr{variant.trace_pointer};
        row_id non_null_count{0};
        for (row_id i{0}; i < variant.length; ++i) {
          if (activity_ptr[i] != 0) {
            non_null_count++;
            // Trace
            tainted = tainted || taint_map.test(activity_ptr[i]);
            *trace_data_ptr++ = legacy_embedded_ctl::cast<int16_t>(activity_ptr[i]);

            // string
            std::copy_n(activity_strings_data[activity_ptr[i]], string_lengths[activity_ptr[i]], string_ptr);
            string_ptr += string_lengths[activity_ptr[i]];

            if (non_null_count != variant.output_length) {
              std::copy_n(delimiter.begin(), delimiter.size(), string_ptr);
              string_ptr += delimiter.size();
            }
          }
        }
        *string_ptr++ = '\0';

        legacy_embedded_debug_assert(variant_id != 0);
        to_sort[variant_id].length = non_null_count;
        to_sort[variant_id].orig_id = variant_id;
        to_sort[variant_id].trace_pointer = trace_start_ptr;
        to_sort[variant_id].tainted = tainted;
      }
    }
  });
}

template <typename STRING_COL_PTR, typename STRING_COL_PTR_TYPE, typename OPERATOR_ACCESSOR>
void fill_buffers_and_sort_array_with_strings(
    const STRING_COL_PTR& string_col_ptrs_ac, const memory::string_dictionary::const_data_accessor_t& strings_data,
    char* string_buffer_sa, sort_agg_group_handle* to_sort,
    const legacy_embedded_ctl::static_array<int64_t>& string_lengths,
    const buffer_sizes_and_offsets_type& buffer_sizes_and_offsets,
    const std::array<row_id, process::HASHMAPS>& id_offset,
    const legacy_embedded_ctl::static_array<row_id>& group_aligned_permutation,
    const cube::filter_bitset_t& accepted_rows, const std::string_view delimiter,
    const deduplicate_local<STRING_COL_PTR_TYPE, eq_agg_group_handle<OPERATOR_ACCESSOR, STRING_COL_PTR_TYPE>>&
        thread_local_front) {
  strncpy(string_buffer_sa, NULL_STRING.data(), NULL_STRING.size());

  // NULL aggregation group
  to_sort[0].orig_id = 0;
  to_sort[0].length = 0;
  to_sort[0].aggregated_string = string_buffer_sa;

  // fill string and trace buffer, and the to_sort array
  tbb::parallel_for(tbb::blocked_range<row_id>{0, process::HASHMAPS, 1}, [&](const auto range) {
    for (row_id slot{range.begin()}; slot < range.end(); slot++) {
      int64_t string_offset{buffer_sizes_and_offsets.slot_string_buffer_offset[slot]};
      char* string_ptr{string_buffer_sa + string_offset};

      for (auto& agg_group : thread_local_front.set[slot]) {
        const row_id agg_group_id{id_offset[slot] + agg_group.second};
        to_sort[agg_group_id].aggregated_string = string_ptr;

        const auto offset{static_cast<size_t>(agg_group.first.row_start)};
        row_id non_null_count{0};
        for (size_t i{0}; i < static_cast<size_t>(agg_group.first.length); ++i) {
          const auto curr_row{group_aligned_permutation[offset + i]};
          if (!accepted_rows.test(curr_row)) {
            continue;
          }

          const auto curr_string_col_ptr{string_col_ptrs_ac[curr_row]};
          if (curr_string_col_ptr != 0) {
            non_null_count++;
            std::copy_n(strings_data[curr_string_col_ptr], string_lengths[curr_string_col_ptr], string_ptr);
            string_ptr += string_lengths[curr_string_col_ptr];

            if (non_null_count != agg_group.first.output_length) {
              std::copy_n(delimiter.begin(), delimiter.size(), string_ptr);
              string_ptr += delimiter.size();
            }
          }
        }
        *string_ptr++ = '\0';

        to_sort[agg_group_id].length = non_null_count;
        to_sort[agg_group_id].orig_id = agg_group_id;
      }
    }
  });
}

/**
 * Sort the to_sort array in dictionary order i.e. to create the string dictionary for the variant strings.
 * The sort can be done in an optimized way because we do not always have to do string comparisons but can
 * look at the traces, and compare the activity ids directly. However, if the either of the variant traces contains
 * activities that already contain the VARIANT_DELIMITER i.e. ', ', then we have to do full the string comparison.
 */
void sort_unique_agg_groups_variant(sort_agg_group_handle* to_sort, row_id aggregation_domain_size,
                                    const process::sort_mappers_result& string_sort_taint_maps) {
  const auto& end_map{string_sort_taint_maps.end_map};
  const auto& no_end_map{string_sort_taint_maps.no_end_map};

  // +1 since we want to keep the entry for NULL as the first entry.
  tbb::parallel_sort(to_sort + 1, to_sort + aggregation_domain_size,
                     [&](const sort_agg_group_handle& h1, const sort_agg_group_handle& h2) {
                       /* The basic idea is that n-prefix of two variant strings (first n activities of a trace
                        * joined by ", ") matches as long as the activity id is the same and n is smaller than the
                        * size of the smaller string. The last activity id of the smaller string needs special
                        * treatment because it does not have ", " added, in contrast to the longer trace.
                        *
                        * A special case occurs if an activity contains the string ", " ("tainted" activity name).
                        * Then, the traces cannot be used to infer the string ordering, so we have to use the more
                        * expensive string comparison.
                        */
                       if (h1.tainted || h2.tainted) {
                         return std::strcmp(h1.aggregated_string, h2.aggregated_string) < 0;
                       }
                       int16_t* h1_ptr{h1.trace_pointer};
                       int16_t* h2_ptr{h2.trace_pointer};
                       // The prefix size is the size of the smaller trace except for the last activity
                       int16_t* prefix_stop{h1_ptr + std::min(h1.length, h2.length) - 1};
                       for (; h1_ptr != prefix_stop; ++h1_ptr, ++h2_ptr) {
                         if (*h1_ptr != *h2_ptr) {
                           // The no_end_map here is necessary because
                           // "Create Delivery Item" >  "Create Delivery"
                           // but
                           // "Create Delivery Item, " < "Create Delivery, "
                           return no_end_map[*h1_ptr] < no_end_map[*h2_ptr];
                         }
                       }
                       // We are at the end of at least one trace
                       if (h1.length < h2.length) {
                         return end_map[*h1_ptr] < no_end_map[*h2_ptr];
                       }
                       if (h1.length > h2.length) {
                         return no_end_map[*h1_ptr] < end_map[*h2_ptr];
                       }
                       if (h1.length == h2.length) {
                         return *h1_ptr < *h2_ptr;
                       }
                       return true;
                     });
}
#endif

void sort_unique_agg_groups_row_ids(legacy_embedded_ctl::static_array<sort_row_ids_handle>& to_sort) {
  // skip the NULL entry by starting at the first entry
  tbb::parallel_sort(std::next(begin(to_sort)), end(to_sort),
                     [](const sort_row_ids_handle& h1, const sort_row_ids_handle& h2) {
                       std::span h1_trace{h1.trace_pointer, static_cast<size_t>(h1.length)};
                       std::span h2_trace{h2.trace_pointer, static_cast<size_t>(h2.length)};

                       return std::ranges::lexicographical_compare(h1_trace, h2_trace);
                     });
}

#ifndef CELOSTAR
void sort_unique_agg_groups_pu_string_agg(sort_agg_group_handle* to_sort, row_id aggregation_domain_size) {
  // +1 since we want to keep the entry for NULL as the first entry.
  tbb::parallel_sort(to_sort + 1, to_sort + aggregation_domain_size,
                     [](const sort_agg_group_handle& h1, const sort_agg_group_handle& h2) {
                       return std::strcmp(h1.aggregated_string, h2.aggregated_string) < 0;
                     });
}
#endif

template <legacy_embedded_ctl::one_of<sort_agg_group_handle, sort_row_ids_handle> SORT_HANDLE_TYPE>
void assign_sorted_traces(row_id aggregation_domain_size, legacy_embedded_ctl::static_array<trace_type>& traces_data,
                          legacy_embedded_ctl::static_array<trace_buffer_type>& sorted_traces_buffer,
                          legacy_embedded_ctl::static_array<trace_length_type>& trace_lengths_data,
                          const legacy_embedded_ctl::static_array<SORT_HANDLE_TYPE>& to_sort,
                          legacy_embedded_ctl::static_array<row_id>& sort_variant_map,
                          legacy_embedded_ctl::static_array<cel_string_t>& pointers_sa) {
  // Should never be zero since we always have at least the empty variant
  legacy_embedded_debug_assert(aggregation_domain_size != 0);

  struct block {
    row_id trace_offset;
  };

  constexpr row_id BLOCK_SIZE{1 << 17};

  // subtracting 1 from the aggregation_domain_size takes care of the case domain_size == BLOCK_SIZE
  const row_id block_count{(aggregation_domain_size - 1) / BLOCK_SIZE + 1};
  std::vector<block> blocks(block_count);

  tbb::parallel_for(tbb::blocked_range<row_id>{0, block_count, 1}, [&](const auto range) {
    for (row_id b{range.begin()}; b < range.end(); ++b) {
      row_id begin{b * BLOCK_SIZE};
      row_id end{std::min((b + 1) * BLOCK_SIZE, aggregation_domain_size)};
      row_id next_block_offset{0};
      for (row_id i{begin}; i < end; ++i) {
        next_block_offset += to_sort[i].length;
      }
      blocks[b].trace_offset = next_block_offset;  // these are the offsets for the next block
    }
  });

  // fix the offsets for each block
  row_id cur_offset{0};
  for (auto& block : blocks) {
    const row_id block_offset{block.trace_offset};
    block.trace_offset = cur_offset;
    cur_offset += block_offset;
  }

  tbb::parallel_for(tbb::blocked_range<row_id>{0, block_count, 1}, [&](const auto range) {
    for (row_id b{range.begin()}; b < range.end(); ++b) {
      const row_id begin{b * BLOCK_SIZE};
      const row_id end{std::min((b + 1) * BLOCK_SIZE, aggregation_domain_size)};

      int16_t* cur_trace_ptr{sorted_traces_buffer.get() + blocks[b].trace_offset};

      for (row_id i{begin}; i < end; ++i) {
        const row_id trace_length{to_sort[i].length};
        int16_t* src_trace_ptr{to_sort[i].trace_pointer};
        traces_data[i] = cur_trace_ptr;
        // copy trace data
        for (row_id j{0}; j != trace_length; ++j) {
          *cur_trace_ptr++ = *src_trace_ptr++;
        }
        // traces
        trace_lengths_data[i] = trace_length;

        // strings: only if we have to produce strings
        if constexpr (std::same_as<SORT_HANDLE_TYPE, sort_agg_group_handle>) {
          pointers_sa[i] = to_sort[i].aggregated_string;
        }

        legacy_embedded_debug_assert(to_sort[i].orig_id < aggregation_domain_size);
        // map for trace and string column pointers
        sort_variant_map[to_sort[i].orig_id] = i;
      }
    }
  });

  // ensure that all variant ids are still valid
  for (row_id i{0}; i < aggregation_domain_size; ++i) {
    legacy_embedded_debug_assert(sort_variant_map[i] < aggregation_domain_size);
  }
}

void assign_sorted_traces(row_id aggregation_domain_size, legacy_embedded_ctl::static_array<trace_type>& traces_data,
                          legacy_embedded_ctl::static_array<trace_buffer_type>& sorted_traces_buffer,
                          legacy_embedded_ctl::static_array<trace_length_type>& trace_lengths_data,
                          const legacy_embedded_ctl::static_array<sort_row_ids_handle>& to_sort,
                          legacy_embedded_ctl::static_array<row_id>& sort_variant_map,
                          common::execution_context& context) {
  legacy_embedded_ctl::static_array<cel_string_t> dummy_pointers{
      memory::tracking::make_static_array_for_overwrite<cel_string_t>(
          0, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};

  assign_sorted_traces(aggregation_domain_size, traces_data, sorted_traces_buffer, trace_lengths_data, to_sort,
                       sort_variant_map, dummy_pointers);
}

#ifndef CELOSTAR
struct exec_execute_fill_output {
  const row_id output_row_count;
  const group_result* result_per_group;
  const row_id* agg_group_id_to_dict_pos;
  const row_id* project_group_id_to_group_table_ptr;
  const size_t grain_size;
  const common::execution_context& context;

  exec_execute_fill_output(const row_id output_row_count, const group_result* result_per_group,
                           const row_id* agg_group_id_to_dict_pos, const row_id* project_group_id_to_group_table_ptr,
                           const size_t grain_size, const common::execution_context& context)
      : output_row_count{output_row_count},
        result_per_group{result_per_group},
        agg_group_id_to_dict_pos{agg_group_id_to_dict_pos},
        project_group_id_to_group_table_ptr{project_group_id_to_group_table_ptr},
        grain_size{grain_size},
        context{context} {}

  template <typename COL_PTRS_TYPE>
  memory::raw_column_ptrs_t operator()() {
    auto raw_column_pointers{
        memory::create_raw_column_pointer<COL_PTRS_TYPE>(output_row_count, memory::zero_init_t{true}, context)};
    auto output_ptrs_ac{raw_column_pointers->get_data()};

    // fill column pointers for aggregated column in the grouping's table
    const auto row_count{output_row_count};
    tbb::parallel_for(tbb::blocked_range<row_id>{0, row_count, grain_size}, [&](const auto range) {
      for (row_id i{range.begin()}; i < range.end(); i++) {
        auto agg_group_id{result_per_group[i].agg_group_id};
        // Get table row for the current group
        auto projected_value{project_group_id_to_group_table_ptr[i]};
        // check if the group is mapped
        if (projected_value != VALUE_NOT_FOUND) {
          // get column pointer value
          auto dict_pos{agg_group_id_to_dict_pos[agg_group_id]};
          // assign column pointer value to the group's row
          output_ptrs_ac[projected_value] = static_cast<typename decltype(output_ptrs_ac)::value_type>(dict_pos);
        }
      }
    });

    return raw_column_pointers;
  }
};
#endif

template <typename OPERATOR_ACCESSOR, typename PROJECTION_TYPE>
std::vector<legacy_embedded_ctl::half_open_interval<row_id>> generate_group_aligned_blocks(
    const OPERATOR_ACCESSOR& accessor, const row_id row_count, const PROJECTION_TYPE& projection_vector,
    const size_t grain_size) {
  const auto grain_size_row_id{static_cast<row_id>(grain_size)};

  std::vector<legacy_embedded_ctl::half_open_interval<row_id>> blocks{};
  row_id block_start{0};
  row_id block_end{0};
  while (block_end < row_count) {
    block_end = std::min(block_start + grain_size_row_id, row_count - 1);
    const auto current_group{accessor.get_group_id(projection_vector, block_end)};

    block_end++;
    while (block_end < row_count) {
      const auto next_group{accessor.get_group_id(projection_vector, block_end)};
      if (next_group != current_group) {
        break;
      }
      block_end++;
    }

    blocks.emplace_back(block_start, block_end);
    // Start a new block.
    block_start = block_end;
  }

  return blocks;
}

struct pu_string_agg_result {
  memory::raw_column_ptrs_t output_column_pointers;
  legacy_embedded_ctl::static_array<cel_string_t> pointers_sa;
  legacy_embedded_ctl::static_array<char> string_buffer_sa;
  row_id dict_size;
};

struct variant_op_result {
  memory::raw_column_ptrs_t output_column_pointers;
  legacy_embedded_ctl::static_array<cel_string_t> pointers_sa;
  legacy_embedded_ctl::static_array<char> string_buffer_sa;
  legacy_embedded_ctl::static_array<trace_type> trace_ptrs;
  legacy_embedded_ctl::static_array<trace_buffer_type> trace_buffer_data;
  legacy_embedded_ctl::static_array<trace_length_type> trace_lengths;
  row_id num_unique_variants;
};

[[nodiscard]] variant_row_id_result create_empty_variant_row_id_result(const common::execution_context& context) {
  static constexpr trace_length_type EMPTY_TRACE_LENGTH{0};
  auto trace_cache_data_sorted{memory::tracking::make_static_array_value_init<trace_buffer_type>(
      EMPTY_TRACE_LENGTH, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};
  auto trace_lengths{memory::tracking::make_static_array<trace_length_type>(
      {EMPTY_TRACE_LENGTH}, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};
  auto traces{memory::tracking::make_static_array<trace_type>(
      {trace_cache_data_sorted.data()}, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG),
      context)};

  auto group_id_to_trace_id{common::create_owned_column_data(0, 1, memory::zero_init_t{true}, context)};

  return {std::move(traces), std::move(trace_cache_data_sorted), std::move(trace_lengths),
          std::move(group_id_to_trace_id), 1};
}

#ifndef CELOSTAR
[[nodiscard]] pu_string_agg_result create_empty_string_agg_result(const common::execution_context& context) {
  auto out_col_ptrs{memory::create_raw_column_pointer<memory::col_ptr_8_t>(0, memory::zero_init_t{true}, context)};

  int64_t str_buffer_size{NULL_STRING.size()};
  auto string_buffer_sa{memory::tracking::make_static_array_for_overwrite<char>(
      str_buffer_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};
  memcpy(&string_buffer_sa[0], NULL_STRING.data(), NULL_STRING.size());

  auto pointers_sa{memory::tracking::make_static_array_for_overwrite<cel_string_t>(
      1, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};
  pointers_sa[0] = &string_buffer_sa[0];

  return {std::move(out_col_ptrs), std::move(pointers_sa), std::move(string_buffer_sa), 1};
}
#endif

class variant_accessor {
 public:
  template <typename STRING_COL_PTR_T>
  [[nodiscard]] static bool equals(const agg_group_handle<const STRING_COL_PTR_T>& h1,
                                   const agg_group_handle<const STRING_COL_PTR_T>& h2,
                                   const STRING_COL_PTR_T* /*string_col_ptrs_ac*/) {
    return process::eq_variant_set_handle<const STRING_COL_PTR_T>{}(h1, h2);
  }

  template <typename PROJECTION_TYPE>
  [[nodiscard]] static row_id get_group_id(const PROJECTION_TYPE& projection_vector_ptr, row_id i) {
    return projection_vector_ptr[i];
  }

  template <typename ACTIVITY_COL_PTR_T, typename ACTIVITY_COL_PTR_AC_TYPE>
  static void init_agg_group_handle(agg_group_handle<ACTIVITY_COL_PTR_T>& agg_group, const row_id i,
                                    const ACTIVITY_COL_PTR_AC_TYPE& activity_col_ptrs_ac) {
    agg_group.trace_pointer = activity_col_ptrs_ac.get() + i;
    agg_group.row_start = i;
  }

  template <typename ACTIVITY_COL_PTR_T, typename ACTIVITY_COL_PTR_AC_TYPE>
  [[nodiscard]] static std::pair<size_t, row_id> compute_hash_and_output_length(
      const agg_group_handle<ACTIVITY_COL_PTR_T>& variant, const ACTIVITY_COL_PTR_AC_TYPE& /*activity_col_ptrs_ac*/) {
    size_t seed{1};
    row_id trace_length{0};

    for (const ACTIVITY_COL_PTR_T* ptr{variant.trace_pointer}; ptr != variant.trace_pointer + variant.length; ++ptr) {
      if (*ptr != 0) {
        boost::hash_combine(seed, *ptr);
        trace_length++;
      }
    }

    return {seed, trace_length};
  }

  template <typename STRING_PTR_AC_TYPE>
  [[nodiscard]] static row_id get_current_row(size_t offset, size_t i, const STRING_PTR_AC_TYPE& string_ptrs_ac) {
    row_id current_row{static_cast<row_id>(offset + i)};
    // We ignore NULL strings.
    return string_ptrs_ac[current_row] == 0 ? SKIP_ROW : current_row;
  }

#ifndef CELOSTAR
  [[nodiscard]] static variant_op_result compute_empty_result(const common::execution_context& context) {
    auto row_id_result{create_empty_variant_row_id_result(context)};
    auto string_agg_result{create_empty_string_agg_result(context)};
    return variant_op_result{std::move(string_agg_result.output_column_pointers),
                             std::move(string_agg_result.pointers_sa),
                             std::move(string_agg_result.string_buffer_sa),
                             std::move(row_id_result.trace_ptrs),
                             std::move(row_id_result.trace_buffer_data),
                             std::move(row_id_result.trace_lengths),
                             row_id_result.num_unique_variants};
  }

  template <typename STRING_PTRS, typename THREAD_LOCAL_TYPE>
  [[nodiscard]] static variant_op_result compute_final_result(
      const row_id aggregation_domain_size, const row_id output_row_count, const STRING_PTRS& /*string_ptrs_ac*/,
      const process::sort_mappers_result& string_sort_taint_maps,
      const buffer_sizes_and_offsets_type& buffer_sizes_and_offsets,
      const std::array<row_id, process::HASHMAPS>& id_offset, const THREAD_LOCAL_TYPE& thread_local_front_keep,
      const group_result* result_per_group_ptr, const row_id* project_group_id_to_group_table_ptr,
      const std::string_view delimiter, const memory::column_t& string_column, common::execution_context& context,
      const size_t grain_size) {
    auto to_sort{memory::tracking::make_static_array<sort_agg_group_handle>(
        aggregation_domain_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};

    // initialize the structures for the variant string dictionary
    auto pointers_sa{memory::tracking::make_static_array_for_overwrite<cel_string_t>(
        aggregation_domain_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};
    auto string_buffer_sa{memory::tracking::make_static_array_for_overwrite<char>(
        buffer_sizes_and_offsets.string_buffer_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG),
        context)};

    // initialize trace data for later lookups
    auto trace_cache_data_sorted{memory::tracking::make_static_array_for_overwrite<trace_buffer_type>(
        buffer_sizes_and_offsets.trace_buffer_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG),
        context)};
    auto traces{memory::tracking::make_static_array_for_overwrite<trace_type>(
        aggregation_domain_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};
    auto trace_lengths{memory::tracking::make_static_array_for_overwrite<trace_length_type>(
        aggregation_domain_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};

    auto trace_cache_data_unsorted{memory::tracking::make_static_array_value_init<trace_buffer_type>(
        buffer_sizes_and_offsets.trace_buffer_size,
        LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};

    auto activity_dictionary{string_column->get_string_dict(context)};
    const auto activity_strings_data{activity_dictionary->get_const_data(context)};

    fill_buffers_and_sort_array_with_traces(activity_strings_data, string_buffer_sa.get(),
                                            trace_cache_data_unsorted.data(), traces.get(), trace_lengths.get(),
                                            to_sort.data(), string_sort_taint_maps, buffer_sizes_and_offsets, id_offset,
                                            delimiter, thread_local_front_keep);

    sort_unique_agg_groups_variant(to_sort.data(), aggregation_domain_size, string_sort_taint_maps);
    // to_sort is now sorted in dictionary order i.e. we can use it to build the string dictionary

    auto sort_variant_map{memory::tracking::make_static_array_value_init<row_id>(
        aggregation_domain_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};

    // fill the string and trace pointers in sorted dictionary order
    assign_sorted_traces(aggregation_domain_size, traces, trace_cache_data_sorted, trace_lengths, to_sort,
                         sort_variant_map, pointers_sa);

    // project variants to the case table...
    auto output_column_pointers{memory::execute_with_column_pointers_type(
        exec_execute_fill_output{output_row_count, result_per_group_ptr, sort_variant_map.data(),
                                 project_group_id_to_group_table_ptr, grain_size, context},
        aggregation_domain_size)};

    return variant_op_result{
        std::move(output_column_pointers),  std::move(pointers_sa),   std::move(string_buffer_sa), std::move(traces),
        std::move(trace_cache_data_sorted), std::move(trace_lengths), aggregation_domain_size};
  }
#endif
};

[[nodiscard]] variant_row_id_result compute_empty_result_row_ids(const common::execution_context& context) {
  return create_empty_variant_row_id_result(context);
}

template <typename THREAD_LOCAL_TYPE>
[[nodiscard]] variant_row_id_result compute_final_result_row_ids(
    const row_id aggregation_domain_size, const row_id output_row_count,
    const trace_buffer_sizes_and_offsets_type& trace_buffer_sizes_and_offsets,
    const std::array<row_id, process::HASHMAPS>& id_offset, const THREAD_LOCAL_TYPE& thread_local_front_keep,
    const legacy_embedded_ctl::static_array<group_result>& result_per_group,
    const legacy_embedded_ctl::static_array<row_id>& project_group_id_to_group_table,
    common::execution_context& context, const size_t grain_size) {
  auto to_sort{memory::tracking::make_static_array_for_overwrite<sort_row_ids_handle>(
      aggregation_domain_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};

  // variant_id_to_trace[variant_id] points into the sorted_trace_data_buffer for variant with id 'variant_id'
  auto variant_id_to_trace{memory::tracking::make_static_array_for_overwrite<trace_type>(
      aggregation_domain_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};

  // variant_id_to_length[variant_id]: gives the length of variant with id 'variant_id'
  auto variant_id_to_trace_length{memory::tracking::make_static_array_for_overwrite<trace_length_type>(
      aggregation_domain_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};

  // Stores the actual variant trace data i.e. row ids from the activity column representing the trace
  // this is int16_t since there can only be 2^15 - 1 == 32767 distinct activity types
  auto trace_data_buffer{memory::tracking::make_static_array_value_init<int16_t>(
      trace_buffer_sizes_and_offsets.trace_buffer_size,
      LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};

  auto sorted_trace_data_buffer{memory::tracking::make_static_array_value_init<int16_t>(
      trace_buffer_sizes_and_offsets.trace_buffer_size,
      LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};

  fill_trace_buffer_and_sort_array(variant_id_to_trace, variant_id_to_trace_length, trace_data_buffer,
                                   trace_buffer_sizes_and_offsets, id_offset, to_sort, thread_local_front_keep);

  sort_unique_agg_groups_row_ids(to_sort);

  auto unsorted_to_sorted_variant_id_map{memory::tracking::make_static_array_value_init<row_id>(
      aggregation_domain_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};

  assign_sorted_traces(aggregation_domain_size, variant_id_to_trace, sorted_trace_data_buffer,
                       variant_id_to_trace_length, to_sort, unsorted_to_sorted_variant_id_map, context);

  auto group_id_to_trace_id{
      common::create_owned_column_data(output_row_count, aggregation_domain_size, memory::zero_init_t{true}, context)};

  std::visit(
      [&result_per_group = std::as_const(result_per_group),
       &project_group_id_to_group_table = std::as_const(project_group_id_to_group_table),
       &unsorted_to_sorted_variant_id_map = std::as_const(unsorted_to_sorted_variant_id_map), output_row_count,
       grain_size]<typename COL_PTR_TYPE>(common::owned_column_ptr_data<COL_PTR_TYPE>& owned_col_data) {
        tbb::parallel_for(tbb::blocked_range<row_id>{0, output_row_count, grain_size}, [&](const auto range) {
          for (row_id i{range.begin()}; i < range.end(); i++) {
            // get column pointer value
            auto variant_id{unsorted_to_sorted_variant_id_map.at(result_per_group[i].agg_group_id)};
            // Get table row for the current group
            auto projected_value{project_group_id_to_group_table[i]};

            // check if the group is mapped
            if (projected_value != VALUE_NOT_FOUND) {
              // assign column pointer value to the group's row
              owned_col_data[projected_value] = static_cast<COL_PTR_TYPE>(variant_id);
            }
          }
        });
      },
      group_id_to_trace_id);

  return variant_row_id_result{std::move(variant_id_to_trace), std::move(sorted_trace_data_buffer),
                               std::move(variant_id_to_trace_length), std::move(group_id_to_trace_id),
                               aggregation_domain_size};
}

#ifndef CELOSTAR
class pu_string_agg_accessor {
 public:
  using result_type = pu_string_agg_result;

  pu_string_agg_accessor(const legacy_embedded_ctl::static_array<row_id>& group_aligned_permutation,
                         const cube::filter_bitset_t& accepted_rows)
      : group_aligned_permutation_{group_aligned_permutation}, accepted_rows_{accepted_rows} {}

  template <typename STRING_COL_PTR_T>
  [[nodiscard]] bool equals(const agg_group_handle<const STRING_COL_PTR_T>& h1,
                            const agg_group_handle<const STRING_COL_PTR_T>& h2,
                            const STRING_COL_PTR_T* string_col_ptrs_ac) const {
    if (h1.output_length != h2.output_length) {
      return false;
    }

    size_t h1_offset{0};
    size_t h2_offset{0};

    const size_t h1_row_start{static_cast<size_t>(h1.row_start)};
    const size_t h2_row_start{static_cast<size_t>(h2.row_start)};

    const size_t h1_length{static_cast<size_t>(h1.length)};
    const size_t h2_length{static_cast<size_t>(h2.length)};

    while (h1_offset < h1_length && h2_offset < h2_length) {
      const auto h1_current_row{group_aligned_permutation_[h1_row_start + h1_offset]};
      const auto h1_string_col_ptr{string_col_ptrs_ac[h1_current_row]};
      if (h1_string_col_ptr == 0 || !accepted_rows_.test(h1_current_row)) {
        // The entry is either NULL or filtered out.
        h1_offset++;
        continue;
      }

      const auto h2_current_row{group_aligned_permutation_[h2_row_start + h2_offset]};
      const auto h2_string_col_ptr{string_col_ptrs_ac[h2_current_row]};
      if (h2_string_col_ptr == 0 || !accepted_rows_.test(h2_current_row)) {
        // The entry is either NULL or filtered out.
        h2_offset++;
        continue;
      }

      if (h1_string_col_ptr != h2_string_col_ptr) {
        // We found a non-matching entry! Hence, the agg_groups are not equal.
        return false;
      }

      h1_offset++;
      h2_offset++;
    }

    return true;
  }

  template <typename PROJECTION_TYPE>
  [[nodiscard]] row_id get_group_id(const PROJECTION_TYPE& projection_vector_ptr, row_id i) const {
    return projection_vector_ptr[group_aligned_permutation_[i]];
  }

  template <typename STRING_COL_PTR_T, typename STRING_PTR_AC_TYPE>
  static void init_agg_group_handle(agg_group_handle<STRING_COL_PTR_T>& agg_group, const row_id i,
                                    const STRING_PTR_AC_TYPE& /*string_ptrs_ac*/) {
    agg_group.row_start = i;
  }

  template <typename STRING_COL_PTR_T, typename STRING_COL_PTR_AC_TYPE>
  [[nodiscard]] std::pair<size_t, row_id> compute_hash_and_output_length(
      const agg_group_handle<STRING_COL_PTR_T>& agg_group, const STRING_COL_PTR_AC_TYPE& string_col_ptrs_ac) const {
    size_t seed{1};
    row_id agg_group_output_length{0};

    const auto offset{static_cast<size_t>(agg_group.row_start)};
    for (size_t i{0}; i < static_cast<size_t>(agg_group.length); i++) {
      const row_id current_row{group_aligned_permutation_[offset + i]};
      const auto string_col_ptr{string_col_ptrs_ac[current_row]};
      if (string_col_ptr != 0 && accepted_rows_.test(current_row)) {
        boost::hash_combine(seed, string_col_ptr);
        agg_group_output_length++;
      }
    }

    return {seed, agg_group_output_length};
  }

  template <typename STRING_PTR_AC_TYPE>
  [[nodiscard]] row_id get_current_row(size_t offset, size_t i, const STRING_PTR_AC_TYPE& string_ptrs_ac) const {
    row_id current_row{group_aligned_permutation_[offset + i]};
    // We ignore filtered and NULL strings.
    return (!accepted_rows_.test(current_row) || string_ptrs_ac[current_row] == 0) ? SKIP_ROW : current_row;
  }

  [[nodiscard]] static result_type compute_empty_result(const common::execution_context& context) {
    return create_empty_string_agg_result(context);
  }

  template <typename STRING_PTRS, typename THREAD_LOCAL_TYPE>
  [[nodiscard]] result_type compute_final_result(
      const row_id aggregation_domain_size, const row_id output_row_count, const STRING_PTRS& string_ptrs_ac,
      const process::sort_mappers_result& string_sort_taint_maps,
      const buffer_sizes_and_offsets_type& buffer_sizes_and_offsets,
      const std::array<row_id, process::HASHMAPS>& id_offset, const THREAD_LOCAL_TYPE& thread_local_front_keep,
      const group_result* result_per_group_ptr, const row_id* project_group_id_to_group_table_ptr,
      const std::string_view delimiter, const memory::column_t& string_column, common::execution_context& context,
      const size_t grain_size) const {
    auto to_sort{memory::tracking::make_static_array<sort_agg_group_handle>(
        aggregation_domain_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};

    // initialize the structures for the string dictionary
    auto pointers_sa{memory::tracking::make_static_array_for_overwrite<cel_string_t>(
        aggregation_domain_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG), context)};
    auto string_buffer_sa{memory::tracking::make_static_array_for_overwrite<char>(
        buffer_sizes_and_offsets.string_buffer_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::OUTPUT_COLUMN_MSG),
        context)};

    auto string_dictionary{string_column->get_string_dict(context)};
    const auto strings_data{string_dictionary->get_const_data(context)};

    fill_buffers_and_sort_array_with_strings(string_ptrs_ac, strings_data, string_buffer_sa.get(), to_sort.data(),
                                             string_sort_taint_maps.string_sizes, buffer_sizes_and_offsets, id_offset,
                                             group_aligned_permutation_, accepted_rows_, delimiter,
                                             thread_local_front_keep);

    sort_unique_agg_groups_pu_string_agg(to_sort.data(), aggregation_domain_size);

    auto sort_agg_group_map{memory::tracking::make_static_array_value_init<row_id>(
        aggregation_domain_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};

    // fill the string pointers in sorted dictionary order
    process::assign_sorted_strings(to_sort.data(), sort_agg_group_map.data(), pointers_sa.get(),
                                   aggregation_domain_size, grain_size);

    auto output_column_pointers{memory::execute_with_column_pointers_type(
        exec_execute_fill_output{output_row_count, result_per_group_ptr, sort_agg_group_map.data(),
                                 project_group_id_to_group_table_ptr, grain_size, context},
        aggregation_domain_size)};

    return pu_string_agg_result{std::move(output_column_pointers), std::move(pointers_sa), std::move(string_buffer_sa),
                                aggregation_domain_size};
  }

 private:
  const legacy_embedded_ctl::static_array<row_id>& group_aligned_permutation_;
  const cube::filter_bitset_t& accepted_rows_;
};
#endif

/**
 * A small helper class to abstract the column information needed for the different variant computations
 * @tparam HAS_COLUMN
 */
template <bool HAS_COLUMN>
struct column_info {
  row_id row_count;
};
#ifndef CELOSTAR
template <>
struct column_info<true> {
  row_id row_count;
  const memory::column_t& column;
};
template <typename... MAYBE_COLUMN>
  requires(std::is_convertible_v<MAYBE_COLUMN &&, const memory::column_t&> && ...) && (sizeof...(MAYBE_COLUMN) < 2)
column_info(row_id, MAYBE_COLUMN&&...) -> column_info<(sizeof...(MAYBE_COLUMN) > 0)>;
#endif

/**
 * Compute string aggregation using a parallel algorithm (used by VARIANT and PU_STRING_AGG)
 *
 * On a high-level perspective, this algorithm comprises the following steps:
 *
 * 1. Collect the unique group in a hashmap and assign a unique aggregation group id
 * 2. Compute the string buffer to represent the unique aggregation groups. Additionally, if we compute the VARIANT
 *   operator, we also fill the trace
 * 3. Compute the string sorting and put the string buffer and trace buffer (only VARIANT) pointers into the right order
 * 4. Add the correct column pointers to the grouping table (which is the case table in case of VARIANT)
 *
 * Details on the first step:
 *
 * Computing the unique aggregation group id is done using a two-step hashmap-based algorithm.
 *
 * - First, the input is split in group-aligned blocks, which are distributed among the available threads. Furthermore,
 *   each thread distributes its unique aggregation groups to 64 * different hashmaps (slots), based on a portion of
 *   the aggregation group's hash value, such that there are 64 * #number_of_threads different hash maps.
 * - Second, for each slot, the hashmaps are merged to the first thread's hashmap. This merge is also done in parallel,
 *   as each slot can be handled by a different thread.
 *
 * IMPORTANT: This part of the algorithm assumes that unique aggregation groups span CONSECUTIVE rows in the input
 * column. When this is not the case, it is possible to get undefined behavior/a segfault (see CPL-6385) under very
 * specific conditions:
 *
 * 1. The group_aligned_blocks are aligned in such a way that two blocks contain a group with the same group ID.
 * 2. Two threads (A and B) compute handle_aggregation_group in parallel for these groups.
 * 3. A race occurs in handle_aggregation_group such that A sets result_per_group[last_group_id].slot and B sets
 *    result_per_group[last_group_id].agg_group_id. Hereby it is required that the slot computed by B already contains
 *    more groups than will eventually be present in the slot as computed by A such that the resulting agg_group_id
 *    is greater than the highest agg_group_id in A's slot.
 * 4. The debug assert condition in the parallel for loop of compute_offset_global_agg_group_id will now evaluate to
 *    false and undefined behavior follows.
 *
 */
template <typename OPERATOR_ACCESSOR, typename PROJECTION_TYPE, bool COMPUTE_STRINGS = true>
struct exec_parallel_string_aggregation {
  const OPERATOR_ACCESSOR& accessor;
  common::execution_context& context;
  const PROJECTION_TYPE& projection_vector;
  const row_id output_row_count{};
  column_info<COMPUTE_STRINGS> string_column;
  const std::string_view delimiter;
  const size_t grain_size{};

  template <typename TUPLE>
  decltype(auto) operator()(const TUPLE& tup) {
    return std::invoke(*this, std::get<0>(tup).get_const_accessor());
  }

  template <class ACCESSOR>
    requires std::is_integral_v<std::remove_reference_t<decltype(std::declval<ACCESSOR>()[row_id{}])>>
  decltype(auto) operator()(const ACCESSOR & string_col_ptrs_ac) {
    if (output_row_count == 0) {
#ifndef CELOSTAR
      if constexpr (COMPUTE_STRINGS) {
        return accessor.compute_empty_result(context);
      } else {
#endif
        return compute_empty_result_row_ids(context);
#ifndef CELOSTAR
      }
#endif
    }

    using string_col_ptr_t = std::remove_reference_t<decltype(std::declval<const ACCESSOR&>()[row_id{}])>;

    auto result_per_group{memory::tracking::make_static_array<group_result>(
        static_cast<size_t>(output_row_count), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG),
        context)};

    // atomic variable to get unique values for thread ids
    std::atomic<int> next_thread_id{0};

    eq_agg_group_handle<OPERATOR_ACCESSOR, string_col_ptr_t> eq_cmp{&accessor, string_col_ptrs_ac.get()};
    // deduplicate_local_init is a functor that returns instances of deduplicate_local with already assigned thread_id
    thread_local_deduplicate_type<string_col_ptr_t, eq_agg_group_handle<OPERATOR_ACCESSOR, string_col_ptr_t>>
        thread_local_deduplicate(
            (deduplicate_local_init<string_col_ptr_t, eq_agg_group_handle<OPERATOR_ACCESSOR, string_col_ptr_t>>(
                next_thread_id, eq_cmp)));

    // We create at least 1 thread local storage. This is required since the string column on which we aggregate
    // might be empty and hence no thread would be spawned to process it. This would result in thread_local_deduplicate
    // to be empty, however, the algorithm requires it to have size >= 1.
    thread_local_deduplicate.local();

    auto group_aligned_blocks{
        generate_group_aligned_blocks(accessor, string_column.row_count, projection_vector, grain_size)};

    auto project_group_id_to_group_table{memory::tracking::make_static_array(
        output_row_count, VALUE_NOT_FOUND, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG),
        context)};

    // collect the unique groups in thread_local_deduplicate
    collect_aggregation_groups(accessor, group_aligned_blocks, thread_local_deduplicate, string_col_ptrs_ac,
                               project_group_id_to_group_table, result_per_group, projection_vector);
    const int number_of_threads{next_thread_id};

    //[slot][thread_id][local_agg_group_id]
    std::vector<std::vector<std::vector<row_id>>> thread_slot_local_to_slot_local_maps(process::HASHMAPS);

    auto& thread_local_front{*thread_local_deduplicate.begin()};

    // merge the collected traces among the different threads
    process::merge_collected_traces_build_maps(thread_local_deduplicate, thread_slot_local_to_slot_local_maps,
                                               number_of_threads, thread_local_front);

    auto thread_local_front_keep{std::move(thread_local_front)};

    // clean up - no longer needed
    thread_local_deduplicate.clear();

    std::array<row_id, process::HASHMAPS> id_offset{};

    // The aggregation groups are now collected in their slots, so we know how many traces are in each slots.
    // In the next step, we assign a globally unique aggregation group id to the groups.
    const row_id aggregation_domain_size{
        compute_offset_global_agg_group_id(output_row_count, id_offset, thread_slot_local_to_slot_local_maps,
                                           thread_local_front_keep, result_per_group.data(), grain_size)};

    // clean up - no longer needed
    thread_slot_local_to_slot_local_maps.clear();

#ifndef CELOSTAR
    // in this case, we only want only row ids
    if constexpr (COMPUTE_STRINGS) {
      auto string_sort_taint_maps{process::sort_and_string_mappers(context, string_column.column, delimiter)};

      const auto& string_lengths{string_sort_taint_maps.string_sizes};

      // compute the offsets for the slots in the trace_buffer and string_buffer
      buffer_sizes_and_offsets_type buffer_sizes_and_offsets{compute_buffer_sizes_and_offsets(
          accessor, thread_local_front_keep, string_lengths, string_col_ptrs_ac, delimiter, context)};

      return accessor.compute_final_result(
          aggregation_domain_size, output_row_count, string_col_ptrs_ac, string_sort_taint_maps,
          buffer_sizes_and_offsets, id_offset, thread_local_front_keep, result_per_group.data(),
          project_group_id_to_group_table.data(), delimiter, string_column.column, context, grain_size);
    } else {
#endif
      auto trace_buffer_sizes_and_offsets{compute_trace_buffer_sizes_and_offsets(thread_local_front_keep, context)};

      return compute_final_result_row_ids(aggregation_domain_size, output_row_count, trace_buffer_sizes_and_offsets,
                                          id_offset, thread_local_front_keep, result_per_group,
                                          project_group_id_to_group_table, context, grain_size);
#ifndef CELOSTAR
    }
#endif
  }
};

template <typename OPERATOR_ACCESSOR, typename PROJECTION_TYPE>
exec_parallel_string_aggregation(const OPERATOR_ACCESSOR&, common::execution_context&, const PROJECTION_TYPE&, row_id,
                                 const column_info<true>&, std::string_view, size_t)
    -> exec_parallel_string_aggregation<OPERATOR_ACCESSOR, PROJECTION_TYPE>;

template <typename PROJECTION_TYPE>
using exec_parallel_string_aggregation_internal_t =
    exec_parallel_string_aggregation<variant_accessor, PROJECTION_TYPE, false>;

#ifndef CELOSTAR
legacy_embedded_ctl::static_array<row_id> compute_group_aligned_permutation(
    const memory::management::checked_vector_t<row_id>& permutation, size_t group_count,
    const projection_vector_t& projection, const common::execution_context& context) {
  return std::visit(
      [&](auto& projection) {
        auto offsets{memory::tracking::make_static_array_value_init<row_id>(
            group_count + 2, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};
        for (size_t i{0}; i < permutation.size(); i++) {
          row_id target{projection[i] + 1};
          if (projection[i] == VALUE_NOT_FOUND) {
            target = static_cast<row_id>(group_count) + 1;
          }
          legacy_embedded_debug_assert(target < static_cast<row_id>(offsets.size()));
          offsets[target]++;
        }

        for (size_t i{0}; i < group_count + 1; i++) {
          offsets[i + 1] += offsets[i];
        }

        auto group_aligned_permutation{memory::tracking::make_static_array_for_overwrite<row_id>(
            permutation.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG), context)};
        for (size_t i{0}; i < permutation.size(); i++) {
          const row_id current_row{permutation[i]};
          row_id projected_group{projection[current_row]};
          if (projected_group == VALUE_NOT_FOUND) {
            projected_group = static_cast<row_id>(group_count);
          }
          legacy_embedded_debug_assert(offsets[projected_group] < offsets[projected_group + 1]);
          group_aligned_permutation[offsets[projected_group]] = current_row;
          offsets[projected_group]++;
        }

        return group_aligned_permutation;
      },
      projection);
}

using column_ptr_variant_type = std::variant<memory::details::const_column_ptrs_accessor<memory::col_ptr_8_t>,
                                             memory::details::const_column_ptrs_accessor<memory::col_ptr_16_t>,
                                             memory::details::const_column_ptrs_accessor<memory::col_ptr_32_t>,
                                             memory::details::const_column_ptrs_accessor<memory::col_ptr_64_t>>;
#endif

}  // namespace

#ifndef CELOSTAR
memory::builders::result_column_builder_t execute_variant_operator(
    common::execution_context& context, const std::string& cache_key,
    cube::variant_trace_cache_manager& variant_trace_cache_manager_instance, memory::table* case_table,
    const memory::join_projection_vector_t& projection_vector, const memory::column_t& activity_column,
    const size_t grain_size) {
  constexpr std::string_view VARIANT_DELIMITER{", "};

  variant_accessor accessor{};
  auto exec_func = [&](const auto& projection_vector) {
    return memory::cast_execute_column_pointers(
        exec_parallel_string_aggregation{accessor, context, projection_vector, case_table->get_rows(),
                                         column_info{activity_column->get_row_count(context), activity_column},
                                         VARIANT_DELIMITER, grain_size},
        activity_column->get_column_pointers(context));
  };
  auto result{memory::cast_execute_projection_vector(exec_func, projection_vector)};

  // create and store the trace cache for later lookups. we have to store the trace cache first, otherwise
  // we might run into race conditions.
  variant_trace_cache_manager_instance.create_and_store_variant_cache(
      cache_key, activity_column->get_owner()->get_name(), std::move(result.trace_ptrs),
      std::move(result.trace_buffer_data), std::move(result.trace_lengths));

  return memory::builders::cache_column_from_dictionary::builder::get()
      ->set_raw_column_pointer(std::move(result.output_column_pointers))
      ->set_raw_dictionary(std::move(result.pointers_sa))
      ->set_string_buffer(std::move(result.string_buffer_sa))
      ->build();
}

memory::builders::result_column_builder_t execute_parallel_string_agg_operator(
    const std::string& op_name, common::execution_context& context, const projection_vector_t& projection,
    const std::shared_ptr<cube::filter_bitset_t>& accepted_rows, const memory::column_t& source_column,
    const row_id group_count, const std::string& delimiter,
    const legacy_embedded_ctl::static_array<row_id>& group_aligned_permutation, const size_t grain_size) {
  if (!source_column->is_cel_string_type()) {
    source_column->maybe_untyped_null_constant()
        ? throw common::cpm_exception("{}: First parameter must be of type STRING, but got a NULL constant.", op_name)
        : throw common::cpm_exception("{}: First parameter must be of type STRING, but got [{}].", op_name,
                                      convert_to_string(source_column->get_data_type()));
  }

  pu_string_agg_accessor accessor{group_aligned_permutation, *accepted_rows};
  auto string_agg_result{std::visit(
      [&](auto& projection) {
        return memory::cast_execute_column_pointers(
            exec_parallel_string_aggregation{accessor, context, projection, group_count,
                                             column_info{source_column->get_row_count(context), source_column},
                                             delimiter, grain_size},
            source_column->get_column_pointers(context));
      },
      projection)};

  return memory::builders::cache_column_from_dictionary::builder::get()
      ->set_raw_column_pointer(std::move(string_agg_result.output_column_pointers))
      ->set_raw_dictionary(std::move(string_agg_result.pointers_sa))
      ->set_string_buffer(std::move(string_agg_result.string_buffer_sa))
      ->build();
}

legacy_embedded_ctl::static_array<row_id> compute_group_aligned_permutation_simple(
    const memory::column_t& source_column, common::execution_context& context, const projection_vector_t& projection,
    row_id target_table_size) {
  memory::management::checked_vector_t<row_id> default_permutation{
      static_cast<size_t>(source_column->get_row_count(context)), 0,
      memory::management::checked_allocator<decltype(default_permutation)>(
          context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::PERMUTATION_VECTOR_MSG))};
  std::iota(default_permutation.begin(), default_permutation.end(), row_id{0});
  return compute_group_aligned_permutation(default_permutation, static_cast<size_t>(target_table_size), projection,
                                           context);
}

legacy_embedded_ctl::static_array<row_id> compute_group_aligned_permutation_order_by_columns(
    const std::string& op_name, const memory::column_t& source_column, common::execution_context& context,
    const cube::query_scope& query_scope, const std::vector<memory::column_t>& orderby_columns,
    const std::vector<OrderByDirection>& orderby_directions, const projection_vector_t& projection,
    row_id target_table_size) {
  auto col_ptrs{common::cache_column_ptrs_with_sort_order_no_pullup(orderby_columns, orderby_directions, query_scope,
                                                                    context, op_name)};
  const auto order_by_permutation{common::compute_order_by(source_column->get_row_count(context), col_ptrs, context)};
  return compute_group_aligned_permutation(order_by_permutation, static_cast<size_t>(target_table_size), projection,
                                           context);
}

std::pair<cube::execution::columns_and_common_table, std::vector<OrderByDirection>>
compute_orderby_fetcher_and_order_directions(const OrderByExpressions& order_by_expressions,
                                             const cube::query_scope& query_scope,
                                             const common::execution_context& context,
                                             const memory::column_lookup_t& order_by_expression_columns) {
  cube::execution::columns_and_common_table_builder builder{};
  std::vector<OrderByDirection> order_directions{};
  order_directions.reserve(order_by_expressions.expressions_size());

  for (const auto& orderby_column : order_by_expressions.expressions()) {
    order_directions.emplace_back(orderby_column.order_by_direction());
    builder.add_column(
        memory::resolve_operator_ref_id(order_by_expression_columns, orderby_column.order_by_ref().operator_ref_id()),
        query_scope, context);
  }

  return {std::move(builder).build(), order_directions};
}
#endif

namespace {

constexpr std::string_view VARIANT_DELIMITER{", "};

template <typename ACCESSOR>
[[nodiscard]] memory::cache::variant_trace_cache_t compute_variant_row_ids_impl(
    const common::execution_context& context, const std::string& cache_key, const std::string& activity_table_name,
    row_id num_case_rows, const memory::join_projection_vector_t& projection_vector, const ACCESSOR& activity_column,
    cube::variant_trace_cache_manager& variant_trace_cache_manager_instance, size_t grain_size) {
  auto compute_variants_context{context.create_sub_context("compute_variant_row_ids_impl", {})};
#ifndef CELOSTAR
  // Before computing it, first check if entry already exists
  if (auto variant_trace_cache_entry{variant_trace_cache_manager_instance.retrieve_variant_cache_col_ptrs(cache_key)};
      variant_trace_cache_entry != nullptr) {
    // Return existing cache entry
    return variant_trace_cache_entry;
  }
#endif

  variant_accessor accessor{};

  auto exec_func = [&](const auto& projection_vector) {
    return exec_parallel_string_aggregation_internal_t<decltype(projection_vector)>{
        accessor,
        compute_variants_context,
        projection_vector,
        num_case_rows,
        column_info<false>{legacy_embedded_ctl::cast<row_id>(activity_column.size())},
        VARIANT_DELIMITER,
        grain_size}(activity_column);
  };

  auto variant_trace_result{memory::cast_execute_projection_vector(exec_func, projection_vector)};

#ifdef CELOSTAR
  return cube::make_non_cached_variant_entries_with_group_mapping(
             std::move(variant_trace_result.trace_ptrs), std::move(variant_trace_result.trace_buffer_data),
             std::move(variant_trace_result.trace_lengths), std::move(variant_trace_result.group_id_to_trace_id))
      .underlying();
#else
  variant_trace_cache_manager_instance.create_and_store_variant_cache_col_ptrs(
      cache_key, activity_table_name, std::move(variant_trace_result.trace_ptrs),
      std::move(variant_trace_result.trace_buffer_data), std::move(variant_trace_result.trace_lengths),
      std::move(variant_trace_result.group_id_to_trace_id));

  return variant_trace_cache_manager_instance.retrieve_variant_cache_col_ptrs(cache_key);
#endif
}

template <typename VALUES_ACCESSOR>
[[nodiscard]] memory::cache::variant_entries_t compute_temporary_variant_row_ids_impl(  //
    const row_id number_of_groups,                                                      //
    const memory::value_idx_to_group_id_mapping_t& value_idx_to_group_id_mapping,       //
    const VALUES_ACCESSOR& values,                                                      //
    const common::execution_context& context,                                           //
    const size_t grain_size) {
  auto compute_variants_context{context.create_sub_context("compute_temporary_variant_row_ids_impl", {})};

  variant_accessor accessor{};
  auto variant_trace_result{memory::cast_execute_projection_vector(
      [&](const auto& mapping) {
        return exec_parallel_string_aggregation_internal_t<decltype(mapping)>{
            accessor,
            compute_variants_context,
            mapping,
            number_of_groups,
            column_info<false>{legacy_embedded_ctl::cast<row_id>(values.size())},
            VARIANT_DELIMITER,
            grain_size}(values);
      },
      value_idx_to_group_id_mapping)};

  return cube::make_non_cached_variant_entries_with_group_mapping(                    //
      cube::trace_array_t{std::move(variant_trace_result.trace_ptrs)},                //
      cube::trace_buffer_array_t{std::move(variant_trace_result.trace_buffer_data)},  //
      cube::trace_length_array_t{std::move(variant_trace_result.trace_lengths)},      //
      std::move(variant_trace_result.group_id_to_trace_id));
}

}  // namespace

std::string make_generalized_variant_row_ids_computation_cache_key(const memory::column_t& values,
                                                                   const std::string& mapping_cache_key) {
  // Will be either <table_name>.<column name/cache key> or, if the column has no owner, simply <column name/cache key>
  const std::string variants_source_name{[&values]() {
    auto source_column_tag{
        fmt::format("<{}>", values->get_cache_key().empty() ? values->get_name() : values->get_cache_key())};
    const memory::raw_table_ptr_t owner_table{values->get_owner()};
    if (owner_table != nullptr) {
      return fmt::format("<{}>.{}", owner_table->get_name(), source_column_tag);
    }
    return source_column_tag;
  }()};
  static constexpr std::string_view VARIANT_ROW_IDS_COMPUTATION_CACHE_KEY_PREFIX{"$$VARIANT_ROW_IDS$$"};
  return fmt::format("{}_{}_SOURCE_{}", VARIANT_ROW_IDS_COMPUTATION_CACHE_KEY_PREFIX, mapping_cache_key,
                     variants_source_name);
}

memory::cache::variant_entries_t generalized_variant_row_ids_computation(      //
    const optional_caching_meta_data_t optional_caching_meta_data,             //
    memory::group_id_mapping_and_group_id_domain mapping_and_group_id_domain,  //
    const memory::column_t& values,                                            //
    const common::execution_context& context,                                  //
    const size_t grain_size) {
  const auto& mapping_value{mapping_and_group_id_domain.value};
  const auto size{static_cast<row_id>(memory::get_projection_vector_size(mapping_value))};
  common::runtime_assert(values->get_row_count(context) == size,
                         "Size mismatch: The number of values [{}] and the group mapping size [{}] must match.",
                         values->get_row_count(context), size);
  const auto group_id_domain{mapping_and_group_id_domain.get_or_compute_group_id_domain()};
  return memory::cast_execute_column_pointers(
      [&]<typename TUPLE>(const TUPLE& tup) -> memory::cache::variant_entries_t {
        const auto value_accessor{std::get<0>(tup).get_const_accessor()};
        if (optional_caching_meta_data.has_value()) {
          // As the caching_meta_data is set, we want to compute 'permanent' cacheable variant entries
          const auto cache_key{make_generalized_variant_row_ids_computation_cache_key(
              values, optional_caching_meta_data->group_ids_cache_key)};
          return compute_variant_row_ids_impl(context, cache_key, values->get_owner()->get_name(), group_id_domain,
                                              mapping_value, value_accessor,
                                              optional_caching_meta_data->variant_cache_manager_instance, grain_size);
        }
        // As the variant trace cache manager instance is not set, we want to compute 'temporary' variant entries
        return compute_temporary_variant_row_ids_impl(group_id_domain, mapping_value, value_accessor, context,
                                                      grain_size);
      },
      values->get_column_pointers(context));
}

#ifdef CELOSTAR
memory::cache::variant_trace_cache_t compute_variant_row_ids(
    const memory::table_to_column_projection& table_to_column_projection, const common::execution_context& context,
    const size_t grain_size) {
  const auto& case_table{table_to_column_projection.table_one_side};
  const auto& activity_column{table_to_column_projection.column_n_side};
  const auto& projection_vector{table_to_column_projection.projection};

  return generalized_variant_row_ids_computation(
             std::nullopt, {.value = projection_vector, .optional_group_id_domain = case_table->get_rows()},
             activity_column, context, grain_size)
      .underlying();
}
#else
memory::cache::variant_trace_cache_t compute_variant_row_ids(
    const memory::table_to_column_projection& table_to_column_projection,
    cube::variant_trace_cache_manager& variant_trace_cache_manager_instance, const common::execution_context& context,
    const size_t grain_size) {
  const auto& case_table{table_to_column_projection.table_one_side};
  const auto& activity_column{table_to_column_projection.column_n_side};
  const auto& projection_vector{table_to_column_projection.projection};

  return generalized_variant_row_ids_computation(
             caching_meta_data{.group_ids_cache_key = case_table->get_name(),
                               .variant_cache_manager_instance = variant_trace_cache_manager_instance},
             {.value = projection_vector, .optional_group_id_domain = case_table->get_rows()}, activity_column, context,
             grain_size)
      .underlying();
}
#endif

[[nodiscard]] memory::cache::variant_trace_cache_t compute_variant_row_ids(
    common::execution_context& context, const std::string& cache_key, const std::string& activity_table_name,
    row_id num_case_rows, const memory::join_projection_vector_t& projection_vector,
    const legacy_embedded_ctl::shared_static_array<row_id>& activity_column,
    cube::variant_trace_cache_manager& variant_trace_cache_manager_instance, size_t grain_size) {
  return compute_variant_row_ids_impl(context, cache_key, activity_table_name, num_case_rows, projection_vector,
                                      activity_column, variant_trace_cache_manager_instance, grain_size);
}

#ifndef CELOSTAR
variant_row_id_result compute_variant_row_ids(common::execution_context& context, row_id num_case_rows,
                                              const memory::join_projection_vector_t& projection_vector,
                                              const legacy_embedded_ctl::shared_static_array<row_id>& activity_column,
                                              size_t grain_size) {
  auto compute_variants_context{context.create_sub_context("compute_variant_row_ids", {})};
  variant_accessor accessor{};
  auto exec_func = [&](const auto& projection_vector) {
    return exec_parallel_string_aggregation_internal_t<decltype(projection_vector)>{
        accessor,
        compute_variants_context,
        projection_vector,
        num_case_rows,
        column_info<false>{legacy_embedded_ctl::cast<row_id>(activity_column.size())},
        VARIANT_DELIMITER,
        grain_size}(activity_column);
  };

  return memory::cast_execute_projection_vector(exec_func, projection_vector);
}
#endif

}  // namespace celonis::accelerator::operators::aggregation
