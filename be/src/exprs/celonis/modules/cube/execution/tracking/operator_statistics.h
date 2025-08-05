#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "legacy_embedded_format/json/json_fwd.h"
#include "modules/cube/execution/tracking/operator_statistics_fwd.h"

#ifndef CELOSTAR
namespace celonis::accelerator {
class Response_QueryStatistics;
}  // namespace celonis::accelerator
#endif

namespace celonis::accelerator::cube::execution::tracking {

struct drill_down_stats {
  // Total invocations/runtime, which might be cached or not cached, no further information is available
  int64_t total_invocations{};
  std::chrono::milliseconds total_runtime{};
};

using drilled_stats_map_t = std::unordered_map<std::string /* operation_stage */, drill_down_stats>;

using operator_telemetry_map_t = std::unordered_map<std::string /* counter name */, size_t /* counter */>;

struct resource_usage_data_point {
  std::size_t estimated{};
  std::size_t actual{};
  [[nodiscard]] double ratio() const { return static_cast<double>(estimated) / static_cast<double>(actual); }
};

struct resource_usage_data_points {
  std::size_t input_column_size{};
  resource_usage_data_point working_memory{};
  resource_usage_data_point output_column_size{};
};

legacy_embedded_format::json::json_object_t serialize_data_points(const resource_usage_data_points& data_points);

class averaged_resource_usage_data_points {
 public:
  /** Add data points to aggregates
   *
   * @return averaged data points
   */
  resource_usage_data_points add_data_points(const resource_usage_data_points& data_points);

  /** Get averaged data points
   *
   * @return averaged data points
   */
  [[nodiscard]] resource_usage_data_points get() const;

 private:
  std::size_t count{};
  resource_usage_data_points sum{};
};

class max_resource_usage_data_points {
 public:
  void add_data_points(const resource_usage_data_points& data_points);
  [[nodiscard]] const resource_usage_data_points& get() const { return max; }

 private:
  resource_usage_data_points max{};
};

struct resource_usage_stats {
  /** Add data points to aggregates
   *
   * @return averaged data points over all invocations
   */
  resource_usage_data_points add_data_points(const resource_usage_data_points& data_points);

  averaged_resource_usage_data_points all_avg{};
  averaged_resource_usage_data_points underestimated_working_memory_avg{};
  averaged_resource_usage_data_points underestimated_output_column_size_avg{};
  averaged_resource_usage_data_points overestimated_working_memory_avg{};
  averaged_resource_usage_data_points overestimated_output_column_size_avg{};
  max_resource_usage_data_points underestimated_working_memory_max{};
  max_resource_usage_data_points underestimated_output_column_size_max{};
  max_resource_usage_data_points overestimated_working_memory_max{};
  max_resource_usage_data_points overestimated_output_column_size_max{};
};

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
  resource_usage_stats resource_usage{};
};

using stats_map_t = std::unordered_map<std::string /* operator_key */, operator_runtime_stats>;

class operator_statistics {
 public:
  void add_invocation(const std::string& operator_key, std::chrono::milliseconds time, size_t peak_memory);
  void add_cached_operator_invocation(const std::string& operator_key, bool is_cached, std::chrono::milliseconds time);

  void add_sub_stage_invocation(const std::string& operator_key, const std::string& stage_name,
                                std::chrono::milliseconds time);
  void add_telemetry_counter(const std::string& operator_key, const std::string& counter_name, size_t count);

  std::optional<resource_usage_data_points> report_resource_usage(const std::string& operator_key,
                                                                  const resource_usage_data_points& data_points);

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

#ifndef CELOSTAR
  void write_to_query_statistics_response(Response_QueryStatistics* query_statistics);
#endif

  // Warning: This function is for testing purposes only since it is not thread-safe.
  [[nodiscard]] const memory_stats_map_t& unsafe_get_stats_map() const noexcept { return memory_stats_map_; }

 private:
  memory_stats_map_t memory_stats_map_{};
  mutable std::mutex mutex_{};
};

}  // namespace celonis::accelerator::cube::execution::tracking
