#pragma once

#include <algorithm>
#include <numeric>

#include <tbb/enumerable_thread_specific.h>
#include <tbb/parallel_for.h>

#include <ctl/assert.h>
#include <ctl/bitset.h>

namespace celonis::accelerator::operators::process {

/**
 * Key for the deduplication hash map
 */
template <class ACTIVITY_PTR_TYPE>
struct variant_set_handle {
  /// Hash of the trace after removing cycles
  size_t hashvalue{0};
  /// Array representing the trace
  const ACTIVITY_PTR_TYPE* trace_pointer{nullptr};
  /// Length of trace including cycles (e.g. A-B-B-B-C has length 5), i.e., length of the trace_pointer
  row_id length{0};
  /// Length of trace after removing cycles (e.g. A-B-B-B-C has length 3 with max_cycle_length = 1)
  row_id output_length{0};
  /// Row index of the trace's first entry
  row_id row_start{0};
};

/**
 * Functor to get precomputed hashvalue field
 */
template <class ACTIVITY_PTR_TYPE>
struct get_hash {
  size_t operator()(const variant_set_handle<ACTIVITY_PTR_TYPE>& handle) const { return handle.hashvalue; }
};

/**
 * Functor for equality comparison of two variant set handles
 */
template <class ACTIVITY_PTR_TYPE>
struct eq_variant_set_handle {
  bool operator()(const variant_set_handle<ACTIVITY_PTR_TYPE>& h1, const variant_set_handle<ACTIVITY_PTR_TYPE>& h2) {
    debug_assert(h1.trace_pointer != nullptr && h2.trace_pointer != nullptr);
    if (h1.output_length != h2.output_length) {
      return false;
    }
    auto* h1_ptr = h1.trace_pointer;
    auto* h2_ptr = h2.trace_pointer;
    auto* h1_end_ptr = h1_ptr + h1.length;
    auto* h2_end_ptr = h2_ptr + h2.length;
    while (h1_ptr != h1_end_ptr && h2_ptr != h2_end_ptr) {
      // Skip NULL activities
      if (*h1_ptr == 0) {
        ++h1_ptr;
        continue;
      }
      if (*h2_ptr == 0) {
        ++h2_ptr;
        continue;
      }
      // non-match - not equal
      if (*h1_ptr != *h2_ptr) {
        return false;
      }
      // go to next pair
      h1_ptr++;
      h2_ptr++;
    }
    // Skip NULL activities (required to be correct if one of the traces ends with NULL activity)
    while (h1_ptr != h1_end_ptr && *h1_ptr == 0) {
      h1_ptr++;
    }
    while (h2_ptr != h2_end_ptr && *h2_ptr == 0) {
      h2_ptr++;
    }
    // traces are equal if both pointers made it to the end of the trace
    return h1_ptr == h1_end_ptr && h2_ptr == h2_end_ptr;
  }
};

static constexpr int HASHMAPS{1 << 6};  // 64

template <class THREAD_LOCAL_TYPE>
void merge_collected_traces_build_maps(
    tbb::enumerable_thread_specific<THREAD_LOCAL_TYPE>& thread_local_deduplicate,
    std::vector<std::vector<std::vector<row_id>>>& thread_slot_local_to_slot_local_maps, int number_of_threads,
    THREAD_LOCAL_TYPE& thread_local_front) {
  auto thread_local_it = thread_local_deduplicate.begin();

  // initialize thread_slot_local_to_slot_local map of front
  for (int slot = 0; slot < HASHMAPS; ++slot) {
    thread_slot_local_to_slot_local_maps[slot].resize(number_of_threads);
    thread_slot_local_to_slot_local_maps[slot][thread_local_front.thread_id].resize(
        thread_local_front.set[slot].size());
    std::iota(thread_slot_local_to_slot_local_maps[slot][thread_local_front.thread_id].begin(),
              thread_slot_local_to_slot_local_maps[slot][thread_local_front.thread_id].end(), static_cast<row_id>(0));
  }

  // merge the variants to thread 0 and within the same slots - parallelization by slot
  tbb::parallel_for(tbb::blocked_range<int>{0, HASHMAPS, 1}, [&](const auto range) {
    for (int slot = range.begin(); slot < range.end(); ++slot) {
      // copy of iterator
      auto my_thread_local_it = thread_local_it;  // thread 0
      my_thread_local_it++;
      // get hashmap to merge to
      auto& base_hashmap = thread_local_front.set[slot];  // thread 0 hashmap
      auto& base_nextid = thread_local_front.next_id[slot];
      // Loop over thread local ctx
      for (; my_thread_local_it != thread_local_deduplicate.end(); ++my_thread_local_it) {
        // loop over hashmaps in slot
        auto& thread_local_hashmap = my_thread_local_it->set[slot];
        const auto& thread_id = my_thread_local_it->thread_id;
        auto& thread_local_to_global_map = thread_slot_local_to_slot_local_maps[slot][thread_id];
        // Initiate map for thread/slot temporary variant_id to slot temporary variant ids
        thread_local_to_global_map.resize(my_thread_local_it->next_id[slot]);
        debug_assert(thread_local_to_global_map.size() == static_cast<size_t>(my_thread_local_it->next_id[slot]));
        // Do the merge
        for (const auto& [variant, local_id] : thread_local_hashmap) {
          const auto [variant_base_map_iter, not_in_base_map]{base_hashmap.emplace(variant, base_nextid)};
          if (not_in_base_map) {
            base_nextid++;
          }
          thread_local_to_global_map[local_id] =
              variant_base_map_iter->second;  // ->second == base_nextid before incrementing
        }
      }
    }
  });
}

/**
 * Computed data related to variant sorting based on the variant strings order but using the variant traces.
 */
struct sort_mappers_result {
  const ctl::static_array<row_id> end_map;
  const ctl::static_array<row_id> no_end_map;
  // bad string (contains delimiter)
  const ctl::dynamic_bitset<> tainted;
  const ctl::static_array<int64_t> string_sizes;

  sort_mappers_result(ctl::static_array<row_id> end_map, ctl::static_array<row_id> no_end_map,
                      ctl::dynamic_bitset<> tainted, ctl::static_array<int64_t> string_sizes)
      : end_map(std::move(end_map)),
        no_end_map(std::move(no_end_map)),
        tainted(std::move(tainted)),
        string_sizes(std::move(string_sizes)) {}
};

template <typename SORT_HANDLE>
// Note: clang-tidy complains that sort_agg_group_map and pointers_sa should point to const which is not correct.
// NOLINTNEXTLINE(readability-non-const-parameter)
void assign_sorted_strings(SORT_HANDLE* to_sort, row_id* sort_agg_group_map, cel_string_t* pointers_sa,
                           row_id aggregation_domain_size, const size_t grain_size) {
  tbb::parallel_for(tbb::blocked_range<row_id>{0, aggregation_domain_size, grain_size}, [&](const auto range) {
    for (row_id i{range.begin()}; i < range.end(); ++i) {
      pointers_sa[i] = to_sort[i].aggregated_string;
      debug_assert(to_sort[i].orig_id < aggregation_domain_size);
      sort_agg_group_map[to_sort[i].orig_id] = i;
    }
  });
  for (row_id i{0}; i < aggregation_domain_size; ++i) {
    debug_assert(sort_agg_group_map[i] < aggregation_domain_size);
  }
}

}  // namespace celonis::accelerator::operators::process
