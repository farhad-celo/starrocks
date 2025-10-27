#include "memory_manager.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <mutex>

#include "concurrency/concurrency_utils.h"
#include "concurrency/shared_counting_mutex.h"
#include "legacy_embedded_ctl/memory/meminfo.h"
#include "legacy_embedded_ctl/memory/memory_consumption_tracker.h"
#include "log/log.h"
#include "modules/common/call_and_log_unsafe_callable.h"
#include "modules/common/execution_context.h"
#ifndef CELOSTAR
#include "modules/cube/query_transaction.h"
#endif
#include "modules/memory/allocator_utils.h"
#include "modules/memory/management/data_handler.h"

namespace celonis::accelerator::memory::management {

static constexpr std::chrono::seconds WARNING_UPPER_BOUND{10};

memory_manager::memory_manager(std::shared_ptr<cube::execution::tracking::operator_statistics> op_statistics)
    : meminfo_fetcher_([]() { return legacy_embedded_ctl::fetch_current_full_meminfo(); }),
      op_statistics_(std::move(op_statistics)) {}

void memory_manager::set_meminfo_fetcher(std::function<legacy_embedded_ctl::full_meminfo()> meminfo_fetcher) {
  meminfo_fetcher_ = std::move(meminfo_fetcher);
}

// This function is already protected with a lock by the caller function, i.e. evict_if_needed()
std::vector<std::pair<data_handler_t, mem_time_t>> memory_manager::unsafe_get_groups_with_time() {
  std::vector<std::pair<data_handler_t, mem_time_t>> data_handlers_with_last_usage_time;

  std::for_each(volatile_groups_.cbegin(), volatile_groups_.cend(),
                [&data_handlers_with_last_usage_time](const managed_group_t& group) {
                  group->apply([&data_handlers_with_last_usage_time](const data_handler_t& handler) {
                    auto status{handler->get_load_status()};
                    if (status != load_status::SWAPPED && handler->is_swappable()) {
                      data_handlers_with_last_usage_time.emplace_back(handler, handler->get_last_usage());
                    }
                  });
                });

  std::for_each(persistent_groups_.cbegin(), persistent_groups_.cend(),
                [&data_handlers_with_last_usage_time](const managed_group_t& group) {
                  group->apply([&data_handlers_with_last_usage_time](const data_handler_t& handler) {
                    auto status{handler->get_load_status()};
                    if (status != load_status::SWAPPED && handler->is_swappable()) {
                      data_handlers_with_last_usage_time.emplace_back(handler, handler->get_last_usage());
                    }
                  });
                });

  return data_handlers_with_last_usage_time;
}

std::pair<std::vector<volatile_group_t>, std::vector<managed_group_t>> memory_manager::get_groups(
    std::chrono::seconds wait_time, legacy_embedded_ctl::source_location source_location) const {
  const auto set_lock{concurrency::lock_shared_validated(data_mutex_, wait_time, source_location)};
  common::timer timer;

  std::vector<volatile_group_t> volatile_groups_copy{};
  volatile_groups_copy.reserve(volatile_groups_.size());
  for (const volatile_group_t& group : volatile_groups_) {
    volatile_groups_copy.emplace_back(group);
  }
  std::vector<managed_group_t> managed_group_copy{};
  managed_group_copy.reserve(persistent_groups_.size());
  for (const managed_group_t& group : persistent_groups_) {
    managed_group_copy.emplace_back(group);
  }

  timer.stop();
  const auto seconds{std::chrono::duration_cast<std::chrono::seconds>(timer.duration())};
  if (seconds >= WARNING_UPPER_BOUND) {
    log::jwarn(
        fmt::format("Memory Manager: get_groups takes {}s.", seconds.count()),
        {{"volatile_groups_size", volatile_groups_.size()}, {"persistent_groups_size", persistent_groups_.size()}});
  }

  return std::make_pair(std::move(volatile_groups_copy), std::move(managed_group_copy));
}

bool memory_manager::evict_cache_if_needed(const memory_threshold& threshold, common::execution_context& context) {
  const auto cache_eviction_is_needed = [&threshold](const std::optional<double>& usage) noexcept {
    return usage.has_value() && usage > threshold.higher();
  };
  auto usage = get_memory_status().in_use_global_percentage();
  if (!cache_eviction_is_needed(usage)) {
    return false;
  }

  std::vector<std::pair<std::shared_ptr<data_handler>, mem_time_t>> sorted_data_handlers{};
  {
    std::shared_lock<std::shared_timed_mutex> set_lock(data_mutex_, std::chrono::seconds(1));

    if (!set_lock.owns_lock()) {
      log::warn("Eviction attempt was not successful. Could not acquire lock!");
      return false;
    }

    common::timer timer;
    sorted_data_handlers = unsafe_get_groups_with_time();

    timer.stop();
    const auto seconds{std::chrono::duration_cast<std::chrono::seconds>(timer.duration())};
    if (seconds >= WARNING_UPPER_BOUND) {
      log::jwarn(fmt::format("Memory Manager: unsafe_get_groups_with_time takes {}s.", seconds.count()),
                 {{"data_handler_count", sorted_data_handlers.size()}});
    }
  }
  auto partition_point =
      std::partition(std::begin(sorted_data_handlers), std::end(sorted_data_handlers),
                     [](const auto& handle_time_pair) { return handle_time_pair.first->is_persisted(); });
  std::sort(std::begin(sorted_data_handlers), partition_point,
            [](const auto& p1, const auto& p2) { return p1.second < p2.second; });
  std::sort(partition_point, std::end(sorted_data_handlers),
            [](const auto& p1, const auto& p2) { return p1.second < p2.second; });

  const auto old_memory_status = get_memory_status();
  usage = old_memory_status.in_use_global_percentage();
  if (!cache_eviction_is_needed(usage)) {
    log::info("Eviction is not necessary anymore!");
    return false;
  }
  size_t not_swapped_bytes{0};
  auto curr_memory_status = old_memory_status;
#ifndef CELOSTAR
  for (auto& handler_time_pair : sorted_data_handlers) {
    bool swapped_out{handler_time_pair.first->swap_out(context)};
    if (!swapped_out) {
      not_swapped_bytes += handler_time_pair.first->get_size_in_memory();
    }
    curr_memory_status = get_memory_status();
    usage = curr_memory_status.in_use_global_percentage();

    if (!usage.has_value()) {
      log::jwarn("Failed to retrieve usage", legacy_embedded_ctl::meminfo_change_json(
                                                 old_memory_status, curr_memory_status, std::nullopt, "Eviction"));
      return true;  // leave function as we can not get current memory usage
    }

    if (usage.value() <= threshold.lower()) {
      log::jinfo("End memory eviction early", legacy_embedded_ctl::meminfo_change_json(
                                                  old_memory_status, curr_memory_status, std::nullopt, "Eviction"));
      return true;  // leave function as enough data was evicted
    }
  }
#endif

  log::jinfo("High Memory", legacy_embedded_ctl::meminfo_change_json(old_memory_status, curr_memory_status,
                                                                     not_swapped_bytes, "Eviction"));
  return true;
}

memory_info memory_manager::get_data_status() const {
  const auto group_vectors{get_groups(std::chrono::seconds{60})};
  memory_info info;
  for (const volatile_group_t& group : group_vectors.first) {
    info.volatile_groups.push_back(group->dump_header());
    info.size_on_disk += group->get_size_on_disk();
    info.size_in_memory += group->get_size_in_memory();
  }
  for (const managed_group_t& group : group_vectors.second) {
    info.persistent_groups.push_back(group->dump_header());
    info.size_on_disk += group->get_size_on_disk();
    info.size_in_memory += group->get_size_in_memory();
  }
  return info;
}

legacy_embedded_ctl::full_meminfo memory_manager::get_memory_status() const { return meminfo_fetcher_(); }

void memory_manager::force_swap_in(const common::execution_context& context) const {
  const auto group_vectors{get_groups(std::chrono::seconds{60})};
  for (const volatile_group_t& group : group_vectors.first) {
    group->force_swap_in(context);
  }
  for (const managed_group_t& group : group_vectors.second) {
    group->force_swap_in(context);
  }
}

#ifndef CELOSTAR
void memory_manager::force_swap_out(common::execution_context& context) const {
  auto group_vectors{get_groups(std::chrono::seconds{60})};
  const auto old_memory_status = get_memory_status();
  auto first_partition_point = std::partition(std::begin(group_vectors.first), std::end(group_vectors.first),
                                              [](const auto& group) { return group->is_persisted(); });
  auto second_partition_point = std::partition(std::begin(group_vectors.second), std::end(group_vectors.second),
                                               [](const auto& group) { return group->is_persisted(); });
  for (auto it = std::begin(group_vectors.first); it != first_partition_point; ++it) {
    (*it)->force_swap_out(context);
  }
  for (auto it = std::begin(group_vectors.second); it != second_partition_point; ++it) {
    (*it)->force_swap_out(context);
  }
  for (auto it = first_partition_point; it != std::end(group_vectors.first); ++it) {
    (*it)->force_swap_out(context);
  }
  for (auto it = second_partition_point; it != std::end(group_vectors.second); ++it) {
    (*it)->force_swap_out(context);
  }
  const auto new_memory_status = get_memory_status();
  log::jinfo("Forced Swap-Out", legacy_embedded_ctl::meminfo_change_json(old_memory_status, new_memory_status,
                                                                         std::nullopt, "Forced Swap-Out"));

  release_unused_buffers();
}

void memory_manager::force_compress() const {
  const auto group_vectors{get_groups(std::chrono::seconds{60})};
  const auto old_memory_status = get_memory_status();
  for (const volatile_group_t& group : group_vectors.first) {
    group->force_compress();
  }
  for (const managed_group_t& group : group_vectors.second) {
    group->force_compress();
  }
  const auto new_memory_status = get_memory_status();
  log::jinfo("Forced Compression", legacy_embedded_ctl::meminfo_change_json(old_memory_status, new_memory_status,
                                                                            std::nullopt, "Forced Compression"));
}
#endif

void memory_manager::force_clean_up() {
  const auto set_lock{concurrency::lock_validated(data_mutex_, std::chrono::seconds{60})};
  common::timer timer;

  const size_t volatile_groups_size{volatile_groups_.size()};
  for (const volatile_group_t& group : volatile_groups_) {
    group->erase();
  }
  volatile_groups_.clear();

  timer.stop();
  const auto seconds{std::chrono::duration_cast<std::chrono::seconds>(timer.duration())};
  if (seconds >= WARNING_UPPER_BOUND) {
    log::jwarn(fmt::format("Memory Manager: force_clean_up takes {}s.", seconds.count()),
               {{"erased_volatile_groups_size", volatile_groups_size}});
  }
}

struct collect_garbage_mem_groups_result {
  // whether anything *was* swapped out
  bool anything_swapped_out{};
  // whether everything *is* swapped out
  bool everything_is_swapped_out{};
  // whether we stopped collecting earlier due to another query
  bool stopped_collecting{};
  // memory handlers that are not swapped out but were expected to be swapped out
  std::vector<std::string> non_swapped_out_mem_handlers;
};

#ifdef CELOSTAR
bool memory_manager::collect_garbage(concurrency::shared_counting_mutex& cube_mutex,
                                     std::optional<std::chrono::steady_clock::time_point> last_query_finished,
                                     common::execution_context& context) {
  return true;
}
#else
template <class SETTYPE>
collect_garbage_mem_groups_result collect_garbage_handle_groups(
    concurrency::shared_counting_mutex& cube_mutex, common::execution_context& context, const SETTYPE& groups,
    const mem_time_t now, const int64_t eviction_threshold_in_seconds,
    const int64_t seconds_between_allowed_compression,
    const std::optional<std::chrono::steady_clock::time_point> last_query_finished) {
  bool anything_swapped_out{false};
  bool everything_is_swapped_out{true};
  bool collected_garbage = false;
  std::vector<std::string> non_swapped_dhs;
  const bool everything_should_be_swapped_out{last_query_finished.has_value() &&
                                              std::chrono::seconds{eviction_threshold_in_seconds} <
                                                  (mem_clock_t::now() - last_query_finished.value())};
  for (const auto& group : groups) {
    group->apply_and_remove_dangling_weak([eviction_threshold_in_seconds, seconds_between_allowed_compression, &now,
                                           &context, &anything_swapped_out, &collected_garbage, &non_swapped_dhs,
                                           &everything_should_be_swapped_out, &everything_is_swapped_out,
                                           type = group->get_type()](data_handler_t& dh) {
      if (!dh->is_swappable()) {
        return;
      }
      const auto load_status = dh->get_load_status();
      if (load_status != load_status::SWAPPED) {
        const auto unused_time_in_seconds{
            std::chrono::duration_cast<std::chrono::seconds>(now - dh->get_last_usage().get()).count()};
        if (unused_time_in_seconds >= eviction_threshold_in_seconds) {
          const bool cur_swapped_out = dh->swap_out(context);
          everything_is_swapped_out &= cur_swapped_out;
          anything_swapped_out |= cur_swapped_out;
          collected_garbage |= cur_swapped_out;
          if (everything_should_be_swapped_out && !cur_swapped_out) {
            if (dh->description().empty()) {
              non_swapped_dhs.push_back(fmt::format("type: {}", type));
            } else {
              non_swapped_dhs.emplace_back(dh->description());
            }
          }
        } else if (unused_time_in_seconds >= seconds_between_allowed_compression) {
          collected_garbage |= dh->compress();
          everything_is_swapped_out = false;
        } else {
          everything_is_swapped_out = false;
        }
      }
    });
    // Make sure that garbage collection has a chance to "progress": In a scenario with high load, we should still
    // compress / swap out one group per garbage collection run before we stop. Otherwise, we could end up in a scenario
    // where memory consumption is constantly growing, until we need to forcefully swap out data, which could delay
    // query execution even further.
    if (collected_garbage && cube_mutex.other_threads_waiting()) {
      log::info("Stopping garbage collection as queries are waiting for execution.");
      return {anything_swapped_out, everything_is_swapped_out, true, std::move(non_swapped_dhs)};
    }
  }
  return {anything_swapped_out, everything_is_swapped_out, false, std::move(non_swapped_dhs)};
}

bool memory_manager::collect_garbage(concurrency::shared_counting_mutex& cube_mutex,
                                     std::optional<std::chrono::steady_clock::time_point> last_query_finished,
                                     common::execution_context& context) {
  auto now{mem_clock_t::now()};

  // retention time is configurable
  int64_t seconds_between_allowed_cache = cache_retention_time_in_min_ * 60;
  if (seconds_between_allowed_cache <= 0) {
    seconds_between_allowed_cache = 0;
  }

  // cache compression time is configurable
  int64_t seconds_between_allowed_compression = cache_compression_time_in_min_ * 60;
  if (seconds_between_allowed_compression <= 0) {
    // use half of the cache retention time as default
    seconds_between_allowed_compression = seconds_between_allowed_cache / 2;
  }

  const auto group_vectors{get_groups(std::chrono::seconds{60})};

  // persistent_data
  auto persistent_result{collect_garbage_handle_groups(cube_mutex, context, group_vectors.second, now,
                                                       seconds_between_allowed_cache,
                                                       seconds_between_allowed_compression, last_query_finished)};

  collect_garbage_mem_groups_result volatile_result;
  if (!persistent_result.stopped_collecting) {
    volatile_result =
        collect_garbage_handle_groups(cube_mutex, context, group_vectors.first, now, seconds_between_allowed_cache,
                                      seconds_between_allowed_compression, last_query_finished);
  }

  report_non_swapped_dhs(persistent_result, volatile_result);

  if (persistent_result.anything_swapped_out || volatile_result.anything_swapped_out) {
    release_unused_buffers();
  }

  report_jemalloc_and_tracker_stats(persistent_result, volatile_result);

  return !(persistent_result.stopped_collecting || volatile_result.stopped_collecting);
}
#endif

void memory_manager::report_non_swapped_dhs(const collect_garbage_mem_groups_result& persistent_result,
                                            const collect_garbage_mem_groups_result& volatile_result) {
  bool stop_collecting{persistent_result.stopped_collecting || volatile_result.stopped_collecting};
  // non_swapped_out_mem_handlers are only filled if all data handlers should have been swapped out due to the time
  // between the last query executed and now being larger than the swap out time
  if (!stop_collecting && !reported_non_swapped_dhs_ &&
      (!persistent_result.non_swapped_out_mem_handlers.empty() ||
       !volatile_result.non_swapped_out_mem_handlers.empty())) {
    legacy_embedded_format::json::json_array_t data_handle_descriptions;
    // Since finding such dhs should be the exception, we are fine with copying the strings here
    for (const auto& description : persistent_result.non_swapped_out_mem_handlers) {
      data_handle_descriptions.emplace_back(description);
    }
    for (const auto& description : volatile_result.non_swapped_out_mem_handlers) {
      data_handle_descriptions.emplace_back(description);
    }
    log::jerror("Non-swapped out data handlers", {{"data_handle_descriptions", data_handle_descriptions}});
    reported_non_swapped_dhs_ = true;
  }
}

void memory_manager::report_jemalloc_and_tracker_stats(const collect_garbage_mem_groups_result& persistent_result,
                                                       const collect_garbage_mem_groups_result& volatile_result) {
  // if we just swapped out everything, we report the memory consumption according to jemalloc
  if (!reported_jemalloc_after_full_swap_out && volatile_result.everything_is_swapped_out &&
      persistent_result.everything_is_swapped_out) {
    auto jemalloc_stats{get_allocator_stats()};
    if (!jemalloc_stats.has_value()) {
      return;
    }
    auto tracker_used{
        legacy_embedded_ctl::global_memory_consumption_tracker::get_consumption_tracker().cur_net_allocated()};
    log::jinfo("Reporting jemalloc and tracker stats", {{"allocated", jemalloc_stats->allocated},
                                                        {"active", jemalloc_stats->active},
                                                        {"metadata_size", jemalloc_stats->metadata_size},
                                                        {"resident", jemalloc_stats->resident},
                                                        {"mapped", jemalloc_stats->mapped},
                                                        {"retained", jemalloc_stats->retained},
                                                        {"tracker_used", tracker_used}});
    reported_jemalloc_after_full_swap_out = true;
  } else if (volatile_result.anything_swapped_out || persistent_result.anything_swapped_out) {
    reported_jemalloc_after_full_swap_out = false;
  }
}

void memory_manager::register_persistent_group(const std::shared_ptr<managed_memory_group>& group) {
  const auto set_lock{concurrency::lock_validated(data_mutex_, std::chrono::seconds{60})};
  persistent_groups_.insert(group);
}

void memory_manager::register_volatile_group(const std::shared_ptr<volatile_managed_memory_group>& group) {
  const auto set_lock{concurrency::lock_validated(data_mutex_, std::chrono::seconds{60})};
  volatile_groups_.insert(group);
}

void memory_manager::deregister_all() {
  const auto set_lock{concurrency::lock_validated(data_mutex_, std::chrono::seconds{60})};

  common::timer timer;
  const size_t volatile_groups_size{volatile_groups_.size()};
  const size_t persistent_groups_size{persistent_groups_.size()};

  for (const auto& group : volatile_groups_) {
    common::call_and_log_unsafe_callable([&group]() { group->erase(); },
                                         "Memory Manager: erase called by deregister_all failed");
  }

  for (const auto& group : persistent_groups_) {
    common::call_and_log_unsafe_callable([&group]() { group->clear_group(); },
                                         "Memory Manager: clear_group called by deregister_all failed");
  }

  timer.stop();
  const auto seconds{std::chrono::duration_cast<std::chrono::seconds>(timer.duration())};
  if (seconds >= WARNING_UPPER_BOUND) {
    log::jwarn(fmt::format("Memory Manager: deregister_all takes {}s.", seconds.count()),
               {{"deregistered_volatile_groups_size", volatile_groups_size},
                {"deregistered_persistent_groups_size", persistent_groups_size}});
  }
}

void memory_manager::erase_persistent(const managed_group_t& managed_group) {
  const auto set_lock{concurrency::lock_validated(data_mutex_, std::chrono::seconds{60})};
  common::timer timer;

  managed_group->clear_group();
  auto removed_count = persistent_groups_.erase(managed_group);
  if (removed_count == 0) {
    log::error("Tried to remove a non-registered persistent managed_group: {}",
               managed_group->dump_header().description);
  }

  timer.stop();
  const auto seconds{std::chrono::duration_cast<std::chrono::seconds>(timer.duration())};
  if (seconds >= WARNING_UPPER_BOUND) {
    log::jwarn(fmt::format("Memory Manager: erase_persistent takes {}s.", seconds.count()));
  }
}

void memory_manager::erase_volatile(const volatile_group_t& volatile_group, bool log_failure) {
  const auto lck{concurrency::lock_validated(data_mutex_, std::chrono::seconds{60})};
  common::timer timer;

  volatile_group->clear_group();
  const auto removed_count{volatile_groups_.erase(volatile_group)};
  if (log_failure && removed_count == 0) {
    log::error("Tried to remove a non-registered volatile managed group: {}",
               volatile_group->dump_header().description);
  }

  timer.stop();
  const auto seconds{std::chrono::duration_cast<std::chrono::seconds>(timer.duration())};
  if (seconds >= WARNING_UPPER_BOUND) {
    log::jwarn(fmt::format("Memory Manager: erase_volatile takes {}s.", seconds.count()));
  }
}

#ifndef CELOSTAR
void memory_manager::end_transaction(const cube::query_transaction& transaction) const {
  const auto group_vectors{get_groups(std::chrono::seconds{60})};

  for (const volatile_group_t& group : group_vectors.first) {
    try {
      group->swap_out(transaction.transaction_id, transaction.transaction_start, transaction.query_context);
    } catch (const std::exception& e) {
      log::error("Memory Manager: swap_out called by end_transaction failed due to '{}'.", e.what());
    } catch (...) {
      log::error("Memory Manager: swap_out called by end_transaction failed.");
    }
  }
  for (const managed_group_t& group : group_vectors.second) {
    try {
      group->swap_out(transaction.transaction_id, transaction.transaction_start, transaction.query_context);
    } catch (const std::exception& e) {
      log::error("Memory Manager: swap_out called by end_transaction failed due to '{}'.", e.what());
    } catch (...) {
      log::error("Memory Manager: swap_out called by end_transaction failed.");
    }
  }
}
#endif

void memory_manager::add_invocation_to_operator_statistics(const std::string& key,
                                                           std::chrono::milliseconds runtime) const {
  if (op_statistics_ != nullptr) {
    op_statistics_->add_invocation(key, runtime, PEAK_MEMORY_PLACEHOLDER);
  }
}

}  // namespace celonis::accelerator::memory::management
