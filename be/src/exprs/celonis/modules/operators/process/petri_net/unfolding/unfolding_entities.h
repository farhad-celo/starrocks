#pragma once

#include <deque>
#include <vector>

#include "modules/operators/process/petri_net/petri_net.h"
#include "modules/operators/process/petri_net/petri_net_entities.h"
#include "modules/operators/process/petri_net/unfolding/unfolding_entities_fwd.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding {

class unfolding_condition {
 public:
  unfolding_condition(petri_net_place_id reference_place, unfolding_event* pre_event) noexcept
      : reference_place_{reference_place}, pre_event_{pre_event} {}

  void set_pre_event(unfolding_event* pre_event);
  [[nodiscard]] unfolding_event* get_pre_event() const;
  [[nodiscard]] const std::vector<unfolding_event*>& get_post_events() const;

  [[nodiscard]] bool is_initial_condition() const;

  [[nodiscard]] petri_net_place_id get_reference_place() const;

  void add_post_event(unfolding_event* post_event);

 private:
  petri_net_place_id reference_place_{};
  // Unfolding conditions ALWAYS have only one pre-event
  unfolding_event* pre_event_{nullptr};
  std::vector<unfolding_event*> post_events_{};
};

class unfolding_event {
 public:
  unfolding_event(petri_net_transition_id reference_transition, std::vector<unfolding_condition*> pre_set)
      : reference_transition_{reference_transition}, pre_conditions_{std::move(pre_set)} {}

  [[nodiscard]] uint16_t get_reference_transition_id() const;
  [[nodiscard]] petri_net_transition_id get_reference_transition() const;

  [[nodiscard]] const std::vector<unfolding_condition*>& get_pre_conditions() const;
  [[nodiscard]] const std::vector<unfolding_condition*>& get_post_conditions() const;

  /** This method cannot be called after get_local_configuration, get_foata_normal_form or get_marking have been called
   *    Because the computation of these methods require the pre_conditions
   */
  void add_pre_condition(unfolding_condition* pre_condition);
  void add_post_condition(unfolding_condition* post_condition);

  // TODO (goulart.e) consider if it's better to move this 3 methods to unfolding_net
  [[nodiscard]] const local_configuration_t& get_local_configuration();
  [[nodiscard]] const foata_normal_form_t& get_foata_normal_form();
  [[nodiscard]] const marking_type& get_marking(const petri_net_accessor& pn_accessor);

 private:
  // Each Unfolding Event is a pair (S, t) where S is the preset (pre-conditions)
  // and t is a transition on the original petri net (reference_transition)
  petri_net_transition_id reference_transition_{};
  std::vector<unfolding_condition*> pre_conditions_{};
  std::vector<unfolding_condition*> post_conditions_{};
  // Local configuration and foata normal forms are cached for reuse
  local_configuration_t local_configuration_{};
  foata_normal_form_t foata_normal_form_{};
  marking_type marking_{};

  void compute_local_configuration();
  void compute_foata_normal_form();
  void compute_marking(const petri_net_accessor& pn_accessor);
};

struct unfolding_data {
  std::deque<unfolding_condition> unfolding_conditions{};
  std::deque<unfolding_event> unfolding_events{};
};

/**
 * An unfolding_net is in fact a special type of petri net
 * However, for the unfolding_net computation it's necessary to be able to expand it
 */
class unfolding_net {
 public:
  unfolding_net() = default;
  unfolding_net(const unfolding_net& other) = delete;
  unfolding_net& operator=(const unfolding_net& other) = delete;
  unfolding_net(unfolding_net&& other) = delete;
  unfolding_net& operator=(unfolding_net&& other) = delete;

  unfolding_net(const unfolding_representation& unf_repr, const input_output_mapper& io_mapper);

  [[maybe_unused]] unfolding_condition* add_initial_condition(petri_net_place_id reference_place);

  [[nodiscard]] unfolding_condition* create_condition(petri_net_place_id reference_place, unfolding_event* pre_event);

  void add_condition(unfolding_condition* condition);

  [[nodiscard]] unfolding_event* create_event(petri_net_transition_id transition_id,
                                              std::vector<unfolding_condition*> pre_set);

  void add_event(unfolding_event* event);

  [[nodiscard]] const std::unordered_set<unfolding_condition*>& get_initial_conditions() const;

  [[nodiscard]] const std::vector<unfolding_event*>& get_unfolding_events() const;

  [[nodiscard]] const std::vector<unfolding_condition*>& get_unfolding_conditions() const;

  [[nodiscard]] std::vector<unfolding_condition*> conditions_for_place(petri_net_place_id place_id) const;

 private:
  // Unfolding data holds all the created nodes.
  // Not all created nodes are in fact part of the unfolding_net.
  // Some are just Possible Extensions
  unfolding_data data_{};

  std::vector<unfolding_condition*> unfolding_conditions_{};
  std::vector<unfolding_event*> unfolding_events_{};
  std::unordered_set<unfolding_condition*> initial_conditions_{};
  std::unordered_multimap<petri_net_place_id, unfolding_condition*, hash_place> place_to_unfolding_conditions_{};
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding
