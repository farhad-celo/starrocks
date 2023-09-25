#pragma once

#include <functional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "modules/operators/process/alignment/log_alignment_result_cache_fwd.h"
#include "modules/operators/process/alignment/log_alignment_result_fwd.h"

namespace celonis::accelerator::operators::process::alignment {

/**
 * Just a synchronized map in order to short time cache log_alignment_results (similar to edgifier_cache).
 */
class log_alignment_result_cache {
 public:
  /**
   * Retrieves log_alignment_result from cache.
   * If log_alignment_result doesn't exist in cache, a new log_alignment_result is created and added to cache.
   *
   * @param alignment_table_cache_key identify the log_alignment_result for that table
   * @param compute_log_alignment function for creating new log_alignment_result
   * @return log_alignment_result stored under cache key
   */
  [[nodiscard]] log_alignment_result_t get_cached_or_compute_log_alignment(
      const std::string& alignment_table_cache_key,
      const std::function<log_alignment_result_t()>& compute_log_alignment);

 private:
  [[nodiscard]] log_alignment_result_t get_log_alignment(const std::string& alignment_table_cache_key);
  [[nodiscard]] log_alignment_result_t add_log_alignment(
      const std::string& alignment_table_cache_key,
      const std::function<log_alignment_result_t()>& compute_log_alignment);

  std::unordered_map<std::string, log_alignment_result_t> log_alignment_pool_{};
  std::shared_timed_mutex pool_mutex_{};
};

}  // namespace celonis::accelerator::operators::process::alignment
