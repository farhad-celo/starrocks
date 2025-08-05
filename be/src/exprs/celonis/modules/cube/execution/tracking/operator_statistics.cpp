#include "operator_statistics.h"

#include <sstream>

#include <fmt/format.h>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/conversion.h"
#include "legacy_embedded_format/json/json.h"
#include "log/log.h"
#ifndef CELOSTAR
#include "modules/query/queries.pb.h"
#endif

namespace celonis::accelerator::cube::execution::tracking {

legacy_embedded_format::json::json_object_t serialize_data_points(const resource_usage_data_points& data_points) {
  legacy_embedded_format::json::json_object_t json{};
  auto& working_memory{json["working_memory"]};
  working_memory["estimated"] = data_points.working_memory.estimated;
  working_memory["actual"] = data_points.working_memory.actual;
  working_memory["ratio"] = data_points.working_memory.ratio();
  auto& output_column_size{json["output_column_size"]};
  output_column_size["estimated"] = data_points.output_column_size.estimated;
  output_column_size["actual"] = data_points.output_column_size.actual;
  output_column_size["ratio"] = data_points.output_column_size.ratio();
  return json;
}

namespace {

template <typename T>
[[nodiscard]] T* add_or_get(std::unordered_map<std::string, T>& map, const std::string& operator_key) {
  // prevent large memory consumptions. More than 1000 values is anyway to large to log
  if (map.size() < 1000 || map.find(operator_key) != map.end()) {
    return &map[operator_key];
  }
  return nullptr;
}

void log_operator_statistics(const stats_map_t& stats_map) {
  for (const auto& [operator_key, stat] : stats_map) {
    legacy_embedded_format::json::json_object_t json;
    {
      auto& operator_runtime{json["Operator_Runtime"]};
      operator_runtime["operator"] = operator_key;
      operator_runtime["invocations"] = stat.total_invocations;
      operator_runtime["runtimeMillis"] = stat.total_runtime.count();
      operator_runtime["peakMemory"] = stat.peak_memory_sum;

      {
        auto& cached{operator_runtime["cached"]};
        cached["invocations"] = stat.cached_invocations;
        cached["runtimeMillis"] = stat.cached_runtime.count();
      }
      {
        auto& non_cached{operator_runtime["nonCached"]};
        non_cached["invocations"] = stat.non_cached_invocations;
        non_cached["runtimeMillis"] = stat.non_cached_runtime.count();
      }
    }

    log::jinfo("Operator Runtime", json);
  }
}

void log_statistics_drill_down(const stats_map_t& stats_map) {
  for (const auto& [operator_key, stat] : stats_map) {
    for (const auto& [operator_stage, drilled_stats] : stat.drilled_down_statistics) {
      legacy_embedded_format::json::json_object_t json;
      {
        auto& operator_runtime{json["Operator_Runtime_Drilldown"][operator_key]};
        operator_runtime["stage"] = operator_stage;
        operator_runtime["invocations"] = drilled_stats.total_invocations;
        operator_runtime["runtimeMillis"] = drilled_stats.total_runtime.count();
      }

      log::jinfo("Operator Runtime Drilldown", json);
    }
  }
}

void log_operator_telemetry(const stats_map_t& stats_map) {
  legacy_embedded_format::json::json_object_t json;
  auto& telemetry_obj{json["Operator_Telemetry"]};
  bool has_telemetry{false};

  for (const auto& [operator_key, stat] : stats_map) {
    if (stat.operator_telemetry.empty()) {
      continue;
    }

    has_telemetry = true;
    auto& operator_telemetry{telemetry_obj[operator_key]};

    const auto invocation_count{stat.total_invocations};
    operator_telemetry["invocation_count"] = invocation_count;
    for (const auto& [counter_name, counter] : stat.operator_telemetry) {
      operator_telemetry[counter_name] = counter;
    }
  }

  if (has_telemetry) {
    log::jinfo("Operator Telemetry", json);
  }
}

void log_operator_resource_usage(const stats_map_t& stats_map) {
  for (const auto& [operator_key, stat] : stats_map) {
    legacy_embedded_format::json::json_object_t json{};
    auto& resource_usage{json["Operator_Resource_Usage"]};
    resource_usage["operator"] = operator_key;
    resource_usage["all_avg"] = serialize_data_points(stat.resource_usage.all_avg.get());

    resource_usage["underestimated_working_memory_avg"] =
        serialize_data_points(stat.resource_usage.underestimated_working_memory_avg.get());
    resource_usage["underestimated_output_column_size_avg"] =
        serialize_data_points(stat.resource_usage.underestimated_output_column_size_avg.get());

    resource_usage["overestimated_working_memory_avg"] =
        serialize_data_points(stat.resource_usage.overestimated_working_memory_avg.get());
    resource_usage["overestimated_output_column_size_avg"] =
        serialize_data_points(stat.resource_usage.overestimated_output_column_size_avg.get());

    resource_usage["underestimated_working_memory_max"] =
        serialize_data_points(stat.resource_usage.underestimated_working_memory_max.get());
    resource_usage["underestimated_output_column_size_max"] =
        serialize_data_points(stat.resource_usage.underestimated_output_column_size_max.get());

    resource_usage["overestimated_working_memory_max"] =
        serialize_data_points(stat.resource_usage.overestimated_working_memory_max.get());
    resource_usage["overestimated_output_column_size_max"] =
        serialize_data_points(stat.resource_usage.overestimated_output_column_size_max.get());

    log::jinfo("Operator Resource Usage", json);
  }
}

#ifndef CELOSTAR
bool cmp(std::pair<std::string, size_t>& a, std::pair<std::string, size_t>& b) { return a.second > b.second; }

std::vector<std::pair<std::string, size_t>> sort_descending(const memory_stats_map_t& map) {
  std::vector<std::pair<std::string, size_t>> vector;

  for (const auto& it : map) {
    vector.emplace_back(it);
  }

  sort(vector.begin(), vector.end(), cmp);
  return vector;
}
#endif

}  // namespace

resource_usage_data_points averaged_resource_usage_data_points::add_data_points(
    const resource_usage_data_points& data_points) {
  resource_usage_data_points historical{get()};
  ++count;
  bool overflow_occurred{false};
  overflow_occurred |=
      __builtin_uaddl_overflow(sum.input_column_size, data_points.input_column_size, &sum.input_column_size);
  overflow_occurred |= __builtin_uaddl_overflow(sum.working_memory.estimated, data_points.working_memory.estimated,
                                                &sum.working_memory.estimated);
  overflow_occurred |= __builtin_uaddl_overflow(sum.working_memory.actual, data_points.working_memory.actual,
                                                &sum.working_memory.actual);
  overflow_occurred |= __builtin_uaddl_overflow(
      sum.output_column_size.estimated, data_points.output_column_size.estimated, &sum.output_column_size.estimated);
  overflow_occurred |= __builtin_uaddl_overflow(sum.output_column_size.actual, data_points.output_column_size.actual,
                                                &sum.output_column_size.actual);
  if (overflow_occurred) [[unlikely]] {
    count = 1;
    sum = data_points;
  }
  return historical;
}

resource_usage_data_points averaged_resource_usage_data_points::get() const {
  auto non_zero_count{count + static_cast<size_t>(count == 0)};
  return {sum.input_column_size / non_zero_count,
          {sum.working_memory.estimated / non_zero_count, sum.working_memory.actual / non_zero_count},
          {sum.output_column_size.estimated / non_zero_count, sum.output_column_size.actual / non_zero_count}};
}

void max_resource_usage_data_points::add_data_points(const resource_usage_data_points& data_points) {
  if (data_points.working_memory.actual > max.working_memory.actual ||
      data_points.output_column_size.actual > max.output_column_size.actual) {
    max = data_points;
  }
}

resource_usage_data_points resource_usage_stats::add_data_points(const resource_usage_data_points& data_points) {
  if (data_points.working_memory.estimated < data_points.working_memory.actual) {
    underestimated_working_memory_avg.add_data_points(data_points);
    underestimated_working_memory_max.add_data_points(data_points);
  } else if (data_points.working_memory.estimated > data_points.working_memory.actual) {
    overestimated_working_memory_avg.add_data_points(data_points);
    overestimated_working_memory_max.add_data_points(data_points);
  }
  if (data_points.output_column_size.estimated < data_points.output_column_size.actual) {
    underestimated_output_column_size_avg.add_data_points(data_points);
    underestimated_output_column_size_max.add_data_points(data_points);
  } else if (data_points.output_column_size.estimated > data_points.output_column_size.actual) {
    overestimated_output_column_size_avg.add_data_points(data_points);
    overestimated_output_column_size_max.add_data_points(data_points);
  }
  return all_avg.add_data_points(data_points);
}

void operator_statistics::add_invocation(const std::string& operator_key, const std::chrono::milliseconds time,
                                         const size_t peak_memory) {
  const std::scoped_lock lock{mutex_};

  if (auto* const stat{add_or_get(stats_map_, operator_key)}) {
    stat->total_invocations++;
    stat->total_runtime += time;
    stat->peak_memory_sum += peak_memory;
  }
}

void operator_statistics::add_cached_operator_invocation(const std::string& operator_key, const bool is_cached,
                                                         const std::chrono::milliseconds time) {
  const std::scoped_lock lock{mutex_};

  if (auto* const stat{add_or_get(stats_map_, operator_key)}) {
    if (is_cached) {
      stat->cached_invocations++;
      stat->cached_runtime += time;
    } else {
      stat->non_cached_invocations++;
      stat->non_cached_runtime += time;
    }
  }
}

void operator_statistics::add_sub_stage_invocation(const std::string& operator_key, const std::string& stage_name,
                                                   std::chrono::milliseconds time) {
  const std::scoped_lock lock{mutex_};

  if (auto* const stat{add_or_get(stats_map_, operator_key)}) {
    if (auto* const drilled_stats{add_or_get(stat->drilled_down_statistics, stage_name)}) {
      drilled_stats->total_invocations++;
      drilled_stats->total_runtime += time;
    }
  }
}

void operator_statistics::add_telemetry_counter(const std::string& operator_key, const std::string& counter_name,
                                                size_t count) {
  const std::scoped_lock lock{mutex_};

  if (auto* const stat{add_or_get(stats_map_, operator_key)}) {
    if (auto* const drilled_stats{add_or_get(stat->operator_telemetry, counter_name)}) {
      *drilled_stats += count;
    }
  }
}

std::optional<resource_usage_data_points> operator_statistics::report_resource_usage(
    const std::string& operator_key, const resource_usage_data_points& data_points) {
  const std::scoped_lock lock{mutex_};
  if (auto* stat{add_or_get(stats_map_, operator_key)}; stat != nullptr) {
    return stat->resource_usage.add_data_points(data_points);
  }
  return std::nullopt;
}

void operator_statistics::log_statistics_and_reset() {
  stats_map_t new_stats{};
  {
    const std::scoped_lock lock{mutex_};
    std::swap(new_stats, stats_map_);
  }

  if (!new_stats.empty()) {
    log_operator_statistics(new_stats);
    log_statistics_drill_down(new_stats);
    log_operator_telemetry(new_stats);
    log_operator_resource_usage(new_stats);
  }
}

void operator_statistics_per_query::add_invocation(const std::string& operator_key, const size_t peak_memory) {
  const std::scoped_lock lock{mutex_};

  if (auto* const current_max{add_or_get(memory_stats_map_, operator_key)}) {
    *current_max = std::max(*current_max, peak_memory);
  }
}

#ifndef CELOSTAR
void operator_statistics_per_query::write_to_query_statistics_response(
    Response_QueryStatistics* const query_statistics) {
  const std::scoped_lock lock{mutex_};

  if (!memory_stats_map_.empty()) {
    auto sorted_values{sort_descending(memory_stats_map_)};
    for (const auto& operator_value_pair : sorted_values) {
      auto* operator_statistic{query_statistics->add_operator_memory_statistics()};
      operator_statistic->set_operator_key(operator_value_pair.first);
      operator_statistic->set_max_allocated(legacy_embedded_ctl::cast<int64_t>(operator_value_pair.second));
    }
  }
}
#endif

}  // namespace celonis::accelerator::cube::execution::tracking
