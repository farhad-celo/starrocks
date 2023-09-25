#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "modules/cube/execution/tracking/operator_statistics_fwd.h"

namespace celonis::accelerator {
class Response_QueryStatistics;
}  // namespace celonis::accelerator

namespace celonis::accelerator::cube::execution::tracking {

struct drill_down_stats {
  // Total invocations/runtime, which might be cached or not cached, no further information is available
  int64_t total_invocations{};
  std::chrono::milliseconds total_runtime{};
};

using drilled_stats_map_t = std::unordered_map<std::string /* operation_stage */, drill_down_stats>;

using operator_telemetry_map_t = std::unordered_map<std::string /* counter name */, size_t /* counter */>;

struct operator_runtime_stats {
  // Total invocations/runtime, which might be cached or not cached, no further information is available
  int64_t total_invocations{};
  std::chrono::milliseconds total_runtime{};
  size_t peak_memory_sum{};

  // Total cached/non-cached invocations/runtime, only available for operators using the cached_operator interface
  int64_t cached_invocations{};
  std::chrono::milliseconds cached_runtime{};
  int64_t non_cached_invocations{};
  std::chrono::milliseconds non_cached_runtime{};

  drilled_stats_map_t drilled_down_statistics{};
  operator_telemetry_map_t operator_telemetry{};
};

using stats_map_t = std::unordered_map<std::string /* operator_key */, operator_runtime_stats>;

class operator_statistics {
 public:
  void add_invocation(const std::string& operator_key, std::chrono::milliseconds time, size_t peak_memory);
  void add_cached_operator_invocation(const std::string& operator_key, bool is_cached, std::chrono::milliseconds time);

  void add_sub_stage_invocation(const std::string& operator_key, const std::string& stage_name,
                                std::chrono::milliseconds time);
  void add_telemetry_counter(const std::string& operator_key, const std::string& counter_name, size_t count);

  void log_statistics_and_reset();

  // Warning: This function is for testing purposes only since it is not thread-safe.
  [[nodiscard]] const stats_map_t& unsafe_get_stats_map() const noexcept { return stats_map_; }

 private:
  stats_map_t stats_map_{};
  mutable std::mutex mutex_{};
};

using memory_stats_map_t = std::unordered_map<std::string /* operator_key */, size_t /*max_allocated*/>;

class operator_statistics_per_query {
 public:
  void add_invocation(const std::string& operator_key, size_t peak_memory);

  void write_to_query_statistics_response(Response_QueryStatistics* query_statistics);

  // Warning: This function is for testing purposes only since it is not thread-safe.
  [[nodiscard]] const memory_stats_map_t& unsafe_get_stats_map() const noexcept { return memory_stats_map_; }

 private:
  memory_stats_map_t memory_stats_map_{};
  mutable std::mutex mutex_{};
};

}  // namespace celonis::accelerator::cube::execution::tracking
