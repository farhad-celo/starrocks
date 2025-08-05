#include "inductive_miner_statistics.h"

#include "legacy_embedded_format/json/json.h"

namespace celonis::accelerator::operators::process {

#ifndef CELOSTAR
void inductive_miner_statistics::log_to_operator_statistics(
    const cube::execution::tracking::add_telemetry_counter_fn& add_telemetry_counter) const {
  for (const auto& [key, value] : data_) {
    add_telemetry_counter(key, value);
  }
}
#endif

void inductive_miner_statistics::insert_or_assign(const std::string& key, size_t value) {
  data_.insert_or_assign(key, value);
}

void inductive_miner_statistics::insert_or_increment(const std::string& key) { data_[key] += 1; }

legacy_embedded_format::json::json_object_t inductive_miner_statistics::to_json() const {
  legacy_embedded_format::json::json_object_t result{};
  for (const auto& [key, value] : data_) {
    result.emplace(key, value);
  }
  return result;
}

}  // namespace celonis::accelerator::operators::process
