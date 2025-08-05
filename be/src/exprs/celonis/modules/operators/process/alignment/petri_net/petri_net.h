#pragma once

#include <iostream>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/container/pmr/polymorphic_allocator.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/functional/hash.hpp>

#include "legacy_embedded_ctl/utils/allocation_messages.h"
#include "legacy_embedded_ctl/utils/allocation_reason.h"
#include "modules/memory/management/memory_checked_containers.h"
#include "modules/memory/row_id.h"
#include "modules/operators/process/alignment/input_output_mapper.h"
#include "modules/operators/process/alignment/petri_net/petri_net_entities.h"
#include "modules/operators/process/alignment/rl_align/rl_align_configs_fwd.h"

namespace celonis::accelerator::operators::process::alignment::petri_net {

// TODO (goulart.e) split petri_net_representation and unfolding_representation. Also, they should carry their label ids
// as strings
/**
 * PN representation with labels strings mapped to row_id
 *  If a label string exists in the Activity column, its Activity ID is taken instead
 */
struct petri_net_representation {
  using label_type = string_to_int_mapper::label_id_t;

  std::unordered_set<std::string> places{};
  std::unordered_map<std::string, label_type> transitions{};
  std::unordered_multimap<std::string, std::string> place_transition_arcs{};
  std::unordered_multimap<std::string, std::string> transition_place_arcs{};
  std::unordered_set<std::string> initial_marking{};
  std::unordered_set<std::string> final_marking{};

  [[maybe_unused]] bool operator==(const petri_net_representation& other) const = default;

  [[nodiscard]] bool is_reachable_from(const std::string& source_place, const std::string& target_place) const;
  // #_visible_labels, #_distinct_visible_labels
  [[nodiscard]] std::pair<size_t, size_t> count_labels() const;
};

/**
 * An Unfolding is a special type of Petri Net
 * Places in an unfolding are called Conditions
 * Transitions in an unfolding are called Events
 * The Unfolding representation contains information to reconnect CUTOFF events
 */
struct unfolding_representation {
  petri_net_representation petri_net_repr{};

  /// Maps condition ids to place id on Petri Net (and back)
  std::unordered_map<std::string, std::string> condition_id_to_original_node_id{};
  std::unordered_multimap<std::string, std::string> original_node_id_to_condition_id{};

  /// Maps condition ids to transition id on Petri Net (and back)
  std::unordered_map<std::string, std::string> event_id_to_original_node_id{};
  std::unordered_multimap<std::string, std::string> original_node_id_to_event_id{};

  /// Leaf conditions
  std::unordered_set<std::string> leaves{};
};

petri_net_representation reconnect_unfolding(const unfolding_representation& unf_repr,
                                             rl_align::unfolding_config unf_config);

/**
 * Data object representing a SAFE Petri net.
 * Markings are represented as bitsets; therefore, no place can contain more than 2 tokens.
 */
class safe_petri_net_data {
 public:
  using label_type = petri_net_representation::label_type;

  safe_petri_net_data(input_output_mapper& io_mapper, const petri_net_representation& pn_repr,
                      std::string operator_name);

  std::vector<petri_net_place> pn_places{};
  std::vector<petri_net_transition> pn_transitions{};
  std::unordered_multimap<label_type, petri_net_transition_id> label_to_transitions{};
  marking_type initial_marking{};
  // Final markings have only one entry
  std::vector<marking_type> final_markings{};

  // To output error messages
  std::string operator_name;

