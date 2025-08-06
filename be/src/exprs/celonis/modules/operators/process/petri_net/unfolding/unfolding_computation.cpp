#include "unfolding_computation.h"

#include <memory>

#include "legacy_embedded_ctl/source_location.h"
#include "modules/common/exceptions.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding {

namespace {

std::unordered_set<unfolding_condition*> get_preset(const petri_net_transition& pn_transition, unfolding_event* event) {
  std::unordered_set<unfolding_condition*> preset{};
  std::unordered_set<petri_net_place_id, hash_place> pre_places{};

  for (const auto& place : pn_transition.in_places) {
    pre_places.insert(place);
  }

  for (auto* post_condition : event->get_post_conditions()) {
    if (pre_places.count(post_condition->get_reference_place()) != 0) {
      preset.insert(post_condition);
    }
  }

  return preset;
}

petri_net_place_id get_place_not_in_preset(const petri_net_transition& pn_transition,
                                           const std::unordered_set<unfolding_condition*>& preset) {
  std::unordered_set<petri_net_place_id, hash_place> places{};
  std::unordered_set<petri_net_place_id, hash_place> preset_places{};

  for (auto* condition : preset) {
    preset_places.insert(condition->get_reference_place());
  }

  for (const auto& in_place : pn_transition.in_places) {
    if (preset_places.count(in_place) == 0) {
      return in_place;
    }
  }

  // Control shouldn't reach here.
  // Initially pre_set is a subset of pn_transition.in_places (see "get_preset()")
  // Inside cover(), the condition pn_transition.in_places.size() == preset.size() is checked
  //  If it's false, then this method is called, the preset is extended by one and cover() is called again
  // Therefore, we are sure to have preset.size() < pn_transition.in_places.size()
  //  for all invocations of this method
  // So there's at least one input place not in pre_set and control should never reach here
  throw common::internal_exception{"ALIGN: Control reached unexpected line {}.", legacy_embedded_ctl::source_location{}};
}

}  // namespace

unfolding_computation::unfolding_computation(const petri_net_accessor& pn_accessor) noexcept
    : pn_accessor_{pn_accessor} {}

unfolding_representation unfolding_computation::compute_unfolding() {
  add_initial_conditions();
  add_initial_possible_extensions();

  // Main loop of algorithm
  while (!possible_extensions_.empty()) {
    auto* minimal_event{get_minimal_event()};
    possible_extensions_.pop();

    if (is_cutoff(minimal_event)) {
      // Add to list of cutoffs
      cutoff_events_.insert(minimal_event);
    } else {
      // Add event and conditions to unfolding_net
      expand_unfolding(minimal_event);
      // Update possible extensions
      update_co_occurrences(minimal_event);
      update_possible_extensions(minimal_event);
    }
  }

  // Add cutoff events
  for (auto* cutoff_event : cutoff_events_) {
    expand_unfolding(cutoff_event);
  }

  return get_unfolding_representation();
}

void unfolding_computation::add_initial_conditions() {
  // Add initial conditions to unfolding_net
  const auto initial_places{pn_accessor_.get_initial_places()};
  for (const auto& initial_place : initial_places) {
    unfolding_.add_initial_condition(initial_place);
  }

  // Update co_occurrences (all initial conditions are co-occurrent)
  for (auto* cond_i : unfolding_.get_initial_conditions()) {
    for (auto* cond_j : unfolding_.get_initial_conditions()) {
      if (cond_i == cond_j) {
        continue;
      }
      co_occurrences_.emplace(cond_i, cond_j);
    }
  }
}

void unfolding_computation::add_initial_possible_extensions() {
  // Initial PE set is the set of all events with transitions enabled by the initial marking
  //  For loop here potentially bad,
  //  but in the case of RLAlign is ok since we assume a workflow net (only one start condition)
  const auto initial_enabled_transitions{pn_accessor_.get_enabled_transitions(pn_accessor_.get_initial_marking())};
  for (auto transition : initial_enabled_transitions) {
    const auto& pn_transition{pn_accessor_[transition]};

    std::vector<unfolding_condition*> pre_set{};
    for (const auto& place : pn_transition.in_places) {
      for (auto* initial_condition : unfolding_.get_initial_conditions()) {
        if (initial_condition->get_reference_place() == place) {
          pre_set.push_back(initial_condition);
          break;
        }
      }
    }

    auto* unf_event{unfolding_.create_event(transition, std::move(pre_set))};
    possible_extensions_.push(unf_event);
  }
}

