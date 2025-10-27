#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

#include "concurrency/shared_counting_mutex.h"
#include "legacy_embedded_ctl/memory/meminfo_fwd.h"
#include "legacy_embedded_ctl/source_location.h"
#include "modules/common/execution_context_fwd.h"
#include "modules/cube/execution/tracking/operator_statistics_fwd.h"
#include "modules/memory/management/data_handler_fwd.h"
#include "modules/memory/management/managed_memory_group.h"
#include "modules/memory/management/memory_threshold.h"

namespace celonis::accelerator::cube {
struct query_transaction;
}

namespace celonis::accelerator::memory::management {

struct memory_info {
  size_t size_in_memory{0};
  size_t size_on_disk{0};
  std::vector<memory_group_info> persistent_groups{};
  std::vector<memory_group_info> volatile_groups{};
};

struct collect_garbage_mem_groups_result;

/**
 * Central place for managing groups of data handles. This class decides when to compress / swap to disk or to
 * completely delete data.
 */
class memory_manager {
 public:
  explicit memory_manager(std::shared_ptr<cube::execution::tracking::operator_statistics> op_statistics);

  /**
   * If the current global memory use is bigger than the higher threshold, evict_cache_if_needed will try to swap
   * out data handles until the global memory usage is below the lower threshold. The data handles which already
   * have an existing swap file are swapped out first, in LRU order. If the usage is still to high, the handles without
   * an existing swap file are swapped out (also in LRU order).
   *
   * @return true, if cache eviction was necessary; false, otherwise.
   */
  bool evict_cache_if_needed(const memory_threshold& threshold, common::execution_context& context);

  void register_persistent_group(const managed_group_t& group);

  void register_volatile_group(const volatile_group_t& group);

  void deregister_all();

  void erase_persistent(const managed_group_t& managed_group);

  void erase_volatile(const volatile_group_t& volatile_group, bool log_failure = true);

  // The following four functions are special functions that can be triggered, if a corresponding protobuf message
  // is sent to the process. They apply the corresponding operation to all contained data handles.
  void force_swap_in(const common::execution_context& context) const;

#ifndef CELOSTAR
  void force_swap_out(common::execution_context& context) const;

  void force_compress() const;
#endif

  void force_clean_up();

  /**
   * Returns information about each stored group and the total main memory and physical storage consumed by all of the
   * stored groups
   */
  memory_info get_data_status() const;

  legacy_embedded_ctl::full_meminfo get_memory_status() const;

  /**
   * Compresses / swaps out data handlers that have not been used for the time limits specified in the corresponding
   * member variables
   *
   * returns false if it stopped due to a query
   */
  bool collect_garbage(concurrency::shared_counting_mutex& cube_mutex,
                       std::optional<std::chrono::steady_clock::time_point> last_query_finished,
                       common::execution_context& context);

  void set_cache_retention_time(int64_t t) { cache_retention_time_in_min_ = t; }

  void set_cache_compression_time(int64_t t) { cache_compression_time_in_min_ = t; }

#ifndef CELOSTAR
  /**
   * Tries to swap out all groups that are associated with the given transaction.
   */
  void end_transaction(const cube::query_transaction& transaction) const;
#endif

  /**
   * Used in testing scenarios to override the real memory status with a mocked one to be able to more easily test
   * high memory usage situations
   */
  void set_meminfo_fetcher(std::function<legacy_embedded_ctl::full_meminfo()> meminfo_fetcher);

  memory_threshold& get_cache_eviction_threshold() noexcept { return cache_eviction_threshold_; }
  const memory_threshold& get_cache_eviction_threshold() const noexcept { return cache_eviction_threshold_; }
  memory_threshold& get_cache_eviction_idle() noexcept { return cache_eviction_idle_; }
  const memory_threshold& get_cache_eviction_idle() const noexcept { return cache_eviction_idle_; }
  bool get_reported_non_swapped_dhs() const noexcept { return reported_non_swapped_dhs_; }

  /**
   * Returns vectors that contain all stored memory groups. The first vector contains the volatile, the second the
   * persistent groups.
   * @param wait_time The time to wait on the memory manger lock until an exception is thrown
   * @param source_location The source location where this function is called (only used in the exception thrown in the
   * case where the lock could not be acquired in time)
   */
  std::pair<std::vector<volatile_group_t>, std::vector<managed_group_t>> get_groups(
      std::chrono::seconds wait_time,
      legacy_embedded_ctl::source_location source_location = legacy_embedded_ctl::source_location{}) const;

  void add_invocation_to_operator_statistics(const std::string& key, std::chrono::milliseconds runtime) const;

 private:
  void report_jemalloc_and_tracker_stats(const collect_garbage_mem_groups_result& persistent_result,
                                         const collect_garbage_mem_groups_result& volatile_result);

  void report_non_swapped_dhs(const collect_garbage_mem_groups_result& persistent_result,
                              const collect_garbage_mem_groups_result& volatile_result);
  /**
   * Returns all contained memory groups together with their last usage time.
   * This function is not thread safe, so the caller needs to ensure that the memory manager lock is held before calling
   * this function.
   */
  std::vector<std::pair<data_handler_t, std::chrono::steady_clock::time_point>> unsafe_get_groups_with_time();

  /**
   * There currently isn't really a difference between the handling of the volatile and persistent groups.
   */
  std::unordered_set<volatile_group_t> volatile_groups_{};
  std::unordered_set<managed_group_t> persistent_groups_{};

  std::function<legacy_embedded_ctl::full_meminfo()> meminfo_fetcher_;
  std::atomic<int64_t> cache_retention_time_in_min_{60};
  std::atomic<int64_t> cache_compression_time_in_min_{cache_retention_time_in_min_ / 2};
  memory_threshold cache_eviction_threshold_{0.5, 0.82};
  memory_threshold cache_eviction_idle_{0.0, 0.7};

  mutable std::shared_timed_mutex data_mutex_;

  std::shared_ptr<cube::execution::tracking::operator_statistics> op_statistics_{nullptr};
  bool reported_non_swapped_dhs_{false};

  bool reported_jemalloc_after_full_swap_out{false};

  // TODO(m.lalic) Remove when memory tracking gets implemented for COLUMN_DICTIFY and SWAP_INs/SWAP_OUTs
  static constexpr int64_t PEAK_MEMORY_PLACEHOLDER{0};
};
}  // namespace celonis::accelerator::memory::management
