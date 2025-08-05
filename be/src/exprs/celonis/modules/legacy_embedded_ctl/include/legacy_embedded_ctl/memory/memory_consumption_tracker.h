#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * @brief Helper class to track the "large data structure" allocations. To be used as singleton, but publicly
 * constructible for testing.
 */
class global_memory_consumption_tracker final {
 public:
  global_memory_consumption_tracker(const global_memory_consumption_tracker&) = delete;
  global_memory_consumption_tracker& operator=(const global_memory_consumption_tracker&) = delete;
  global_memory_consumption_tracker(global_memory_consumption_tracker&&) = delete;
  global_memory_consumption_tracker& operator=(global_memory_consumption_tracker&&) = delete;
  /**
   * @brief Meant for testing only: return number of currently active queries
   */
  [[nodiscard]] std::size_t active_query_count() const noexcept;
  /**
   * @brief Return the (estimated) "static" size of the process (i.e., size of memory that is not tracked here) at the
   * when last, no query was active
   */
  [[nodiscard]] std::size_t static_process_size() const noexcept;
  /**
   * @brief Return the size of the tracked allocations
   */
  [[nodiscard]] std::size_t cur_net_allocated() const noexcept;
  /**
   * @brief Return the size of the currently allocated metatada
   */
  [[nodiscard]] std::size_t cur_metadata_allocated() const noexcept;
  /**
   * @brief Register an allocation
   */
  void register_allocation(std::size_t size, bool as_metadata = false) noexcept;
  /**
   * @brief Deregister an allocation
   */
  void deregister_allocation(std::size_t size, bool as_metadata = false) noexcept;
  /**
   * @brief Register a batched tracker
   */
  void register_batched_tracker() noexcept;
  /**
   * @brief Deregister a batched tracker
   */
  void deregister_batched_tracker() noexcept;
  /**
   * @brief Get the number of registered trackers using batching
   */
  size_t batched_tracker_count() noexcept;
  /**
   * @brief Return singleton consumption tracker
   */
  static global_memory_consumption_tracker& get_consumption_tracker() noexcept;

 private:
  /**
   * @brief Register a query (i.e., a query scope was created).
   */
  void register_query() noexcept;
  /**
   * @brief Deregister a query (i.e., a query scope is about to be destroyed)
   */
  void deregister_query() noexcept;

  global_memory_consumption_tracker() noexcept = default;
  std::mutex update_lock;
  std::atomic<std::size_t> net_allocated_{0};
  std::atomic<std::size_t> metadata_allocated_{0};
  std::atomic<std::size_t> static_process_size_{0};
  std::atomic<std::size_t> active_query_count_{0};
  std::atomic<std::size_t> batched_tracker_count_{0};

  friend class scoped_query_registerer;
};

/**
 * @brief Class to call register a query, to be called from query_scope
 */
class scoped_query_registerer {
 public:
  scoped_query_registerer();
  ~scoped_query_registerer();
};

}  // namespace celonis::accelerator::legacy_embedded_ctl