unfolding_event* unfolding_computation::get_minimal_event() const { return possible_extensions_.top(); }

bool unfolding_computation::is_cutoff(unfolding_event* event) const {
  for (auto* unf_event : unfolding_.get_unfolding_events()) {
    const auto& unf_marking{unf_event->get_marking(pn_accessor_)};
    const auto& marking{event->get_marking(pn_accessor_)};

    if (unf_event != event && unf_marking == marking) {
      return true;
    }
  }

  return false;
}

void unfolding_computation::expand_unfolding(unfolding_event* event) {
  unfolding_.add_event(event);
  for (const auto out_place : pn_accessor_[event->get_reference_transition()].out_places) {
    auto* condition{unfolding_.create_condition(out_place, event)};
    unfolding_.add_condition(condition);
    event->add_post_condition(condition);
  }
}

void unfolding_computation::update_co_occurrences(unfolding_event* event) {
  const auto& conditions{unfolding_.get_unfolding_conditions()};
  std::unordered_set<unfolding_condition*> conditions_to_delete{};

  for (auto* condition : conditions) {
    for (auto* pre_condition : event->get_pre_conditions()) {
      conditions_to_delete.insert(pre_condition);
      if (co_occurrences_.count({pre_condition, condition}) == 0) {
        conditions_to_delete.insert(condition);
      }
    }
  }

  for (auto* post_cond : event->get_post_conditions()) {
    for (auto* post_cond_2 : event->get_post_conditions()) {
      co_occurrences_.emplace(post_cond, post_cond_2);
    }
    for (auto* unf_condition : conditions) {
      if (conditions_to_delete.count(unf_condition) == 0) {
        co_occurrences_.emplace(unf_condition, post_cond);
        co_occurrences_.emplace(post_cond, unf_condition);
      }
    }
  }
}

void unfolding_computation::update_possible_extensions(unfolding_event* event) {
  std::unordered_set<unfolding_event*> extensions{};

  const auto possible_transitions_to_extend{get_possible_transitions_to_extend(event)};
  for (const auto& transition : possible_transitions_to_extend) {
    const auto pn_transition{pn_accessor_[transition]};
    auto preset{get_preset(pn_transition, event)};
    auto C{concurrent_conditions_for_event(event)};
    cover(extensions, C, pn_transition, preset);
  }
  extend_possible_extensions(extensions);
}

std::unordered_set<petri_net_transition_id, hash_transition> unfolding_computation::get_possible_transitions_to_extend(
    unfolding_event* event) const {
  std::unordered_set<petri_net_transition_id, hash_transition> transitions_to_extend{};

  const auto& u{pn_accessor_[event->get_reference_transition()]};
  const auto& u_pre{u.in_places};
  const auto& u_post_places{u.out_places};

  for (const auto& u_post_place : u_post_places) {
    const auto& pn_post_place{pn_accessor_[u_post_place]};
    for (const auto& transition : pn_post_place.out_transitions) {
      transitions_to_extend.insert(transition);
    }
  }

  std::vector<petri_net_place> diff_pre_post{};
  for (const auto& u_pre_place : u_pre) {
    bool add{true};
    for (const auto& u_post_place : u_post_places) {
      if (u_pre_place.id == u_post_place.id) {
        add = false;
        break;
      }
    }
    if (add) {
      const auto& pn_place{pn_accessor_[u_pre_place]};
      diff_pre_post.push_back(pn_place);
    }
  }

  for (const auto& pn_place : diff_pre_post) {
    for (const auto& transition : pn_place.out_transitions) {
      transitions_to_extend.erase(transition);
    }
  }

  return transitions_to_extend;
}