 private:
  void add_places(input_output_mapper& io_mapper, const petri_net_representation& pn_repr);
  void add_transitions(input_output_mapper& io_mapper, const petri_net_representation& pn_repr);
  void add_place_transition_arcs(const input_output_mapper& io_mapper, const petri_net_representation& pn_repr);
  void add_transition_place_arcs(const input_output_mapper& io_mapper, const petri_net_representation& pn_repr);
  void build_label_to_transitions_map();
};

// 12 is set so that sizeof(transitions_container_t<petri_net_transition_id>) = 48, so we only double the memory usage
template <typename TRANSITION_TYPE>
using transitions_container_t = boost::container::small_vector<TRANSITION_TYPE, 12>;
static_assert(sizeof(transitions_container_t<petri_net_transition_id>) ==
              2 * sizeof(std::vector<petri_net_transition_id>));

/**
 * Accessor object for Petri net class. It offers an interface to manipulate the Petri net referred by this accessor,
 * which is stored as a const reference in the accessor object. Multiple accessors can be created for the same Petri net
 * (for parallelization for example).
 */
class petri_net_accessor {
 public:
  using transition_type = petri_net_transition_id;
  using label_type = petri_net_representation::label_type;
  using transition_list_type = transitions_container_t<transition_type>;
  using transition_span_type = std::span<const transition_type>;

  petri_net_accessor(const safe_petri_net_data& pn_data, const common::execution_context& context)
      : enabled_transitions_cache_{memory::management::checked_allocator<cache_t>(
            context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::ENABLED_TRANSITIONS_CACHE))},
        pn_data_{pn_data} {}

  // TODO (goulart.e) we wish to make this non-copiable, but right now we can't (see CPL-10395)
  // // This is non-copiable because we don't want to copy the cache
  // petri_net_accessor(const petri_net_accessor& pn_accessor) = delete;
  // petri_net_accessor& operator=(const petri_net_accessor& pn_accessor) = delete;
  // // Moving is fine
  // petri_net_accessor(petri_net_accessor&& pn_accessor) = default;
  // petri_net_accessor& operator=(petri_net_accessor&& pn_accessor) = default;

  [[nodiscard]] marking_type get_initial_marking() const;

  [[nodiscard]] std::vector<petri_net_place_id> get_initial_places() const;

  [[nodiscard]] bool is_transition_enabled(const marking_type& marking, petri_net_transition_id transition) const;

  /**
   * In the case that firing the transition would result in a marking containing a place with more than 1 token, i.e.
   * the Petri net is unsafe, this place is truncated to have only 1 token.
   *
   * This truncation can lead to wrong results later on, but it is acceptable since that the algorithm explicitly
   * asks for safe Petri nets.
   */
  [[nodiscard]] marking_type fire(const marking_type& marking, petri_net_transition_id transition) const;

  /** The method assumes that the given transition was enabled by the previous marking */
  // This is generally used when firing a sequence of transitions.
  //  In that case, one would be better off by having a function "fire_all_transitions"...
  void fire_no_alloc(marking_type& marking, petri_net_transition_id transition) const;

  /**
   * "Undo" the transition on the marking. Assumes that there is a marking that can fire the transition, thus leading to
   * the provided marking. If not, the result will be truncated!
   */
  [[nodiscard]] marking_type fire_inverse(const marking_type& marking, petri_net_transition_id transition) const;

  /**
   * "Undo" the transition on the marking. Assumes that there is a marking that can fire the transition, thus leading to
   * the provided marking. If not, the result will be truncated!
   */
  void fire_inverse_no_alloc(marking_type& marking, petri_net_transition_id transition) const;

  [[nodiscard]] transition_span_type get_enabled_transitions(const marking_type& marking) const;

  [[nodiscard]] size_t get_max_transition_id() const;

  /// Constructs Petri net marking with tokens on the list of places
  [[nodiscard]] marking_type get_marking(const std::vector<petri_net_place_id>& places) const;

  /**
   * Find all the transitions for which there exists a (not necessarily 1-bounded) marking
   * such that firing the transition on that marking will result in the provided marking.
   * In other words, find all transitions whose post-set is a subset of the marking
   * @param marking the marking after firing
   * @return all transitions that could have generated the marking, unconstrained in the previous marking
   */
  [[nodiscard]] std::vector<petri_net_transition_id> compatible_generating_transitions(
      const marking_type& marking) const;

