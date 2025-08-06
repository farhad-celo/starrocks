#include "modules/operators/process/petri_net/petri_net_conversion.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <fmt/format.h>

#include "modules/common/exceptions.h"
#include "modules/query/operators.pb.h"

namespace celonis::accelerator::operators::process::alignment::petri_net {

namespace {

std::unordered_set<std::string> validate_unique_places(const PetriNetDescription& petri_net_description,
                                                       const std::string& operator_name) {
  std::unordered_set<std::string> place_ids{};
  place_ids.reserve(petri_net_description.places_size());

  for (const auto& place_id : petri_net_description.places()) {
    if (!place_ids.emplace(place_id).second) {
      throw common::cpm_exception{"{}: Place ID [{}] used multiple times. Place IDs must be unique.", operator_name,
                                  place_id};
    }
  }

  return place_ids;
}

std::unordered_set<std::string> validate_unique_transitions(const PetriNetDescription& petri_net_description,
                                                            const std::unordered_set<std::string>& place_ids,
                                                            const std::string& operator_name) {
  std::unordered_set<std::string> transition_ids{};
  transition_ids.reserve(petri_net_description.transitions_size());

  for (const auto& transition_id : petri_net_description.transitions()) {
    if (transition_ids.count(transition_id) != 0) {
      throw common::cpm_exception{"{}: Transition ID [{}] used multiple times. Transition IDs must be unique.",
                                  operator_name, transition_id};
    }
    if (place_ids.count(transition_id) != 0) {
      throw common::cpm_exception{"{}: Found Transition and Place with the same ID [{}]. Node IDs must be unique.",
                                  operator_name, transition_id};
    }
    transition_ids.emplace(transition_id);
  }

  return transition_ids;
}

// TODO (goulart.e) verify that arcs with 2 non-existing nodes is not allowed
void validate_arcs_bipartite_and_unique_nodes(const PetriNetDescription& petri_net_description,
                                              const std::unordered_set<std::string>& place_ids,
                                              const std::unordered_set<std::string>& transition_ids,
                                              const std::string& operator_name) {
  for (const auto& arc : petri_net_description.arcs()) {
    const auto from_is_place{(place_ids.count(arc.from()) != 0)};
    const auto from_is_transition{(transition_ids.count(arc.from()) != 0)};
    const auto to_is_place{(place_ids.count(arc.to()) != 0)};
    const auto to_is_transition{(transition_ids.count(arc.to()) != 0)};

    if (!from_is_place && !from_is_transition) {
      throw common::cpm_exception{"{}: Node [{}] of edge ([{}], [{}]) does not correspond to any Place or Transition.",
                                  operator_name, arc.from(), arc.from(), arc.to()};
    }
    if (!to_is_place && !to_is_transition) {
      throw common::cpm_exception{"{}: Node [{}] of edge ([{}], [{}]) does not correspond to any Place or Transition.",
                                  operator_name, arc.to(), arc.from(), arc.to()};
    }
    if (from_is_place && to_is_place) {
      // Place->Place edge not allowed
      throw common::cpm_exception{
          "{}: Place->Place edge ([{}], [{}]) is not allowed. Petri net graph must be bipartite.", operator_name,
          arc.from(), arc.to()};
    }

    if (from_is_transition && to_is_transition) {
      // Transition->Transition edge not allowed
      throw common::cpm_exception{
          "{}: Transition->Transition edge ([{}], [{}]) is not allowed. Petri net graph must be bipartite.",
          operator_name, arc.from(), arc.to()};
    }
  }
}

}  // namespace

std::optional<std::string> get_duplicated_activities_string(const PetriNetDescription& petri_net_description) {
  std::unordered_map<std::string, int> freq_map{};
  freq_map.reserve(petri_net_description.mapping_size());

  for (const auto& curr_pair : petri_net_description.mapping()) {
    freq_map[curr_pair.from()]++;
  }

  std::string duplicates{};
  for (const auto& curr_pair : freq_map) {
    if (curr_pair.second > 1) {
      duplicates.append(fmt::format("[{}] occurs {} times, ", curr_pair.first, curr_pair.second));
    }
  }

  // remove the unnecessary ", " at the end
  if (duplicates.empty()) {
    return std::nullopt;
  }

  duplicates.pop_back();
  duplicates.pop_back();

  return duplicates;
}

void check_is_valid_petri_net(const PetriNetDescription& petri_net_description, const std::string& operator_name) {
  const auto place_ids{validate_unique_places(petri_net_description, operator_name)};
  const auto transition_ids{validate_unique_transitions(petri_net_description, place_ids, operator_name)};
  validate_arcs_bipartite_and_unique_nodes(petri_net_description, place_ids, transition_ids, operator_name);

  std::unordered_set<std::string> seen_transition_labels{};
  seen_transition_labels.reserve(petri_net_description.mapping_size());
  for (const auto& mapping : petri_net_description.mapping()) {
    if (transition_ids.count(mapping.to()) == 0) {
      throw common::cpm_exception{"{}: Mapping ([{}], [{}]) maps Label to inexistent Transition [{}].", operator_name,
                                  mapping.from(), mapping.to(), mapping.to()};
    }

    if (seen_transition_labels.find(mapping.to()) != seen_transition_labels.end()) {
      throw common::cpm_exception{"{}: More than 2 labels mapped to transition [{}].", operator_name, mapping.to()};
    }
    seen_transition_labels.emplace(mapping.to());
  }

  for (const auto& initial_place_id : petri_net_description.initial_marking()) {
    const auto& node_id{initial_place_id.node()};
    if (place_ids.find(node_id) == place_ids.end()) {
      throw common::cpm_exception{"{}: Place [{}] of initial marking does not correspond to any place id.",
                                  operator_name, node_id};
    }
  }

  for (const auto& final_place_id : petri_net_description.final_marking()) {
    const auto& node_id{final_place_id.node()};
    if (place_ids.find(node_id) == place_ids.end()) {
      throw common::cpm_exception{"{}: Place [{}] of final marking does not correspond to any place id.", operator_name,
                                  node_id};
    }
  }
}

void check_is_workflow_net(const PetriNetDescription& petri_net_description, const std::string& operator_name) {
  // Check if we have a workflow net
  if (petri_net_description.initial_marking_size() != 1 || petri_net_description.initial_marking().at(0).count() != 1) {
    throw common::cpm_exception{
        "{}: Petri net must be a Workflow Net "
        "(i.e. initial marking should consist of one place with one token only).",
        operator_name};
  }

  if (petri_net_description.final_marking_size() != 1 || petri_net_description.final_marking().at(0).count() != 1) {
    throw common::cpm_exception{
        "{}: Petri net must be a Workflow Net "
        "(i.e. final marking should consist of one place with one token only).",
        operator_name};
  }
}

petri_net_representation get_pn_repr_from_operator_input(const PetriNetDescription& petri_net_description,
                                                         string_to_int_mapper& str_mapper) {
  petri_net_representation pn_repr{};

  pn_repr.places.insert(std::cbegin(petri_net_description.places()), std::cend(petri_net_description.places()));

  for (const auto& mapping : petri_net_description.mapping()) {
    const auto& transition_label{mapping.from()};
    const auto& transition_id{mapping.to()};
    const auto label_id{str_mapper.get_label_id(transition_label)};
    pn_repr.transitions.emplace(transition_id, label_id);
  }

  for (const auto& transition_id : petri_net_description.transitions()) {
    pn_repr.transitions.try_emplace(transition_id, str_mapper.get_tau_transition_id());
  }

  for (const auto& marking : petri_net_description.initial_marking()) {
    pn_repr.initial_marking.emplace(marking.node());
  }

  for (const auto& marking : petri_net_description.final_marking()) {
    pn_repr.final_marking.emplace(marking.node());
  }

  for (const auto& arc : petri_net_description.arcs()) {
    const auto& from_node{arc.from()};
    const auto& to_node{arc.to()};

    if (pn_repr.places.find(from_node) != pn_repr.places.end()) {
      // Place -> Transition
      pn_repr.place_transition_arcs.emplace(from_node, to_node);
    } else {
      // Transition -> Place
      pn_repr.transition_place_arcs.emplace(from_node, to_node);
    }
  }

  return pn_repr;
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
