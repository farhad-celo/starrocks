#pragma once

#include <unordered_map>

#include "legacy_embedded_format/json/json_fwd.h"
#include "modules/common/int_types.h"
#ifndef CELOSTAR
#include "modules/cube/execution/tracking/operator_tracker_fwd.h"
#endif

namespace celonis::accelerator::operators::process {

class inductive_miner_statistics {
 public:
  explicit inductive_miner_statistics(std::unordered_map<std::string, size_t> data) : data_{std::move(data)} {}

  inductive_miner_statistics() {
    insert_or_assign(tree_size_key(), 0);
    insert_or_assign(activity_count_key(), 0);
    insert_or_assign(tau_transitions_count_key(), 0);
    insert_or_assign(empty_log_base_case_count_key(), 0);
    insert_or_assign(empty_traces_base_case_count_key(), 0);
    insert_or_assign(noisy_single_activity_base_case_count_key(), 0);
    insert_or_assign(single_activity_base_case_count_key(), 0);
    insert_or_assign(xor_count_key(), 0);
    insert_or_assign(seq_count_key(), 0);
    insert_or_assign(par_count_key(), 0);
    insert_or_assign(loop_count_key(), 0);
    insert_or_assign(noisy_xor_count_key(), 0);
    insert_or_assign(noisy_seq_count_key(), 0);
    insert_or_assign(noisy_par_count_key(), 0);
    insert_or_assign(noisy_loop_count_key(), 0);
    insert_or_assign(activity_concurrent_count_key(), 0);
    insert_or_assign(activity_concurrent_find_count_key(), 0);
    insert_or_assign(activity_concurrent_subfinds_count_key(), 0);
    insert_or_assign(activity_once_per_trace_count_key(), 0);
    insert_or_assign(strict_tau_loop_count_key(), 0);
    insert_or_assign(slack_tau_loop_count_key(), 0);
    insert_or_assign(flower_fallback_count_key(), 0);
    insert_or_assign(has_seq_xor_key(), 0);
    insert_or_assign(precision_key(), 0);
  }

  [[nodiscard]] static inline const std::string& tree_size_key() {
    static std::string key{"tree_size"};
    return key;
  }
  [[nodiscard]] static const std::string& activity_count_key() {
    static std::string key{"activity_count"};
    return key;
  }
  [[nodiscard]] static const std::string& tau_transitions_count_key() {
    static std::string key{"tau_transitions_count"};
    return key;
  }
  [[nodiscard]] static const std::string& empty_log_base_case_count_key() {
    static std::string key{"empty_log_base_case_count"};
    return key;
  }
  [[nodiscard]] static const std::string& empty_traces_base_case_count_key() {
    static std::string key{"empty_traces_base_case_count"};
    return key;
  }
  [[nodiscard]] static const std::string& noisy_single_activity_base_case_count_key() {
    static std::string key{"noisy_single_activity_base_case_count"};
    return key;
  }
  [[nodiscard]] static const std::string& single_activity_base_case_count_key() {
    static std::string key{"single_activity_base_case_count"};
    return key;
  }
  [[nodiscard]] static const std::string& xor_count_key() {
    static std::string key{"xor_count"};
    return key;
  }
  [[nodiscard]] static const std::string& seq_count_key() {
    static std::string key{"seq_count"};
    return key;
  }
  [[nodiscard]] static const std::string& par_count_key() {
    static std::string key{"par_count"};
    return key;
  }
  [[nodiscard]] static const std::string& loop_count_key() {
    static std::string key{"loop_count"};
    return key;
  }
  [[nodiscard]] static const std::string& noisy_xor_count_key() {
    static std::string key{"noisy_xor_count"};
    return key;
  }
  [[nodiscard]] static const std::string& noisy_seq_count_key() {
    static std::string key{"noisy_seq_count"};
    return key;
  }
  [[nodiscard]] static const std::string& noisy_par_count_key() {
    static std::string key{"noisy_par_count"};
    return key;
  }
  [[nodiscard]] static const std::string& noisy_loop_count_key() {
    static std::string key{"noisy_loop_count"};
    return key;
  }
  /*
   * The number of successful activity concurrent fallthroughs
   */
  [[nodiscard]] static const std::string& activity_concurrent_count_key() {
    static std::string key{"activity_concurrent_count"};
    return key;
  }
  /*
   * The number of times we tried finding an activity concurrent fallthrough
   */
  [[nodiscard]] static const std::string& activity_concurrent_find_count_key() {
    static std::string key{"activity_concurrent_find_count"};
    return key;
  }
  /*
   *  The number of cuts that were tried out within activity concurrent fallthroughs
   */
  [[nodiscard]] static const std::string& activity_concurrent_subfinds_count_key() {
    static std::string key{"activity_concurrent_subfinds_count"};
    return key;
  }
  [[nodiscard]] static const std::string& activity_once_per_trace_count_key() {
    static std::string key{"activity_once_per_trace_count"};
    return key;
  }
  [[nodiscard]] static const std::string& strict_tau_loop_count_key() {
    static std::string key{"strict_tau_loop_count"};
    return key;
  }
  [[nodiscard]] static const std::string& slack_tau_loop_count_key() {
    static std::string key{"slack_tau_loop_count"};
    return key;
  }
  [[nodiscard]] static const std::string& flower_fallback_count_key() {
    static std::string key{"flower_fallback_count"};
    return key;
  }
  [[nodiscard]] static const std::string& has_seq_xor_key() {
    static std::string key{"has_seq_xor"};
    return key;
  }
  [[nodiscard]] static const std::string& precision_key() {
    static std::string key{"m2a_precision_times_1E4"};
    return key;
  }
  [[nodiscard]] static const std::string& fitness_key() {
    static std::string key{"m2a_fitness_times_1E4"};
    return key;
  }

  /**
   * Adds a new value to the statistics or updates it if the value already exists
   * @param key The unique key of the statistics value as it is logged in data dog
   * @param value The statistics value
   */
  void insert_or_assign(const std::string& key, size_t value);

  /**
   * Increments the value for the given key by 1
   * @param key the key of the field to increment
   */
  void insert_or_increment(const std::string& key);

#ifdef CELOSTAR
  std::unordered_map<std::string, size_t>& data() { return data_; }
#else
  void log_to_operator_statistics(
      const cube::execution::tracking::add_telemetry_counter_fn& add_telemetry_counter) const;
#endif

  legacy_embedded_format::json::json_object_t to_json() const;

 private:
  std::unordered_map<std::string, size_t> data_{};
};

}  // namespace celonis::accelerator::operators::process
