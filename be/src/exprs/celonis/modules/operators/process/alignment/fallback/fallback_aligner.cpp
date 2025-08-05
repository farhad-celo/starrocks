#include "fallback_aligner.h"

#include "modules/operators/process/alignment/petri_net/a_star/inconsistent_path_construction.h"
#include "modules/operators/process/alignment/petri_net/a_star/iterative_a_star.h"
#include "modules/operators/process/alignment/petri_net/a_star/shortest_path_heuristic.h"

namespace celonis::accelerator::operators::process::alignment::fallback {

std::optional<sequence_aligner::sequence_type> get_shortest_pn_trace(const petri_net::petri_net_accessor& pn_accessor,
                                                                     const petri_net_information& pn_information,
                                                                     int max_num_iterations,
                                                                     std::string_view operator_name,
                                                                     const common::execution_context& context) {
  legacy_embedded_debug_assert(!pn_accessor.get_final_markings().empty());

  const auto baseline{petri_net::a_star::a_star_search(
      pn_accessor,
      petri_net::a_star::heuristic_to_markings{pn_accessor.get_final_markings(), pn_information.transition_distances_tt,
                                               pn_accessor},
      petri_net::a_star::inconsistent_path_construction<petri_net::marking_type, petri_net::petri_net_transition_id,
                                                        petri_net::a_star::heuristic_to_markings::cost_type,
                                                        boost::hash<petri_net::marking_type>>{context},
      pn_accessor.get_initial_marking(),
      std::numeric_limits<petri_net::a_star::heuristic_to_markings::cost_type>::max(), max_num_iterations)};

  using nothing_found = petri_net::a_star::nothing_found;
  if (std::holds_alternative<nothing_found>(baseline)) {
    if (std::get<nothing_found>(baseline) == nothing_found::AT_ALL) {
      throw common::cpm_exception{"{}: The start and end places in the Petri net seem to be disconnected.",
                                  operator_name};
    }
    return std::nullopt;
  }

  using transition_list_type = petri_net::petri_net_accessor::transition_list_type;
  const auto& transitions{std::get<transition_list_type>(baseline)};
  sequence_aligner::sequence_type shortest_run{};
  shortest_run.reserve(transitions.size());
  std::ranges::transform(transitions, std::back_inserter(shortest_run),
                         [&pn_accessor](const auto& t) { return std::make_pair<>(t, pn_accessor.get_label(t)); });
  return shortest_run;
}

fallback_aligner::fallback_aligner(const petri_net::petri_net_accessor& pn_accessor,
                                   const petri_net_information& pn_information, int max_shortest_trace_iterations,
                                   int max_a_star_iterations, std::string_view operator_name,
                                   common::execution_context& context) {
  const auto shortest_run{
      get_shortest_pn_trace(pn_accessor, pn_information, max_shortest_trace_iterations, operator_name, context)};
  if (shortest_run.has_value()) {
    impl_ = sequence_aligner{shortest_run.value(), max_a_star_iterations};
  } else {
    context.add_warning(
        fmt::format("{}: Petri net is very complex. Some alignments might not be computed.", operator_name));
  }
}

trace_alignment_t fallback_aligner::operator()(std::span<const row_id> variant,
                                               const common::execution_context& context) const {
  if (impl_.has_value()) {
    return impl_.value()(variant, context);
  }
  return std::nullopt;
}

}  // namespace celonis::accelerator::operators::process::alignment::fallback