std::vector<unfolding_condition*> unfolding_computation::concurrent_conditions_for_event(unfolding_event* event) const {
  std::vector<unfolding_condition*> concurrent_conditions{};

  for (auto* condition : unfolding_.get_unfolding_conditions()) {
    bool is_concurrent{true};
    for (auto* event_pre_condition : event->get_pre_conditions()) {
      if (!co_occurs(condition, event_pre_condition) || condition == event_pre_condition) {
        is_concurrent = false;
        break;
      }
    }
    if (is_concurrent) {
      concurrent_conditions.push_back(condition);
    }
  }

  return concurrent_conditions;
}

bool unfolding_computation::co_occurs(unfolding_condition* condition_1, unfolding_condition* condition_2) const {
  return co_occurrences_.find({condition_1, condition_2}) != co_occurrences_.end();
}

void unfolding_computation::cover(std::unordered_set<unfolding_event*>& extensions,
                                  const std::vector<unfolding_condition*>& C, const petri_net_transition& pn_transition,
                                  const std::unordered_set<unfolding_condition*>& preset) {
  if (pn_transition.in_places.size() == preset.size()) {
    auto* event{unfolding_.create_event(pn_transition.transition, std::vector(std::cbegin(preset), std::cend(preset)))};
    extensions.insert(event);
  } else {
    const auto place{get_place_not_in_preset(pn_transition, preset)};
    for (auto* condition : C) {
      if (condition->get_reference_place() == place) {
        // Get C'
        std::vector<unfolding_condition*> C_{};
        for (auto* cond : C) {
          if (co_occurs(condition, cond)) {
            C_.push_back(cond);
          }
        }
        // Get preset'
        auto preset_{preset};
        preset_.insert(condition);
        cover(extensions, C_, pn_transition, preset_);
      }
    }
  }
}

void unfolding_computation::extend_possible_extensions(const std::unordered_set<unfolding_event*>& extensions) {
  for (auto* unf_event : extensions) {
    possible_extensions_.push(unf_event);
  }
}

unfolding_representation unfolding_computation::get_unfolding_representation() const {
  unfolding_representation unf_representation{};

  uint16_t condition_counter{0};
  std::unordered_map<unfolding_condition*, std::string> condition_to_string_id{};
  for (auto* condition : unfolding_.get_unfolding_conditions()) {
    std::string condition_id{"c" + std::to_string(condition_counter)};
    ++condition_counter;
    unf_representation.petri_net_repr.places.insert(condition_id);
    if (unfolding_.get_initial_conditions().find(condition) != unfolding_.get_initial_conditions().end()) {
      unf_representation.petri_net_repr.initial_marking.insert(condition_id);
    }
    if (condition->get_post_events().empty()) {
      unf_representation.leaves.insert(condition_id);
    }

    condition_to_string_id.emplace(condition, condition_id);
    auto original_node_place_str_id{pn_accessor_[condition->get_reference_place()].place_str_id};
    unf_representation.original_node_id_to_condition_id.emplace(original_node_place_str_id, condition_id);
    unf_representation.condition_id_to_original_node_id.emplace(condition_id, original_node_place_str_id);
  }

  uint16_t event_counter{0};
  for (auto* event : unfolding_.get_unfolding_events()) {
    const std::string event_id{"e" + std::to_string(event_counter)};
    ++event_counter;
    const auto transition{event->get_reference_transition()};
    const auto transition_label{pn_accessor_.get_label(transition)};
    unf_representation.petri_net_repr.transitions.emplace(event_id, transition_label);

    auto original_node_transition_str_id{pn_accessor_[event->get_reference_transition()].transition_str_id};
    unf_representation.original_node_id_to_event_id.emplace(original_node_transition_str_id, event_id);
    unf_representation.event_id_to_original_node_id.emplace(event_id, original_node_transition_str_id);

    for (auto* post_condition : event->get_post_conditions()) {
      const auto post_condition_str_id{condition_to_string_id[post_condition]};
      unf_representation.petri_net_repr.transition_place_arcs.emplace(event_id, post_condition_str_id);
    }
    for (auto* pre_condition : event->get_pre_conditions()) {
      const auto pre_condition_str_id{condition_to_string_id[pre_condition]};
      unf_representation.petri_net_repr.place_transition_arcs.emplace(pre_condition_str_id, event_id);
    }
  }

  return unf_representation;
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding
