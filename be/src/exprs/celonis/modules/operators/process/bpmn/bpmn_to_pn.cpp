#include "bpmn_to_pn.h"

#include <iterator>
#include <variant>

#include <boost/range/iterator_range_core.hpp>

#include "modules/common/exceptions.h"
#include "modules/memory/row_id.h"
#include "modules/operators/process/alignment/petri_net/petri_net.h"
#include "modules/operators/process/bpmn/bpmn_graph.h"
#include "modules/operators/process/bpmn/vertex.h"
#include "modules/operators/process/bpmn/vertex_types.h"

namespace celonis::accelerator::operators::process::bpmn {

namespace {

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
    static constexpr auto tau_label{alignment::string_to_int_mapper::get_tau_transition_id()};
    const auto [transition_it, success]{
        petri_net.transitions.try_emplace(fmt::format("t{}_to_{}", source_place_id, target_place_id), tau_label)};
    legacy_embedded_debug_assert(success);
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
  legacy_embedded_debug_assert(!pn.places.contains(start_place_id));
  pn.places.emplace(start_place_id);
  pn.place_transition_arcs.emplace(start_place_id, transition_id_string(start_vertex_id));
  pn.initial_marking.emplace(start_place_id);

  const auto end_vertex_id{bpmn_model.single_end_vertex()};
  const auto end_place_id{non_parallel_source_place_id_string(end_vertex_id)};
  legacy_embedded_debug_assert(!pn.places.contains(end_place_id));
  pn.places.emplace(end_place_id);
  pn.transition_place_arcs.emplace(transition_id_string(end_vertex_id), end_place_id);
  pn.final_marking.emplace(end_place_id);
  return pn;
}

}  // namespace celonis::accelerator::operators::process::bpmn
