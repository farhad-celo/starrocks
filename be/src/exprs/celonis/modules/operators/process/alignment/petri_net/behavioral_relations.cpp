#include "behavioral_relations.h"

#include "legacy_embedded_ctl/assert.h"
#include "modules/operators/process/alignment/petri_net/petri_net.h"
#include "modules/operators/process/alignment/petri_net/petri_net_entities.h"
#include "modules/operators/process/alignment/petri_net/transition_distances.h"

namespace celonis::accelerator::operators::process::alignment::petri_net {

behavioral_relations_matrix::behavioral_relations_matrix(size_t number_of_transitions)
    : behavioral_relations_{number_of_transitions * number_of_transitions},
      number_of_transitions_{number_of_transitions} {}

void behavioral_relations_matrix::set_relation(petri_net_transition_id transition_i,
                                               petri_net_transition_id transition_j, behavioral_relation relation) {
  const auto index{number_of_transitions_ * transition_i.id + transition_j.id};
  behavioral_relations_.at(index) = relation;
}

behavioral_relation behavioral_relations_matrix::get_relation(petri_net_transition_id transition_i,
                                                              petri_net_transition_id transition_j) const {
  const auto index{number_of_transitions_ * transition_i.id + transition_j.id};
  return behavioral_relations_.at(index);
}

namespace {

void initialize_from_distance_matrix(behavioral_relations_matrix& behavioral_relations,
                                     const petri_net_accessor& pn_accessor,
                                     const transition_distances_matrix& transition_distances) {
  for (const auto& pn_transition_i : pn_accessor.get_transitions()) {
    for (const auto& pn_transition_j : pn_accessor.get_transitions()) {
      const auto& transition_i{pn_transition_i.transition};
      const auto& transition_j{pn_transition_j.transition};

      if (transition_i.is_null() || transition_j.is_null()) {
        continue;
      }

      const auto& distance_i_j{transition_distances.get_distance(transition_i, transition_j)};
      const auto& distance_j_i{transition_distances.get_distance(transition_j, transition_i)};
      const bool exists_path_i_j{(distance_i_j != -1)};
      const bool exists_path_j_i{(distance_j_i != -1)};

      // Didn't cut the loop by half because this would generate a very cache unfriendly access pattern,
      //  but can write a branchless version of this here. Reordering IFs won't work because each PN has a very
      //  unique behavioral profiles
      if (exists_path_i_j && exists_path_j_i) {
        behavioral_relations.set_relation(transition_i, transition_j, behavioral_relation::INTERLEAVED);
      } else if (exists_path_i_j && !exists_path_j_i) {
        behavioral_relations.set_relation(transition_i, transition_j, behavioral_relation::PRECEDES);
      } else if (exists_path_j_i && !exists_path_i_j) {
        behavioral_relations.set_relation(transition_i, transition_j, behavioral_relation::FOLLOWS);
      } else {
        behavioral_relations.set_relation(transition_i, transition_j, behavioral_relation::EXCLUSIVE);
      }
    }
  }
}

/** Returns all transitions within branch of parallel section */
std::unordered_set<petri_net_transition_id, hash_transition> get_branch_transitions(
    const petri_net_accessor& pn_accessor, petri_net_place_id starting_place, petri_net_transition_id join_transition) {
  // One branch for each place
  std::unordered_set<petri_net_transition_id, hash_transition> seen{};
  // std::queue would be a natural candidate here, but it allocates too much memory
  std::vector<petri_net_transition_id> to_expand{};
  for (const auto& out_transition : pn_accessor[starting_place].out_transitions) {
    if (out_transition != join_transition) {
      to_expand.emplace_back(out_transition);
    }
  }

  size_t cur_index{0};
  while (cur_index < to_expand.size()) {
    const auto& transition_to_expand{to_expand[cur_index]};
    ++cur_index;
    if (seen.find(transition_to_expand) != seen.end()) {
      continue;
    }
    seen.insert(transition_to_expand);

    for (const auto& out_place : pn_accessor[transition_to_expand].out_places) {
      for (const auto& out_transition : pn_accessor[out_place].out_transitions) {
        if (seen.find(out_transition) == seen.end() && out_transition.id != join_transition.id) {
          to_expand.push_back(out_transition);
        }
      }
    }
  }

  return seen;
}

void set_parallel_sections_to_interleaved(behavioral_relations_matrix& behavioral_relations,
                                          const petri_net_accessor_with_parallel_sections& pn) {
  for (const auto& [fork, join] : pn.par_sections().sections()) {
    // If fork and join are interleaved, then all transitions inside parallel section
    // are already interleaved and there's no need to fixup this section
    if (behavioral_relations.get_relation(fork, join) == behavioral_relation::INTERLEAVED) {
      continue;
    }

    std::vector<std::unordered_set<petri_net_transition_id, hash_transition>> transitions_per_branch{};

    for (const auto& place : pn.accessor()[fork].out_places) {
      auto branch_transitions{get_branch_transitions(pn.accessor(), place, join)};
      transitions_per_branch.emplace_back(std::move(branch_transitions));
    }

    // Now for each pair of branches set transitions in branch to interleaved
    for (size_t i{0}; i < transitions_per_branch.size(); ++i) {
      for (size_t j{i + 1}; j < transitions_per_branch.size(); ++j) {
        const auto& branch_i{transitions_per_branch[i]};
        const auto& branch_j{transitions_per_branch[j]};
        for (const auto& transition_i : branch_i) {
          // Branches can join before end of parallel section.
          // To handle it, check if transition_i belongs to both branches
          //  If yes, then it happens after branches were joined and therefore it's not interleaved
          if (branch_j.find(transition_i) != branch_j.end()) {
            continue;
          }

          for (const auto& transition_j : branch_j) {
            // Same check as before, but check if Tj belongs to branchI
            if (branch_i.find(transition_j) != branch_i.end()) {
              continue;
            }

            legacy_embedded_debug_assert(
                behavioral_relations.get_relation(transition_i, transition_j) == behavioral_relation::INTERLEAVED ||
                behavioral_relations.get_relation(transition_i, transition_j) == behavioral_relation::EXCLUSIVE);
            behavioral_relations.set_relation(transition_i, transition_j, behavioral_relation::INTERLEAVED);
            behavioral_relations.set_relation(transition_j, transition_i, behavioral_relation::INTERLEAVED);
          }
        }
      }
    }
  }
}

}  // namespace

behavioral_relations_matrix compute_behavioral_relations(const petri_net_accessor_with_parallel_sections& pn,
                                                         const transition_distances_matrix& transition_distances) {
  behavioral_relations_matrix behavioral_relations{pn.accessor().get_max_transition_id()};

  initialize_from_distance_matrix(behavioral_relations, pn.accessor(), transition_distances);
  set_parallel_sections_to_interleaved(behavioral_relations, pn);

  return behavioral_relations;
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net
