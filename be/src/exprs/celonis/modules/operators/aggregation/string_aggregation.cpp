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

#include <ctl/assert.h>
#include <ctl/conversion.h>
#include <ctl/interval.h>
#include <ctl/static_array.h>

#include "modules/common/trace_types.h"
#include "modules/cube/variant_trace_utils.h"
#include "modules/memory/column.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/transform/join_projection_factories.h"
#include "modules/operators/process/variant_operator_common.h"

namespace celonis::accelerator::operators::aggregation {

namespace {

struct variant_row_id_result {
  ctl::static_array<trace_type> trace_ptrs;
  ctl::static_array<trace_buffer_type> trace_buffer_data;
  ctl::static_array<trace_length_type> trace_lengths;
  common::owned_column_ptr_data_t group_id_to_trace_id;
  row_id num_unique_variants;
};

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
  ctl::static_array<int64_t> slot_trace_buffer_offset;

  trace_buffer_sizes_and_offsets_type(const int64_t trace_buffer_size,
                                      ctl::static_array<int64_t> slot_trace_buffer_offset)
      : trace_buffer_size{trace_buffer_size}, slot_trace_buffer_offset{std::move(slot_trace_buffer_offset)} {}
};

/**
 * Return type containing all the offsets for parallel filling of the buffers.
 */
struct buffer_sizes_and_offsets_type {
  int64_t string_buffer_size;
  ctl::static_array<int64_t> slot_string_buffer_offset;
  // The following two members are specific to the VARIANT operator.
  int64_t trace_buffer_size;
  ctl::static_array<int64_t> slot_trace_buffer_offset;

