#pragma once

#include <memory>

#include <boost/container_hash/hash.hpp>

#include <cpml/model/bpmn_graph_fwd.h>
#include <ctl/named_type_hash.h>

#include "modules/memory/management/memory_checked_containers.h"
#include "modules/operators/process/bpmn/a_star/replay.h"
#include "modules/operators/process/bpmn/prefix_trie.h"
#include "modules/operators/process/bpmn/replay_types.h"

namespace celonis::accelerator::operators::process::bpmn::details {

/**
 * Stores the internal caches used for a replaying an activity column on a bpmn model.
 */
class replay_caches {
 public:
  explicit replay_caches(const common::execution_context& context);

  [[nodiscard]] auto& enabled_transitions_cache() noexcept { return enabled_transitions_cache_; }
  [[nodiscard]] auto& non_conforming_prefix_cache() noexcept { return non_conforming_prefix_cache_; }
  [[nodiscard]] auto& linearized_transitions_cache() noexcept { return linearized_transitions_cache_; }

 private:
  /**
   * Wraps bpmn::get_enabled_transitions and provides caching functionality. This is possible since the BPMN model
   * remains constant during a single replay, therefore the enabled transitions of a given marking
   * do not change. Instead of on the data-model level, this cache is designed to be instantiated on the operator level.
   */
  class enabled_transitions_cache {
   public:
    explicit enabled_transitions_cache(const common::execution_context& context);
    /*
     * For a given BPMN model and markings, returns all the transitions that can fire. Can be used to see if a set of
     * markings has any enabled transitions defined.
     */
    [[nodiscard]] transitions_map_t get_enabled_transitions(const cpml::model::bpmn_graph& model,
                                                            const markings_t& markings);

   private:
    // TODO(bluppes): thread safety
    [[nodiscard]] const transitions_t& get_cached_or_compute_enabled_transitions(const cpml::model::bpmn_graph& model,
                                                                                 const marking_t& marking);

    using cache_t = memory::management::checked_ska_hash_map_t<marking_t, transitions_t, marking_hash>;
    cache_t enabled_transitions_cache_;
  };

  class non_conforming_prefix_cache final {
   public:
    void add(const a_star::non_conforming_subtrace_t& prefix);
    void add(a_star::non_conforming_subtrace_t&& prefix);
    [[nodiscard]] bool contains_prefix_of(const activity_trace_t& variant) const;

   private:
    prefix_trie non_conforming_prefix_trie_{};
  };

  class linearized_transitions_cache final {
   public:
    explicit linearized_transitions_cache(const common::execution_context& context);

    // std::optional does not support T&
    using maybe_get_result_t = std::optional<std::reference_wrapper<const transitions_t>>;
    [[nodiscard]] maybe_get_result_t maybe_get(const activity_trace_t& variant) const;
    void add(const activity_trace_t& variant, const transitions_t& linearized_transition);

   private:
    using cache_t =
        memory::management::checked_ska_hash_map_t<activity_trace_t, transitions_t, boost::hash<activity_trace_t>>;
    cache_t linearized_transitions_cache_;
  };

  // TODO(n.weber/b.luppes): benchmark how expensive the cache creation (i.e., warm up) is. We could keep the cache
  //  across query executions by having it in the cube. Same for the other caches
  class enabled_transitions_cache enabled_transitions_cache_;

  class non_conforming_prefix_cache non_conforming_prefix_cache_{};

  class linearized_transitions_cache linearized_transitions_cache_;
};

}  // namespace celonis::accelerator::operators::process::bpmn::details