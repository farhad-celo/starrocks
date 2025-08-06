#include "fitting_prefix_aligner.h"

#include <optional>
#include <vector>

#include "modules/operators/process/petri_net/a_star/consistent_path_construction.h"
#include "modules/operators/process/petri_net/a_star/iterative_a_star.h"
#include "modules/operators/process/petri_net/a_star/synchronous_product.h"
#include "modules/operators/process/petri_net/petri_net.h"
#include "modules/operators/process/alignment/trace_alignment.h"

namespace celonis::accelerator::operators::process::alignment::fitting_prefix {

namespace {

struct fitting_prefix_heuristic {
  using transition_type = petri_net::a_star::synchronous_product::transition_type;
  using marking_type = petri_net::a_star::synchronous_product::marking_type;
  using cost_type = int32_t;

  [[nodiscard]] static cost_type get_weight(const transition_type& transition) {
    // We assign costs to make sure that we get the shortest path
    return transition.is_log_move() || transition.is_visible_model_move() ? 1 : 0;
  }
  [[nodiscard]] static std::optional<cost_type> estimate(const marking_type& /**marking*/) { return {0}; }

  [[nodiscard]] bool is_target(const marking_type& marking) const {
    return marking.variant_place == trace.size() && petri_net.is_final_marking(marking.petri_net_marking);
  }

  std::span<const petri_net::petri_net_accessor::label_type> trace;
  const petri_net::petri_net_accessor& petri_net;
};

struct fitting_prefix_path_construction
    : public petri_net::a_star::consistent_path_construction<petri_net::a_star::synchronous_product::marking_type,
                                                             petri_net::a_star::synchronous_product::transition_type,
                                                             fitting_prefix_heuristic::cost_type> {
  using transition_type = petri_net::a_star::synchronous_product::transition_type;
  using marking_type = petri_net::a_star::synchronous_product::marking_type;

  explicit fitting_prefix_path_construction(std::span<const petri_net::petri_net_accessor::label_type> trace,
                                            const common::execution_context& context)
      : petri_net::a_star::consistent_path_construction<petri_net::a_star::synchronous_product::marking_type,
                                                        petri_net::a_star::synchronous_product::transition_type,
                                                        fitting_prefix_heuristic::cost_type>(context),
        trace_{trace} {}

  [[nodiscard]] auto get_filter(const marking_type& marking) const {
    const bool trace_is_done{marking.variant_place == trace_.size()};

    return [trace_is_done](const auto& transition) {
      bool should_keep{trace_is_done || transition.is_synchronous_move() || transition.is_tau_move()};
      return should_keep;
    };
  }

  std::span<const petri_net::petri_net_accessor::label_type> trace_;
};

}  // namespace

fitting_prefix_aligner::fitting_prefix_aligner(int max_trailing_model_moves, int max_iterations,
                                               const common::execution_context& context)
    : max_trailing_model_moves_{max_trailing_model_moves}, max_iterations_{max_iterations}, context_{context} {}

trace_alignment_t fitting_prefix_aligner::operator()(const petri_net::petri_net_accessor& pn_accessor,
                                                     std::span<const row_id> variant) const {
  petri_net::a_star::synchronous_product synchronous_petri_net{pn_accessor, variant};

  auto result{petri_net::a_star::a_star_search(synchronous_petri_net, fitting_prefix_heuristic{variant, pn_accessor},
                                               fitting_prefix_path_construction{variant, context_},
                                               synchronous_petri_net.get_initial_marking(), max_trailing_model_moves_,
                                               max_iterations_)};
  using transitions_type = petri_net::a_star::synchronous_product::transition_list_type;

  if (!std::holds_alternative<transitions_type>(result)) {
    return {};
  }
  const auto& value = std::get<transitions_type>(result);
  trace_alignment alignment{};
  alignment.reserve(value.size());

  auto variant_it{std::cbegin(variant)};
  for (const auto& transition : value) {
    if (transition.is_log_move()) {
      alignment.add(alignment_move::log(*variant_it));
      ++variant_it;
    } else if (transition.is_synchronous_move()) {
      alignment.add(alignment_move::sync(*variant_it, *transition.petri_net_transition));
      ++variant_it;
    } else if (transition.is_model_move()) {
      const auto label{pn_accessor.get_label(*transition.petri_net_transition)};
      alignment.add(alignment_move::model(label, *transition.petri_net_transition));
    }
  }
  return std::optional{alignment};
}

}  // namespace celonis::accelerator::operators::process::alignment::fitting_prefix
