#include "unfolding_entities.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "legacy_embedded_ctl/assert.h"
#include "modules/operators/process/petri_net/petri_net_entities.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding {

void unfolding_condition::set_pre_event(unfolding_event* pre_event) { pre_event_ = pre_event; }
unfolding_event* unfolding_condition::get_pre_event() const { return pre_event_; }

bool unfolding_condition::is_initial_condition() const { return pre_event_ == nullptr; }

const std::vector<unfolding_event*>& unfolding_condition::get_post_events() const { return post_events_; }

petri_net_place_id unfolding_condition::get_reference_place() const { return reference_place_; }

void unfolding_condition::add_post_event(unfolding_event* post_event) { post_events_.push_back(post_event); }

uint16_t unfolding_event::get_reference_transition_id() const { return reference_transition_.id; }

petri_net_transition_id unfolding_event::get_reference_transition() const { return reference_transition_; }

void unfolding_event::add_pre_condition(unfolding_condition* pre_condition) {
  legacy_embedded_debug_assert(foata_normal_form_.empty());
  legacy_embedded_debug_assert(local_configuration_.empty());
  legacy_embedded_debug_assert(marking_.empty());

  pre_conditions_.push_back(pre_condition);
}

const std::vector<unfolding_condition*>& unfolding_event::get_pre_conditions() const { return pre_conditions_; }

const std::vector<unfolding_condition*>& unfolding_event::get_post_conditions() const { return post_conditions_; }

void unfolding_event::add_post_condition(unfolding_condition* post_condition) {
  post_conditions_.push_back(post_condition);
}

const local_configuration_t& unfolding_event::get_local_configuration() {
  // Local configuration is never empty as at least the unfolding_event itself is in it
  //  therefore checking for emptiness is enough to check if it was already computed
  if (local_configuration_.empty()) {
    compute_local_configuration();
  }

  return local_configuration_;
}

const foata_normal_form_t& unfolding_event::get_foata_normal_form() {
  // Foata normal form is never empty as at least the unfolding_event itself is in it
  //  therefore checking for emptiness is enough to check if it was already computed
  if (foata_normal_form_.empty()) {
    compute_foata_normal_form();
  }
  return foata_normal_form_;
}

const marking_type& unfolding_event::get_marking(const petri_net_accessor& pn_accessor) {
  // Marking is initialized to empty vector,
  //  therefore, checking if it's empty suffices to check if it was already computed
  if (marking_.empty()) {
    compute_marking(pn_accessor);
  }
  return marking_;
}

void unfolding_event::compute_local_configuration() {
  // Get events that precede current event
  std::unordered_set<unfolding_event*> events_to_expand{};
  std::unordered_set<unfolding_event*> seen_events{};
  events_to_expand.insert(this);

  while (!events_to_expand.empty()) {
    auto* event_to_expand{*events_to_expand.begin()};
    seen_events.insert(event_to_expand);
    events_to_expand.erase(event_to_expand);

    for (auto* condition : event_to_expand->pre_conditions_) {
      if (seen_events.count(condition->get_pre_event()) == 0 && !condition->is_initial_condition()) {
        events_to_expand.insert(condition->get_pre_event());
      }
    }
  }

  local_configuration_.insert(local_configuration_.end(), seen_events.begin(), seen_events.end());

  // Sort by order of transition IDs
  std::sort(local_configuration_.begin(), local_configuration_.end(),
            [](unfolding_event* lhs, unfolding_event* rhs) -> bool {
              return lhs->reference_transition_.id < rhs->reference_transition_.id;
            });
}

void unfolding_event::compute_foata_normal_form() {
  // Add all events from local configuration into set
  std::unordered_set<unfolding_event*> remaining_events{};

  const auto& local_configuration{get_local_configuration()};
  remaining_events.insert(local_configuration.begin(), local_configuration.end());

  while (!remaining_events.empty()) {
    std::vector<unfolding_event*> min_events{};

    for (auto* event : remaining_events) {
      auto is_minimal{true};

      for (auto* condition : event->pre_conditions_) {
        auto* pre_event{condition->get_pre_event()};

        if (remaining_events.find(pre_event) != remaining_events.end()) {
          is_minimal = false;
          break;
        }
      }

      if (is_minimal) {
        min_events.push_back(event);
      }
    }

    for (auto* event : min_events) {
      remaining_events.erase(event);
    }

    std::sort(min_events.begin(), min_events.end(), [](unfolding_event* lhs, unfolding_event* rhs) -> bool {
      return lhs->reference_transition_.id < rhs->reference_transition_.id;
    });

    foata_normal_form_.push_back(min_events);
  }
}

