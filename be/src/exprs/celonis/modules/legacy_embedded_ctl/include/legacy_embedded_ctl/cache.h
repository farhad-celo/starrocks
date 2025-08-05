#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <new>
#include <optional>
#include <shared_mutex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/cache_fwd.h"
#include "legacy_embedded_ctl/static_array_fwd.h"
#include "legacy_embedded_ctl/utility.h"

namespace celonis::accelerator::legacy_embedded_ctl {

/**
 * Specialization for shared_ptr, which returns true if the use count is less or equal to 1.
 */
template <typename T>
struct is_cache_entry_unused<std::shared_ptr<T>> {
  [[nodiscard]] bool operator()(const std::shared_ptr<T>& value) const {
    // Less or equal one, since the cache owns an instance of the shared_ptr. So a use count of 1 means only the cache
    // still owns the value.
    return value.use_count() <= 1;
  }
};

/**
 * Specialization for checked_shared_ptr, which returns true if the use count is less or equal to 1.
 */
template <typename T>
struct is_cache_entry_unused<legacy_embedded_ctl::checked_shared_ptr<T>> {
  [[nodiscard]] bool operator()(const legacy_embedded_ctl::checked_shared_ptr<T>& value) const {
    // Less or equal one, since the cache owns an instance of the shared_ptr. So a use count of 1 means only the cache
    // still owns the value.
    return value.use_count() <= 1;
  }
};

/**
 * Specialization for shared_static_array, which returns true if the use count is less or equal to 1.
 */
template <typename T>
struct is_cache_entry_unused<legacy_embedded_ctl::shared_static_array<T>> {
  [[nodiscard]] bool operator()(const legacy_embedded_ctl::shared_static_array<T>& value) const {
    // Less or equal one, since the cache owns an instance of the shared_ptr. So a use count of 1 means only the cache
    // still owns the value.
    return value.use_count() <= 1;
  }
};

/**
 * @brief This is a CTL cache implementation that stores key - value pairs in a cache like container. The implementation
 * evicts based on the retention time and usage.
 *
 * @tparam KEY the type of the cache's keys
 * @tparam VALUE the type of the cache's entries
 * @tparam HASH hash function used on the key
 */
template <typename KEY, typename VALUE, typename HASH>
class cache final {
 public:
  using key_type = KEY;
  using value_type = VALUE;
  using duration_type = std::int64_t;
  using invalid_result = std::bad_alloc;

  /**
   * @param retention_time_ms the retention time for cache entries. When entries exceed this time they qualify for
   * eviction.
   */
  explicit cache(duration_type retention_time_ms);

  /**
   * @brief Tries to retrieve a copy of the value mapped to the specified key from the cache. If no entry was found,
   * computes the value with the passed compute function.
   * @param key key value of the element to search for
   * @param compute Function which is called to compute the cache entry in case it doesn't exist yet.
   * @return Copy of the cache resident value.
   * @exception Possibly throws std::bad_alloc when calling std::async and any valid exception thrown by the computation
   * function.
   */
  template <typename FUNCTION>
  [[nodiscard]] value_type get_or_compute_if_absent(const key_type& key, const FUNCTION& compute);

  /**
   * @brief Retrieves a copy of the value mapped to the specified key if the value resides in the cache.
   * @param key key of the entry to search for
   * @return Copy of the cache resident value if it exists, otherwise an empty optional.
   * @exception Throws valid exceptions thrown by the function.
   */
  [[nodiscard]] std::optional<value_type> get(const key_type& key) const;

  /**
   * @brief Retrieves a copy of the value mapped to the specified key if the value resides in the cache.
   * @param key key of the entry to search for
   * @return Copy of the cache resident value if it exists, otherwise an empty optional.
   * @exception Throws valid exceptions thrown by the function.
   */
  [[nodiscard]] legacy_embedded_ctl::optional_ref<value_type> get_ignore_exception(const key_type& key) const;

  /**
   * Returns the number of elements in cache
   */
  [[nodiscard]] size_t size() const { return container_.size(); }

  /**
   * @brief Removes all entries from the cache whose retention time exceeds the limit and for which the provided
   * is_unused predicate returns true.
   */
  template <typename PREDICATE = is_cache_entry_unused<value_type>>
  void evict_old_and_unused_entries(PREDICATE is_unused = PREDICATE());

