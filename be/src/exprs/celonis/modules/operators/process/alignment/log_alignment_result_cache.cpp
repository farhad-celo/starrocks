#include "log_alignment_result_cache.h"

#include <chrono>
#include <mutex>

#include "concurrency/concurrency_utils.h"
#include "legacy_embedded_ctl/source_location.h"
#include "modules/operators/process/alignment/log_alignment_result.h"

namespace celonis::accelerator::operators::process::alignment {

log_alignment_result_t log_alignment_result_cache::get_cached_or_compute_log_alignment(
    const std::string& alignment_table_cache_key,
    const std::function<log_alignment_result_t()>& compute_log_alignment) {
  // Check for existing log_alignment_result
  if (auto log_alignment{get_log_alignment(alignment_table_cache_key)}; log_alignment != nullptr) {
    return log_alignment;
  }

  // Create new log_alignment_result and add it to cache
  return add_log_alignment(alignment_table_cache_key, compute_log_alignment);
}

log_alignment_result_t log_alignment_result_cache::get_log_alignment(const std::string& alignment_table_cache_key) {
  std::shared_lock get_lock(pool_mutex_);
  const auto found{log_alignment_pool_.find(alignment_table_cache_key)};
  if (found == log_alignment_pool_.end()) {
    return {};
  }
  return found->second;
}

log_alignment_result_t log_alignment_result_cache::add_log_alignment(
    const std::string& alignment_table_cache_key,
    const std::function<log_alignment_result_t()>& compute_log_alignment) {
  const auto insert_lock{concurrency::lock_validated(pool_mutex_, std::chrono::seconds{60})};

  auto& stored_alignment_result{log_alignment_pool_[alignment_table_cache_key]};
  if (!stored_alignment_result) {
    stored_alignment_result = compute_log_alignment();
  }
  return stored_alignment_result;
}

}  // namespace celonis::accelerator::operators::process::alignment