void unfolding_event::compute_marking(const petri_net_accessor& pn_accessor) {
  const auto& local_configuration{get_local_configuration()};
  auto current_marking{pn_accessor.get_initial_marking()};

  std::unordered_set<petri_net_transition_id, hash_transition> transitions_to_fire{};

  for (auto* previous_event : local_configuration) {
    transitions_to_fire.insert(previous_event->reference_transition_);
  }

  // TODO (goulart.e) quadratic runtime! Use marking equation instead
  while (!transitions_to_fire.empty()) {
    for (const auto& transition : transitions_to_fire) {
      if (pn_accessor.is_transition_enabled(current_marking, transition)) {
        pn_accessor.fire_no_alloc(current_marking, transition);
        transitions_to_fire.erase(transition);
        break;
      }
    }
  }

  marking_ = current_marking;
}

unfolding_net::unfolding_net(const unfolding_representation& unf_repr, const input_output_mapper& io_mapper) {
  const auto& pn_repr{unf_repr.petri_net_repr};

  // Add events
  std::unordered_map<std::string, unfolding_event*> event_str_id_to_ptr{};
  for (const auto& [event_str_id, label] : pn_repr.transitions) {
    const auto& ref_transition_str_id{unf_repr.event_id_to_original_node_id.at(event_str_id)};
    const auto ref_transition_id{io_mapper.get_transition_for_str_id(ref_transition_str_id)};

    auto* event{create_event(ref_transition_id, {})};
    add_event(event);
    event_str_id_to_ptr.emplace(event_str_id, event);
  }

  // Add conditions
  std::unordered_map<std::string, unfolding_condition*> condition_str_id_to_ptr{};
  for (const auto& condition_str_id : pn_repr.places) {
    const auto& ref_place_str_id{unf_repr.condition_id_to_original_node_id.at(condition_str_id)};
    const auto ref_place_id{io_mapper.get_place_for_str_id(ref_place_str_id)};

    if (pn_repr.initial_marking.find(condition_str_id) != pn_repr.initial_marking.end()) {
      // Initial condition
      auto* initial_condition{add_initial_condition(ref_place_id)};
      condition_str_id_to_ptr.emplace(condition_str_id, initial_condition);
    } else {
      auto* condition{create_condition(ref_place_id, nullptr)};
      add_condition(condition);
      condition_str_id_to_ptr.emplace(condition_str_id, condition);
    }
  }

  // Add arcs
  for (const auto& [src_condition_str_id, tgt_event_str_id] : pn_repr.place_transition_arcs) {
    auto* condition{condition_str_id_to_ptr.at(src_condition_str_id)};
    auto* event{event_str_id_to_ptr.at(tgt_event_str_id)};
    condition->add_post_event(event);
    event->add_pre_condition(condition);
  }

  for (const auto& [src_event_str_id, tgt_condition_str_id] : pn_repr.transition_place_arcs) {
    auto* event{event_str_id_to_ptr.at(src_event_str_id)};
    auto* condition{condition_str_id_to_ptr.at(tgt_condition_str_id)};
    event->add_post_condition(condition);
    condition->set_pre_event(event);
  }
}

unfolding_condition* unfolding_net::add_initial_condition(petri_net_place_id reference_place) {
  auto* unfolding_condition_ptr{create_condition(reference_place, nullptr)};
  add_condition(unfolding_condition_ptr);
  initial_conditions_.insert(unfolding_condition_ptr);

  return unfolding_condition_ptr;
}

unfolding_condition* unfolding_net::create_condition(petri_net_place_id reference_place, unfolding_event* pre_event) {
  data_.unfolding_conditions.emplace_back(reference_place, pre_event);
  auto* condition_ptr{&data_.unfolding_conditions.back()};

  return condition_ptr;
}

void unfolding_net::add_condition(unfolding_condition* condition) {
  place_to_unfolding_conditions_.insert({condition->get_reference_place(), condition});
  unfolding_conditions_.push_back(condition);
}

unfolding_event* unfolding_net::create_event(petri_net_transition_id transition_id,
                                             std::vector<unfolding_condition*> pre_set) {
  data_.unfolding_events.emplace_back(transition_id, std::move(pre_set));

  auto* event_ptr{&data_.unfolding_events.back()};
  for (auto* condition : event_ptr->get_pre_conditions()) {
    condition->add_post_event(event_ptr);
  }

  return event_ptr;
}

void unfolding_net::add_event(unfolding_event* event) { unfolding_events_.push_back(event); }

const std::unordered_set<unfolding_condition*>& unfolding_net::get_initial_conditions() const {
  return initial_conditions_;
}

const std::vector<unfolding_event*>& unfolding_net::get_unfolding_events() const { return unfolding_events_; }

const std::vector<unfolding_condition*>& unfolding_net::get_unfolding_conditions() const {
  return unfolding_conditions_;
}

std::vector<unfolding_condition*> unfolding_net::conditions_for_place(petri_net_place_id place_id) const {
  std::vector<unfolding_condition*> ret{};

  for (auto [begin, end] = place_to_unfolding_conditions_.equal_range(place_id); begin != end; ++begin) {
    ret.push_back(begin->second);
  }

  return ret;
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::unfolding
