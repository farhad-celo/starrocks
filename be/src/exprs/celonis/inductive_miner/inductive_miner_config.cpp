#include "inductive_miner_config.h"

#ifdef CELOSTAR
#include "inductive_miner/splittable_eventlog.h"
#else
#include "modules/operators/process/inductive_miner/splittable_eventlog.h"
#endif

namespace celonis::accelerator::operators::process {

inductive_miner_config::inductive_miner_config(splittable_eventlog_config_t splittable_eventlog_config,
#ifdef CELOSTAR
                                               const common::execution_context& parent_context, size_t grain_size,
                                               const dfg_filter_config& filter_config)
#else
                                               const common::execution_context& parent_context, const size_t grain_size)
#endif
    : splittable_eventlog_config_{std::move(splittable_eventlog_config)},
      splittable_eventlog_{splittable_eventlog::extract(splittable_eventlog_config_, parent_context)},
      execution_context_{parent_context},
#ifdef CELOSTAR
      grain_size_{grain_size},
      filter_config_{filter_config} {}
#else
      grain_size_{grain_size} {}
#endif

const splittable_eventlog_config_t& inductive_miner_config::eventlog_config() const {
  return splittable_eventlog_config_;
}

const splittable_eventlog& inductive_miner_config::eventlog() const { return splittable_eventlog_; }

splittable_eventlog& inductive_miner_config::eventlog() { return splittable_eventlog_; }

const common::execution_context& inductive_miner_config::execution_context() const { return execution_context_; }

size_t inductive_miner_config::grain_size() const { return grain_size_; }

const dfg_filter_config& inductive_miner_config::filter_config() const { return filter_config_; }

dfg_filter_config& inductive_miner_config::filter_config() { return filter_config_; }

#ifndef CELOSTAR
bool inductive_miner_config::is_for_eventlog_based_approach() const {
  return std::holds_alternative<splittable_eventlog_config_for_using_entire_eventlog>(eventlog_config());
}

bool inductive_miner_config::is_for_variant_based_approach() const {
  return std::holds_alternative<splittable_eventlog_config_for_using_variants>(eventlog_config());
}

inductive_miner_config make_inductive_miner_config_for_eventlog_based_approach(
    memory::column_t activities, memory::column_t cases, cube::filter_bitset_t selections,
    const common::execution_context& parent_context, const size_t grain_size) {
  return inductive_miner_config{make_splittable_eventlog_config(std::move(activities), std::move(cases), grain_size,
                                                                std::move(selections), parent_context),
                                parent_context, grain_size};
}

inductive_miner_config make_inductive_miner_config_for_eventlog_based_approach(
    memory::column_t activities, memory::column_t cases, const common::execution_context& parent_context,
    const size_t grain_size) {
  return inductive_miner_config{
      make_splittable_eventlog_config(std::move(activities), std::move(cases), grain_size, parent_context),
      parent_context, grain_size};
}

inductive_miner_config make_inductive_miner_config_for_variant_based_approach(
    memory::cache::variant_trace_cache_t variant_trace_cache_ptr, const common::execution_context& parent_context,
    const size_t grain_size) {
  return inductive_miner_config{make_splittable_eventlog_config(std::move(variant_trace_cache_ptr), grain_size),
                                parent_context, grain_size};
}
#endif

}  // namespace celonis::accelerator::operators::process