  [[nodiscard]] const petri_net_place& operator[](const petri_net_place_id place) const noexcept {
    return pn_data_.pn_places[place.id];
  }
  [[nodiscard]] const petri_net_transition& operator[](const petri_net_transition_id& transition) const noexcept {
    return pn_data_.pn_transitions[transition.id];
  }

  [[nodiscard]] const std::vector<petri_net_transition>& get_transitions() const;

  /// Returns the transitions which are in the post-set of places in the transition's post-set
  ///  i.e. transitions T that are only one place of distance from the transition
  [[nodiscard]] std::vector<petri_net_transition_id> consecutive_transitions(
      const petri_net_transition& transition) const;

  /// A node is a fork if it is a transition with 2 outgoing edges. Fork nodes create parallel sections
  [[nodiscard]] bool is_fork_node(petri_net_transition_id transition) const;

  [[nodiscard]] const std::vector<marking_type>& get_final_markings() const;

  [[nodiscard]] bool is_final_marking(const marking_type& marking) const;

  [[nodiscard]] const std::string& get_user_visible_operator_name() const noexcept { return pn_data_.operator_name; }

  [[nodiscard]] label_type get_label(petri_net_transition_id transition) const {
    return pn_data_.pn_transitions[transition.id].label;
  }

  [[nodiscard]] transition_list_type get_transitions_for_label(label_type label) const;

  [[nodiscard]] transition_list_type get_silent_enabled_transitions(const marking_type& marking) const;

 private:
  using tracked_transition_list_type = memory::management::checked_vector_t<petri_net_transition_id>;
  using cache_t = memory::management::checked_ska_hash_map_t<marking_type, tracked_transition_list_type,
                                                             boost::hash<marking_type>, std::equal_to<>>;

  mutable cache_t enabled_transitions_cache_;
  const safe_petri_net_data& pn_data_;

  [[nodiscard]] static std::vector<petri_net_place_id> get_marked_place_ids(const marking_type& marking);
};

/**
 * Compared to petri_net_accessor, this object is expensive to create because of the parallel sections computation,
 * this is why we extract it here.
 */
class petri_net_accessor_with_parallel_sections {
  /**
   * Parallel sections are used in shortest path and behavioral relations computation to ensure that the relaxation
   * steps generate valid paths. In general graphs, a node can be reached by taking any of its incoming edges. In Petri
   * nets, a node can only be reached by taking all of its incoming edges.
   */
  class petri_net_parallel_sections {
   public:
    using parallel_sections_t = std::unordered_map<petri_net_transition_id, petri_net_transition_id, hash_transition>;

    [[nodiscard]] static petri_net_parallel_sections compute(const petri_net_accessor& pn_accessor);

    [[nodiscard]] bool is_section(petri_net_transition_id fork, petri_net_transition_id join) const {
      const auto parallel_section_from{parallel_sections_.find(fork)};
      if (parallel_section_from == parallel_sections_.end()) {
        return false;
      }
      return parallel_section_from->second == join;
    }

    [[nodiscard]] petri_net_transition_id at_join(petri_net_transition_id fork) const {
      return parallel_sections_.at(fork);
    }

    [[nodiscard]] const parallel_sections_t& sections() const { return parallel_sections_; }

   private:
    parallel_sections_t parallel_sections_{};
  };

 public:
  petri_net_accessor_with_parallel_sections(const safe_petri_net_data& pn_data,
                                            const common::execution_context& context)
      : accessor_{pn_data, context}, par_sections_{petri_net_parallel_sections::compute(accessor_)} {}

  [[nodiscard]] const petri_net_accessor& accessor() const { return accessor_; }
  [[nodiscard]] const petri_net_parallel_sections& par_sections() const { return par_sections_; }

 private:
  petri_net_accessor accessor_;
  petri_net_parallel_sections par_sections_;
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
