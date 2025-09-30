#include "replay_caches.h"

#include <ctl/algorithm.h>
#include <ctl/utils/allocation_messages.h>
#include <log/log.h>

#include "modules/operators/process/bpmn/replay_types.h"
#include "modules/operators/process/bpmn/replay_utils.h"

namespace celonis::accelerator::operators::process::bpmn::details {

replay_caches::replay_caches(const common::execution_context& context)
    : enabled_transitions_cache_{context}, non_conforming_prefix_cache_{}, linearized_transitions_cache_{context} {}

replay_caches::enabled_transitions_cache::enabled_transitions_cache(const common::execution_context& context)
    : enabled_transitions_cache_{
          memory::management::checked_allocator<cache_t>(context, ALLOC_MSG(ctl::MEMBER_INIT_MSG))} {}

transitions_map_t replay_caches::enabled_transitions_cache::get_enabled_transitions(
    const cpml::model::bpmn_graph& model, const markings_t& markings) {
  transitions_map_t result{};

  for (const auto& marking : markings) {
    result.emplace(marking, get_cached_or_compute_enabled_transitions(model, marking));
  }

  return result;
}

const transitions_t& replay_caches::enabled_transitions_cache::get_cached_or_compute_enabled_transitions(
    const cpml::model::bpmn_graph& model, const marking_t& marking) {
  if (ctl::contains(enabled_transitions_cache_, marking)) {
    return enabled_transitions_cache_[marking];
  }

  const auto [inserted_element_iter, insert_successful]{
      enabled_transitions_cache_.emplace(marking, bpmn::get_enabled_transitions(model, marking))};
  debug_assert(insert_successful);
  return inserted_element_iter->second;
}

bool replay_caches::non_conforming_prefix_cache::contains_prefix_of(const activity_trace_t& variant) const {
  return non_conforming_prefix_trie_.contains_prefix_of(variant);
}

// todo(h.ashraf): Seems like the fix introduced for CPL-8120 did not resolve all issues with excessive error logs
static constexpr int MAX_ERROR_LOGS{1};

void replay_caches::non_conforming_prefix_cache::add(const a_star::non_conforming_subtrace_t& prefix) {
  if (!prefix.empty()) {
    non_conforming_prefix_trie_.add(prefix);
  }
}

void replay_caches::non_conforming_prefix_cache::add(a_star::non_conforming_subtrace_t&& prefix) {
  if (!prefix.empty()) {
    non_conforming_prefix_trie_.add(std::move(prefix));
  }
}

replay_caches::linearized_transitions_cache::linearized_transitions_cache(const common::execution_context& context)
    : linearized_transitions_cache_{
          memory::management::checked_allocator<cache_t>(context, ALLOC_MSG(ctl::MEMBER_INIT_MSG))} {}

replay_caches::linearized_transitions_cache::maybe_get_result_t replay_caches::linearized_transitions_cache::maybe_get(
    const activity_trace_t& variant) const {
  return ctl::contains(linearized_transitions_cache_, variant)
             ? maybe_get_result_t{linearized_transitions_cache_.at(variant)}
             : std::nullopt;
}

void replay_caches::linearized_transitions_cache::add(const activity_trace_t& variant,
                                                      const transitions_t& linearized_transition) {
  const bool success{linearized_transitions_cache_.emplace(variant, linearized_transition).second};
  static int errors_logged{0};
  if (!success && errors_logged < MAX_ERROR_LOGS) {
    // Such an error means we did not correctly check the cache before
    log::jerror("Tried to add a linearized transition to the cache which was already existing.");
    ++errors_logged;
  }
}

}  // namespace celonis::accelerator::operators::process::bpmn::details