  buffer_sizes_and_offsets_type(const int64_t string_buffer_size, ctl::static_array<int64_t> slot_string_buffer_offset,
                                const int64_t trace_buffer_size, ctl::static_array<int64_t> slot_trace_buffer_offset)
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
    ctl::static_array<row_id>& project_group_id_to_group_table, ctl::static_array<group_result>& result_per_group,
    const PROJECTION_TYPE projection_vector) {
  if (last_group_id == VALUE_NOT_FOUND) {
    return;
  }

  current_agg_group.length = row - current_agg_group.row_start;

  // negative length does not make sense
  debug_assert(current_agg_group.length > 0);

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
  result_per_group[last_group_id].slot = ctl::cast<int>(slot);
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
    const OPERATOR_ACCESSOR& accessor, const std::vector<ctl::half_open_interval<row_id>>& group_aligned_blocks,
    thread_local_deduplicate_type<
        std::remove_reference_t<decltype(std::declval<const STRING_PTR_AC_TYPE&>()[row_id{}])>,
        eq_agg_group_handle<OPERATOR_ACCESSOR,
                            std::remove_reference_t<decltype(std::declval<const STRING_PTR_AC_TYPE&>()[row_id{}])>>>&
        thread_local_deduplicate_step_1,
    const STRING_PTR_AC_TYPE& string_ptrs_ac, ctl::static_array<row_id>& project_group_id_to_group_table,
    ctl::static_array<group_result>& result_per_group, const PROJECTION_TYPE& projection) {
  using string_col_ptr_t = std::remove_reference_t<decltype(std::declval<const STRING_PTR_AC_TYPE&>()[row_id{}])>;

  // Deduplication loop - parallelization by group_aligned_blocks
  tbb::parallel_for_each(group_aligned_blocks, [&](ctl::half_open_interval<row_id> block_data) {
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
  debug_assert(id_offset[0] == 1);

  // adjust agg_group_id
  tbb::parallel_for(
      tbb::blocked_range<row_id>{0, static_cast<row_id>(output_row_count), grain_size}, [&](const auto range) {
        for (row_id i{range.begin()}; i < range.end(); ++i) {
          const int slot{result_per_group_ptr[i].slot};
          // slot == -1 means that the case is a NULL trace case
          if (slot != -1) {
            debug_assert(static_cast<size_t>(result_per_group_ptr[i].agg_group_id) <
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
  auto required_trace_buffer_sizes{
      ctl::make_static_array_for_overwrite<int64_t>(process::HASHMAPS, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

  debug_assert(thread_local_front.set.size() == required_trace_buffer_sizes.size());

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

/**
 * Fills the trace buffer and the to_sort array.
 *
 * Note that we do not fill the trace_ptrs / trace_length arrays here. We only set the location of the NULL variant.
 * The trace_ptrs and trace_length arrays are filled once we have the sorted trace buffer.
 */
template <typename ACTIVITY_PTR_TYPE, typename OPERATOR_ACCESSOR>
void fill_trace_buffer_and_sort_array(
    ctl::static_array<trace_type>& trace_ptrs_array, ctl::static_array<trace_length_type>& trace_length_array,
    ctl::static_array<int16_t>& trace_buffer_data, const trace_buffer_sizes_and_offsets_type& buffer_sizes_and_offsets,
    const std::array<row_id, process::HASHMAPS>& id_offset, ctl::static_array<sort_row_ids_handle>& to_sort,
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

        debug_assert(variant_id != 0);

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

void sort_unique_agg_groups_row_ids(ctl::static_array<sort_row_ids_handle>& to_sort) {
  // skip the NULL entry by starting at the first entry
  tbb::parallel_sort(std::next(begin(to_sort)), end(to_sort),
                     [](const sort_row_ids_handle& h1, const sort_row_ids_handle& h2) {
                       std::span h1_trace{h1.trace_pointer, static_cast<size_t>(h1.length)};
                       std::span h2_trace{h2.trace_pointer, static_cast<size_t>(h2.length)};

                       return std::ranges::lexicographical_compare(h1_trace, h2_trace);
                     });
}

template <ctl::one_of<sort_agg_group_handle, sort_row_ids_handle> SORT_HANDLE_TYPE>
void assign_sorted_traces(row_id aggregation_domain_size, ctl::static_array<trace_type>& traces_data,
                          ctl::static_array<trace_buffer_type>& sorted_traces_buffer,
                          ctl::static_array<trace_length_type>& trace_lengths_data,
                          const ctl::static_array<SORT_HANDLE_TYPE>& to_sort,
                          ctl::static_array<row_id>& sort_variant_map, ctl::static_array<cel_string_t>& pointers_sa) {
  // Should never be zero since we always have at least the empty variant
  debug_assert(aggregation_domain_size != 0);

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

        debug_assert(to_sort[i].orig_id < aggregation_domain_size);
        // map for trace and string column pointers
        sort_variant_map[to_sort[i].orig_id] = i;
      }
    }
  });

  // ensure that all variant ids are still valid
  for (row_id i{0}; i < aggregation_domain_size; ++i) {
    debug_assert(sort_variant_map[i] < aggregation_domain_size);
  }
}

void assign_sorted_traces(row_id aggregation_domain_size, ctl::static_array<trace_type>& traces_data,
                          ctl::static_array<trace_buffer_type>& sorted_traces_buffer,
                          ctl::static_array<trace_length_type>& trace_lengths_data,
                          const ctl::static_array<sort_row_ids_handle>& to_sort,
                          ctl::static_array<row_id>& sort_variant_map, common::execution_context& context) {
  ctl::static_array<cel_string_t> dummy_pointers{
      ctl::make_static_array_for_overwrite<cel_string_t>(0, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

  assign_sorted_traces(aggregation_domain_size, traces_data, sorted_traces_buffer, trace_lengths_data, to_sort,
                       sort_variant_map, dummy_pointers);
}

template <typename OPERATOR_ACCESSOR, typename PROJECTION_TYPE>
std::vector<ctl::half_open_interval<row_id>> generate_group_aligned_blocks(const OPERATOR_ACCESSOR& accessor,
                                                                           const row_id row_count,
                                                                           const PROJECTION_TYPE& projection_vector,
                                                                           const size_t grain_size) {
  const auto grain_size_row_id{static_cast<row_id>(grain_size)};

  std::vector<ctl::half_open_interval<row_id>> blocks{};
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
  ctl::static_array<cel_string_t> pointers_sa;
  ctl::static_array<char> string_buffer_sa;
  row_id dict_size;
};

struct variant_op_result {
  memory::raw_column_ptrs_t output_column_pointers;
  ctl::static_array<cel_string_t> pointers_sa;
  ctl::static_array<char> string_buffer_sa;
  ctl::static_array<trace_type> trace_ptrs;
  ctl::static_array<trace_buffer_type> trace_buffer_data;
  ctl::static_array<trace_length_type> trace_lengths;
  row_id num_unique_variants;
};

[[nodiscard]] variant_row_id_result create_empty_variant_row_id_result(const common::execution_context& context) {
  static constexpr trace_length_type EMPTY_TRACE_LENGTH{0};
  auto trace_cache_data_sorted{
      ctl::make_static_array_value_init<trace_buffer_type>(EMPTY_TRACE_LENGTH, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};
  auto trace_lengths{
      ctl::make_static_array<trace_length_type>({EMPTY_TRACE_LENGTH}, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};
  auto traces{
      ctl::make_static_array<trace_type>({trace_cache_data_sorted.data()}, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

  auto group_id_to_trace_id{common::create_owned_column_data(0, 1, memory::zero_init_t{true}, context)};

  return {std::move(traces), std::move(trace_cache_data_sorted), std::move(trace_lengths),
          std::move(group_id_to_trace_id), 1};
}

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
};

[[nodiscard]] variant_row_id_result compute_empty_result_row_ids(const common::execution_context& context) {
  return create_empty_variant_row_id_result(context);
}

template <typename THREAD_LOCAL_TYPE>
[[nodiscard]] variant_row_id_result compute_final_result_row_ids(
    const row_id aggregation_domain_size, const row_id output_row_count,
    const trace_buffer_sizes_and_offsets_type& trace_buffer_sizes_and_offsets,
    const std::array<row_id, process::HASHMAPS>& id_offset, const THREAD_LOCAL_TYPE& thread_local_front_keep,
    const ctl::static_array<group_result>& result_per_group,
    const ctl::static_array<row_id>& project_group_id_to_group_table, common::execution_context& context,
    const size_t grain_size) {
  auto to_sort{ctl::make_static_array_for_overwrite<sort_row_ids_handle>(aggregation_domain_size,
                                                                         ALLOC_MSG(ctl::RAW_DATA_ALLOC_MSG))};

  // variant_id_to_trace[variant_id] points into the sorted_trace_data_buffer for variant with id 'variant_id'
  auto variant_id_to_trace{
      ctl::make_static_array_for_overwrite<trace_type>(aggregation_domain_size, ALLOC_MSG(ctl::RAW_DATA_ALLOC_MSG))};

  // variant_id_to_length[variant_id]: gives the length of variant with id 'variant_id'
  auto variant_id_to_trace_length{ctl::make_static_array_for_overwrite<trace_length_type>(
      aggregation_domain_size, ALLOC_MSG(ctl::RAW_DATA_ALLOC_MSG))};

  // Stores the actual variant trace data i.e. row ids from the activity column representing the trace
  // this is int16_t since there can only be 2^15 - 1 == 32767 distinct activity types
  auto trace_data_buffer{ctl::make_static_array_value_init<int16_t>(trace_buffer_sizes_and_offsets.trace_buffer_size,
                                                                    ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

  auto sorted_trace_data_buffer{ctl::make_static_array_value_init<int16_t>(
      trace_buffer_sizes_and_offsets.trace_buffer_size, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

  fill_trace_buffer_and_sort_array(variant_id_to_trace, variant_id_to_trace_length, trace_data_buffer,
                                   trace_buffer_sizes_and_offsets, id_offset, to_sort, thread_local_front_keep);

  sort_unique_agg_groups_row_ids(to_sort);

  auto unsorted_to_sorted_variant_id_map{
      ctl::make_static_array_value_init<row_id>(aggregation_domain_size, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

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

/**
 * A small helper class to abstract the column information needed for the different variant computations
 * @tparam HAS_COLUMN
 */
template <bool HAS_COLUMN>
struct column_info {
  row_id row_count;
};

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
  decltype(auto) operator()(const ACCESSOR& string_col_ptrs_ac) {
    if (output_row_count == 0) {
      return compute_empty_result_row_ids(context);
    }

    using string_col_ptr_t = std::remove_reference_t<decltype(std::declval<const ACCESSOR&>()[row_id{}])>;

    auto result_per_group{ctl::make_static_array<group_result>(static_cast<size_t>(output_row_count),
                                                               ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

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

    auto project_group_id_to_group_table{
        ctl::make_static_array(output_row_count, VALUE_NOT_FOUND, ALLOC_MSG(ctl::TEMPORARY_STORAGE_MSG))};

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

    auto trace_buffer_sizes_and_offsets{compute_trace_buffer_sizes_and_offsets(thread_local_front_keep, context)};

    return compute_final_result_row_ids(aggregation_domain_size, output_row_count, trace_buffer_sizes_and_offsets,
                                        id_offset, thread_local_front_keep, result_per_group,
                                        project_group_id_to_group_table, context, grain_size);
  }
};

template <typename OPERATOR_ACCESSOR, typename PROJECTION_TYPE>
exec_parallel_string_aggregation(const OPERATOR_ACCESSOR&, common::execution_context&, const PROJECTION_TYPE&, row_id,
                                 const column_info<true>&, std::string_view, size_t)
    -> exec_parallel_string_aggregation<OPERATOR_ACCESSOR, PROJECTION_TYPE>;

template <typename PROJECTION_TYPE>
using exec_parallel_string_aggregation_internal_t =
    exec_parallel_string_aggregation<variant_accessor, PROJECTION_TYPE, false>;

constexpr std::string_view VARIANT_DELIMITER{", "};

template <typename ACCESSOR>
[[nodiscard]] memory::cache::variant_trace_cache_t compute_variant_row_ids_impl(
    const common::execution_context& context, row_id num_case_rows,
    const memory::join_projection_vector_t& projection_vector, const ACCESSOR& activity_column, size_t grain_size) {
  auto compute_variants_context{context.create_sub_context("compute_variant_row_ids_impl", {})};

  variant_accessor accessor{};

  auto exec_func = [&](const auto& projection_vector) {
    return exec_parallel_string_aggregation_internal_t<decltype(projection_vector)>{
        accessor,
        compute_variants_context,
        projection_vector,
        num_case_rows,
        column_info<false>{ctl::cast<row_id>(activity_column.size())},
        VARIANT_DELIMITER,
        grain_size}(activity_column);
  };

  auto variant_trace_result{memory::cast_execute_projection_vector(exec_func, projection_vector)};

  return cube::make_non_cached_variant_entries_with_group_mapping(
             std::move(variant_trace_result.trace_ptrs), std::move(variant_trace_result.trace_buffer_data),
             std::move(variant_trace_result.trace_lengths), std::move(variant_trace_result.group_id_to_trace_id))
      .underlying();
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
            column_info<false>{ctl::cast<row_id>(values.size())},
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
    const auto& optional_table_config{values->optional_table_config()};
    if (optional_table_config.has_value()) {
      return fmt::format("<{}>.{}", optional_table_config->table_name(), source_column_tag);
    }
    return source_column_tag;
  }()};
  static constexpr std::string_view VARIANT_ROW_IDS_COMPUTATION_CACHE_KEY_PREFIX{"$$VARIANT_ROW_IDS$$"};
  return fmt::format("{}_{}_SOURCE_{}", VARIANT_ROW_IDS_COMPUTATION_CACHE_KEY_PREFIX, mapping_cache_key,
                     variants_source_name);
}

/**
 * @brief Interface for a 'generalized' internal variant computation.
 * Here 'generalized' refers to the following:
 * - no case-events relationship is expected for the input
 * - allowing for temporary (non-cached/non-swappable) AND permanent variant entries
 * - the mapping does not need any semantics for its group ID values (such as referring to the case id table)
 * @param mapping_and_group_id_domain Contains the mapping from a value index to its group ID and optionally the domain
 * @param values The values to aggregate into groups (TODO:n.weber: Generalize to some accessor to work with arrays)
 * @return Either 'permanent' or 'temporary' variant entries
 *
 * @note: This function guarantees that variant-ids are assigned in a stable manner: that is, between two
 * calls to the function with the same input, ids assigned to variants will not change. This is useful if the output
 * of an operator depends on variant-id assignment.
 *
 */
[[nodiscard]] memory::cache::variant_entries_t generalized_variant_row_ids_computation(  //
    memory::group_id_mapping_and_group_id_domain mapping_and_group_id_domain,            //
    const memory::column_t& values,                                                      //
    const common::execution_context& context,                                            //
    const size_t grain_size = operators::process::COMPUTE_VARIANTS_GRAIN_SIZE) {
  const auto& mapping_value{mapping_and_group_id_domain.value};
  const auto size{static_cast<row_id>(memory::get_projection_vector_size(mapping_value))};
  common::runtime_assert(values->get_row_count() == size,
                         "Size mismatch: The number of values [{}] and the group mapping size [{}] must match.",
                         values->get_row_count(), size);
  const auto group_id_domain{mapping_and_group_id_domain.get_or_compute_group_id_domain()};
  return memory::cast_execute_column_pointers(
      [&]<typename TUPLE>(const TUPLE& tup) -> memory::cache::variant_entries_t {
        const auto value_accessor{std::get<0>(tup).get_const_accessor()};
        // As the variant trace cache manager instance is not set, we want to compute 'temporary' variant entries
        return compute_temporary_variant_row_ids_impl(group_id_domain, mapping_value, value_accessor, context,
                                                      grain_size);
      },
      values->get_column_pointers(context));
}

memory::cache::variant_trace_cache_t compute_variant_row_ids(
    const memory::table_to_column_projection& table_to_column_projection, const common::execution_context& context,
    const size_t grain_size) {
  const auto& case_table_size{table_to_column_projection.table_one_side_size};
  const auto& activity_column{table_to_column_projection.column_n_side};
  const auto& projection_vector{table_to_column_projection.projection};

  return generalized_variant_row_ids_computation(
             {.value = projection_vector, .optional_group_id_domain = case_table_size}, activity_column, context,
             grain_size)
      .underlying();
}

[[nodiscard]] memory::cache::variant_trace_cache_t compute_variant_row_ids(
    common::execution_context& context, [[maybe_unused]] const std::string& cache_key,
    [[maybe_unused]] const std::string& activity_table_name, row_id num_case_rows,
    const memory::join_projection_vector_t& projection_vector, const ctl::shared_static_array<row_id>& activity_column,
    size_t grain_size) {
  return compute_variant_row_ids_impl(context, num_case_rows, projection_vector, activity_column, grain_size);
}

}  // namespace celonis::accelerator::operators::aggregation