  /**
   * @brief Removes all entries from the cache for which the provided is_unused predicate returns true.
   * @brief Removes all entries from the cache for the stored values of which the provided is_unused predicate returns
   * true.
   * @return The number of removed entries.
   */
  template <typename PREDICATE = is_cache_entry_unused<value_type>>
  size_t evict_unused_entries(PREDICATE is_unused = PREDICATE());

  /**
   * @brief Removes all entries from the cache for the keys of which the provided is_unused predicate returns true.
   * @return The number of removed entries.
   */
  template <typename PREDICATE = is_cache_entry_unused<value_type>>
  size_t evict_unused_entries_by_key(PREDICATE is_unused = PREDICATE());

  /**
   * @brief Attempts to reduce the cache to size n by evicting oldest entries if they are unused
   */
  template <typename PREDICATE = is_cache_entry_unused<value_type>>
  void reduce_to_n_most_recent_entries(std::size_t n, PREDICATE is_unused = PREDICATE());

  /**
   * @brief Erases all entries from the cache.
   */
  void clear();

  /**
   * @return The hit rate of the cache aggregated since its creation.
   */
  double compute_hit_rate() const;

 private:
  using time_point_type = std::int64_t;
  struct entry final {
    std::shared_future<value_type> future_value;
    mutable std::atomic<time_point_type> last_usage_ms;

    explicit entry(std::shared_future<value_type> new_future_value)
        : future_value{std::move(new_future_value)}, last_usage_ms{get_time_point_now_ms()} {}
  };

  [[nodiscard]] static time_point_type get_time_point_now_ms();

  void log_cache_hit() const;
  void log_cache_miss() const;

  template <typename FUNC>
  size_t erase_if_predicate(const FUNC& predicate);

  // the retention time for cache entries
  duration_type retention_time_ms_;

  std::unordered_map<key_type, entry, HASH> container_{};

  mutable std::shared_mutex container_mutex_{};

