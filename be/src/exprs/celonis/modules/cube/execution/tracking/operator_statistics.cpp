#include "operator_statistics.h"

#include <sstream>

#include <fmt/format.h>

#include "ctl/assert.h"
#include "ctl/conversion.h"
#include "format/json/json.h"
#include "log/log.h"
#include "modules/query/queries.pb.h"

namespace celonis::accelerator::cube::execution::tracking {

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
    format::json::json_object_t json;
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
      format::json::json_object_t json;
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
  format::json::json_object_t json;
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

bool cmp(std::pair<std::string, size_t>& a, std::pair<std::string, size_t>& b) { return a.second > b.second; }

std::vector<std::pair<std::string, size_t>> sort_descending(const memory_stats_map_t& map) {
  std::vector<std::pair<std::string, size_t>> vector;

  for (const auto& it : map) {
    vector.emplace_back(it);
  }

  sort(vector.begin(), vector.end(), cmp);
  return vector;
}

}  // namespace

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
  }
}

void operator_statistics_per_query::add_invocation(const std::string& operator_key, const size_t peak_memory) {
  const std::scoped_lock lock{mutex_};

  if (auto* const current_max{add_or_get(memory_stats_map_, operator_key)}) {
    *current_max = std::max(*current_max, peak_memory);
  }
}

void operator_statistics_per_query::write_to_query_statistics_response(
    Response_QueryStatistics* const query_statistics) {
  const std::scoped_lock lock{mutex_};

  if (!memory_stats_map_.empty()) {
    auto sorted_values{sort_descending(memory_stats_map_)};
    for (const auto& operator_value_pair : sorted_values) {
      auto* operator_statistic{query_statistics->add_operator_memory_statistics()};
      operator_statistic->set_operator_key(operator_value_pair.first);
      operator_statistic->set_max_allocated(ctl::cast<int64_t>(operator_value_pair.second));
    }
  }
}

}  // namespace celonis::accelerator::cube::execution::tracking
