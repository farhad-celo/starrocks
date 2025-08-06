#include "bpmn_to_pn.h"

#include <iterator>
#include <variant>

#include <boost/range/iterator_range_core.hpp>

#include <ctl/algorithm.h>
#include <cpml/constants.h>
#include <cpml/model/bpmn/vertex.h>
#include <cpml/model/bpmn/vertex_types.h>
#include <cpml/model/bpmn_graph.h>
#include <cpml/model/petri_net.h>
#include <cpml/model/pn/murata_reductions.h>

#include "modules/common/exceptions.h"
#include "modules/memory/row_id.h"
#include "modules/operators/process/petri_net/petri_net.h"

namespace celonis::accelerator::operators::process::bpmn {

namespace {

[[nodiscard]] alignment::petri_net::petri_net_representation to_pn_repr(const cpml::model::petri_net& pn) {
  return alignment::petri_net::petri_net_representation{
    .places = ctl::transform_to<std::unordered_set<std::string>>(
        pn.places(), [](const auto& place_id_to_place) { return place_id_to_place.first.get(); }),
    .transitions = ctl::transform_to<std::unordered_map<std::string, cpml::activity_id_t>>(
        pn.transitions(),
        [](const auto& transition_id_to_transition) {
          return std::make_pair(transition_id_to_transition.first.get(), transition_id_to_transition.second.label());
        }),
    .place_transition_arcs = ctl::transform_to<std::unordered_multimap<std::string, std::string>>(
        pn.place_to_transition_arcs(),
        [](const auto& p2t_arc) { return std::make_pair(p2t_arc.from_id().get(), p2t_arc.to_id().get()); }),
    .transition_place_arcs = ctl::transform_to<std::unordered_multimap<std::string, std::string>>(
        pn.transition_to_place_arcs(),
        [](const auto& t2p_arc) { return std::make_pair(t2p_arc.from_id().get(), t2p_arc.to_id().get()); }),
    .initial_marking = ctl::transform_to<std::unordered_set<std::string>>(
        pn.initial_marking(), [](const auto& place_id) { return place_id.get(); }),
    .final_marking = ctl::transform_to<std::unordered_set<std::string>>(
        pn.final_marking(), [](const auto& place_id) { return place_id.get(); })};
}

[[nodiscard]] cpml::model::petri_net to_cpml_pn(const alignment::petri_net::petri_net_representation& pn_repr) {
  cpml::model::petri_net::builder pn_bldr{};

  std::ranges::for_each(pn_repr.places, [&pn_bldr](const auto& raw_place_id) { pn_bldr.add_place(raw_place_id); });

  std::ranges::for_each(pn_repr.transitions, [&pn_bldr](const auto& raw_transition_id_and_label) {
    pn_bldr.add_transition(raw_transition_id_and_label.first, raw_transition_id_and_label.second);
  });

  std::ranges::for_each(pn_repr.place_transition_arcs, [&pn_bldr](const auto& raw_p2t_arc) {
    pn_bldr.add_p2t_arc(raw_p2t_arc.first, raw_p2t_arc.second);
  });

  std::ranges::for_each(pn_repr.transition_place_arcs, [&pn_bldr](const auto& raw_t2p_arc) {
    pn_bldr.add_t2p_arc(raw_t2p_arc.first, raw_t2p_arc.second);
  });

  std::ranges::for_each(pn_repr.initial_marking,
                        [&pn_bldr](const auto& raw_place_id) { pn_bldr.add_to_initial_marking(raw_place_id); });

  std::ranges::for_each(pn_repr.final_marking,
                        [&pn_bldr](const auto& raw_place_id) { pn_bldr.add_to_final_marking(raw_place_id); });

  return std::move(pn_bldr).build();
}

// TODO(n.weber): Temporary using decls until code is migrated to CPML
using cpml::model::bpmn_graph;
using cpml::model::bpmn::vertex;
using cpml::model::bpmn::vertex_id_type;

[[nodiscard]] std::string transition_id_string(vertex_id_type vertex_id) { return fmt::format("t{}", vertex_id); }

[[nodiscard]] std::string parallel_place_id_string(vertex_id_type source_id, vertex_id_type target_id) {
  return fmt::format("p{}_in_{}_out", source_id, target_id);
}

[[nodiscard]] std::string non_parallel_source_place_id_string(vertex_id_type source_id) {
  return fmt::format("p{}_out", source_id);
}

std::string get_source_place_id_string(const vertex& source, const vertex& target) {
  if (is_parallel(source)) {
    return parallel_place_id_string(source.get_vertex_id(), target.get_vertex_id());
  }
  return non_parallel_source_place_id_string(source.get_vertex_id());
}

[[nodiscard]] std::string non_parallel_target_place_id_string(vertex_id_type target_id) {
  return fmt::format("p{}_in", target_id);
}

[[nodiscard]] std::string get_target_place_id_string(const vertex& source, const vertex& target) {
  if (is_parallel(target)) {
    return parallel_place_id_string(source.get_vertex_id(), target.get_vertex_id());
  }
  return non_parallel_target_place_id_string(target.get_vertex_id());
}

template <typename MULTIMAP, typename KEY, typename VAL>
void emplace_if_not_present_yet(MULTIMAP& map, KEY&& key, VAL&& val) {
  using value_type = typename MULTIMAP::value_type;
  const auto equal_range{boost::make_iterator_range(map.equal_range(key))};
  if (std::ranges::empty(equal_range)) {
    map.emplace(std::forward<KEY>(key), std::forward<VAL>(val));
  } else if (const auto it{std::ranges::find(equal_range, val, &value_type::second)};
             it == std::ranges::end(equal_range)) {
    map.emplace_hint(it, std::forward<KEY>(key), std::forward<VAL>(val));
  }
}

std::string get_source_place_id(const vertex& source, const vertex& target,
                                alignment::petri_net::petri_net_representation& petri_net) {
  const auto [place_it, place_success]{petri_net.places.emplace(get_source_place_id_string(source, target))};
  const auto [transition_it, transition_success]{
      petri_net.transitions.try_emplace(transition_id_string(source.get_vertex_id()), source.get_vertex_id())};
  emplace_if_not_present_yet(petri_net.transition_place_arcs, transition_it->first, *place_it);
  return *place_it;
}

std::string get_target_place_id(const vertex& source, const vertex& target,
                                alignment::petri_net::petri_net_representation& petri_net) {
  const auto [place_it, place_success]{petri_net.places.emplace(get_target_place_id_string(source, target))};
  const auto [transition_it, transition_success]{
      petri_net.transitions.try_emplace(transition_id_string(target.get_vertex_id()), target.get_vertex_id())};
  emplace_if_not_present_yet(petri_net.place_transition_arcs, *place_it, transition_it->first);
  return *place_it;
}

void add_edge(const vertex& source, const vertex& target, alignment::petri_net::petri_net_representation& petri_net) {
  const auto source_place_id{get_source_place_id(source, target, petri_net)};
  const auto target_place_id{get_target_place_id(source, target, petri_net)};
  if (source_place_id != target_place_id) {
    static constexpr auto tau_label{cpml::TAU_ID};
    const auto [transition_it, success]{
        petri_net.transitions.try_emplace(fmt::format("t{}_to_{}", source_place_id, target_place_id), tau_label)};
    debug_assert(success);
    petri_net.place_transition_arcs.emplace(source_place_id, transition_it->first);
    petri_net.transition_place_arcs.emplace(transition_it->first, target_place_id);
  }
}

}  // namespace

alignment::petri_net::petri_net_representation get_pn(const bpmn_graph& bpmn_model) {
  if (!bpmn_model.is_single_object()) {
    throw common::cpm_exception{"BPMN to Petri net conversion only supports single object BPMN graphs"};
  }
  alignment::petri_net::petri_net_representation pn{};
  for (const auto& edge : bpmn_model.get_edges()) {
    add_edge(bpmn_model.get_vertex(edge.get_source_id()), bpmn_model.get_vertex(edge.get_target_id()), pn);
  }
  // handle start/end BPMN vertices
  const auto start_vertex_id{bpmn_model.single_start_vertex()};
  const auto start_place_id{non_parallel_target_place_id_string(start_vertex_id)};
  debug_assert(!pn.places.contains(start_place_id));
  pn.places.emplace(start_place_id);
  pn.place_transition_arcs.emplace(start_place_id, transition_id_string(start_vertex_id));
  pn.initial_marking.emplace(start_place_id);

  const auto end_vertex_id{bpmn_model.single_end_vertex()};
  const auto end_place_id{non_parallel_source_place_id_string(end_vertex_id)};
  debug_assert(!pn.places.contains(end_place_id));
  pn.places.emplace(end_place_id);
  pn.transition_place_arcs.emplace(transition_id_string(end_vertex_id), end_place_id);
  pn.final_marking.emplace(end_place_id);
  return pn;
}

template <filter_out_bpmn_edge_transitions DO_FILTER_OUT_BPMN_EDGE_TRANSITIONS>
bpmn_to_petri_net_result_t bpmn_to_petri_net_with_mapping(const cpml::model::bpmn_graph& graph) {
  // NB we assume here that there is a one-to-one mapping from petri net transitions to BPMN vertices
  auto petri_net{get_pn(graph)};
  petri_net_str_id_to_bpmn_mapping pn_str_id_to_bpmn{};
  label_to_bpmn_mapping log_label_to_bpmn{};
  // get_pn(graph) returns a Petri net where the labeled transitions' label ids are the BPMN vertex ids.
  // However, the alignment expects the Petri net transitions' label ids to refer to the corresponding activity column
  // id if they refer to a task (that is present in the activity column), and tau label if not.
  // That means that we need to remap ids from task nodes to their activity ids and from other nodes to tau.
  for (auto& [transition_str_id, vertex_ref] : petri_net.transitions) {
    if (vertex_ref == cpml::TAU_ID) {
      continue;
    }

    const auto& corresponding_vertex{graph.get_vertex(vertex_ref)};
    const auto label_to_be{std::visit(
        ctl::overloaded{[](const cpml::model::bpmn::task& t) -> cpml::activity_id_t { return t.activity_id; },
                        [](const auto& /**/) { return cpml::TAU_ID; }},
        corresponding_vertex.get_vertex_type())};
    pn_str_id_to_bpmn.try_emplace(transition_str_id, vertex_ref);
    vertex_ref = label_to_be;
  }

  // This can also be extracted somewhere else, but we don't bother for now
  for (const auto& [vertex_id, vertex] : graph.get_vertices()) {
    if (is_task(vertex)) {
      const auto activity_id{std::get<cpml::model::bpmn::task>(vertex.get_vertex_type()).activity_id};
      if (activity_id != VALUE_NOT_FOUND) {
        common::runtime_assert(
            !log_label_to_bpmn.contains(activity_id),
            "Input BPMN model has duplicate tasks. This messes up with the vertex id assignment for log moves.");
        log_label_to_bpmn.emplace(activity_id, vertex_id);
      }
    }
  }

  if constexpr (ctl::as_bool(DO_FILTER_OUT_BPMN_EDGE_TRANSITIONS)) {
    auto cpml_pn{to_cpml_pn(petri_net)};

    cpml_pn =
        cpml::model::pn::reduce_murata(cpml_pn, [&pn_str_id_to_bpmn](const cpml::model::pn::transition& transition) {
          // We allow to reduce transitions which do not correspond to a vertex
          const auto& transition_str_id{transition.id().get()};
          return !pn_str_id_to_bpmn.contains(transition_str_id);
        });

    petri_net = to_pn_repr(cpml_pn);
  }

  return {.petri_net = std::move(petri_net),
          .pn_str_id_to_bpmn = std::move(pn_str_id_to_bpmn),
          .log_label_to_bpmn = std::move(log_label_to_bpmn)};
}

template <filter_out_bpmn_edge_transitions DO_FILTER_OUT_BPMN_EDGE_TRANSITIONS>
alignment::petri_net::petri_net_representation bpmn_to_petri_net(const cpml::model::bpmn_graph& graph) {
  return bpmn_to_petri_net_with_mapping<DO_FILTER_OUT_BPMN_EDGE_TRANSITIONS>(graph).petri_net;
}

template bpmn_to_petri_net_result_t bpmn_to_petri_net_with_mapping<filter_out_bpmn_edge_transitions::YES>(
    const cpml::model::bpmn_graph&);
template bpmn_to_petri_net_result_t bpmn_to_petri_net_with_mapping<filter_out_bpmn_edge_transitions::NO>(
    const cpml::model::bpmn_graph&);
template alignment::petri_net::petri_net_representation bpmn_to_petri_net<filter_out_bpmn_edge_transitions::YES>(
    const cpml::model::bpmn_graph&);
template alignment::petri_net::petri_net_representation bpmn_to_petri_net<filter_out_bpmn_edge_transitions::NO>(
    const cpml::model::bpmn_graph&);

}  // namespace celonis::accelerator::operators::process::bpmn