  // statistics for cache hit rate
  mutable std::atomic<uint64_t> cache_hits_{0};
  mutable std::atomic<uint64_t> cache_misses_{0};
};

template <typename KEY, typename VALUE, typename HASH>
cache<KEY, VALUE, HASH>::cache(const duration_type retention_time_ms) : retention_time_ms_{retention_time_ms} {}

template <typename KEY, typename VALUE, typename HASH>
template <typename FUNCTION>
VALUE cache<KEY, VALUE, HASH>::get_or_compute_if_absent(const key_type& key, const FUNCTION& compute) {
  if (auto value_ptr{get(key)}) {  // try to retrieve from cache
    return value_ptr.value();
  }

  // create the future object that will be stored in the cache, can throw std::bad_alloc
  auto future = std::async(std::launch::deferred, [&key, &compute]() { return compute(key); }).share();
  {
    std::unique_lock lock{container_mutex_};
    if (auto [it, success] = container_.try_emplace(key, future); !success) {
      it->second.last_usage_ms = get_time_point_now_ms();
      // cache resident future is copied to prevent redundant computation
      future = it->second.future_value;
    }
  }
  try {
    return future.get();
  } catch (const invalid_result& e) {
    std::unique_lock lck{container_mutex_};
    try {  // in case another thread deleted and inserted before the lock acquire
      return future.get();
    } catch (const invalid_result& /*e*/) {
      container_.erase(key);
      throw;  // rethrow for handling by application code
    }
  }
}

template <typename KEY, typename VALUE, typename HASH>
std::optional<VALUE> cache<KEY, VALUE, HASH>::get(const key_type& key) const {
  std::shared_lock lck{container_mutex_};
  if (const auto search = container_.find(key); search != container_.end()) {
    search->second.last_usage_ms = get_time_point_now_ms();
    log_cache_hit();
    try {
      return search->second.future_value.get();
    } catch (const invalid_result&) {
      return std::nullopt;
    }
  }
  log_cache_miss();
  return std::nullopt;
}

template <typename KEY, typename VALUE, typename HASH>
ctl::optional_ref<VALUE> cache<KEY, VALUE, HASH>::get_ignore_exception(const key_type& key) const {
  std::shared_lock lck{container_mutex_};
  if (const auto search = container_.find(key); search != container_.end()) {
    search->second.last_usage_ms = get_time_point_now_ms();
    log_cache_hit();
    try {
      return &search->second.future_value.get();
    } catch (...) {
      return nullptr;
    }
  }
  log_cache_miss();
  return nullptr;
}

template <typename KEY, typename VALUE, typename HASH>
template <typename PREDICATE>
void cache<KEY, VALUE, HASH>::evict_old_and_unused_entries(PREDICATE is_unused) {
  const auto now = get_time_point_now_ms();

  erase_if_predicate([this, now, &is_unused](const KEY& /*key*/, time_point_type last_usage_ms, const VALUE& value) {
    const auto diff = now - last_usage_ms;
    return diff > retention_time_ms_ && is_unused(value);
  });
}

template <typename KEY, typename VALUE, typename HASH>
template <typename PREDICATE>
size_t cache<KEY, VALUE, HASH>::evict_unused_entries(PREDICATE is_unused) {
  return erase_if_predicate([&is_unused](const KEY& /*key*/, time_point_type /*last_usage*/, const VALUE& value) {
    return is_unused(value);
  });
}

template <typename KEY, typename VALUE, typename HASH>
template <typename PREDICATE>
size_t cache<KEY, VALUE, HASH>::evict_unused_entries_by_key(PREDICATE is_unused) {
  return erase_if_predicate(
      [&is_unused](const KEY& key, time_point_type /*last_usage*/, const VALUE& /*value*/) { return is_unused(key); });
}

template <typename KEY, typename VALUE, typename HASH>
template <typename PREDICATE>
void cache<KEY, VALUE, HASH>::reduce_to_n_most_recent_entries(std::size_t n, PREDICATE is_unused) {
  legacy_embedded_debug_assert(n > 0);
  std::unique_lock lock{container_mutex_};
  if (container_.size() <= n) {
    return;
  }

  auto num_entries_to_evict{container_.size() - n};
  std::vector<std::pair<time_point_type, key_type>> eviction_list;
  eviction_list.reserve(container_.size());

  for (auto& [key, entry] : container_) {
    if (is_unused(entry.future_value.get())) {
      eviction_list.emplace_back(entry.last_usage_ms, key);
    }
  }

  if (num_entries_to_evict < eviction_list.size()) {
    // Identify the oldest elements
    auto oldest_element_to_maintain_it{std::next(eviction_list.begin(), num_entries_to_evict)};
    std::ranges::nth_element(eviction_list, oldest_element_to_maintain_it);
  }

  num_entries_to_evict = std::min(eviction_list.size(), num_entries_to_evict);

  // Evict the found entries
  for (uint64_t i = 0; i < num_entries_to_evict; ++i) {
    container_.erase(eviction_list[i].second);
  }
}

template <typename KEY, typename VALUE, typename HASH>
void cache<KEY, VALUE, HASH>::clear() {
  std::unique_lock lock{container_mutex_};
  container_.clear();
}

template <typename KEY, typename VALUE, typename HASH>
typename cache<KEY, VALUE, HASH>::time_point_type cache<KEY, VALUE, HASH>::get_time_point_now_ms() {
  return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now())
      .time_since_epoch()
      .count();
}

template <typename KEY, typename VALUE, typename HASH>
template <typename FUNC>
size_t cache<KEY, VALUE, HASH>::erase_if_predicate(const FUNC& predicate) {
  std::unique_lock lock{container_mutex_};
  return std::erase_if(container_, [&predicate](const auto& entry) {
    auto value_opt = element_or_null(entry.second.future_value);
    if (!value_opt.has_value()) {
      return true;
    }
    return predicate(entry.first, entry.second.last_usage_ms, value_opt.value().get());
  });
}

template <typename KEY, typename VALUE, typename HASH>
void cache<KEY, VALUE, HASH>::log_cache_hit() const {
  cache_hits_.fetch_add(1, std::memory_order_relaxed);
}

template <typename KEY, typename VALUE, typename HASH>
void cache<KEY, VALUE, HASH>::log_cache_miss() const {
  cache_misses_.fetch_add(1, std::memory_order_relaxed);
}

template <typename KEY, typename VALUE, typename HASH>
double cache<KEY, VALUE, HASH>::compute_hit_rate() const {
  const uint64_t cur_cache_hits{cache_hits_.load(std::memory_order_relaxed)};
  const uint64_t cur_cache_misses{cache_misses_.load(std::memory_order_relaxed)};
  // Note that overflow could occur here, but such high numbers should not occur in production
  const uint64_t total_gets{cur_cache_hits + cur_cache_misses};
  if (total_gets != 0) {
    return static_cast<double>(cur_cache_hits) / static_cast<double>(total_gets);
  }
  return 1.0;
}

}  // namespace celonis::accelerator::legacy_embedded_ctl
