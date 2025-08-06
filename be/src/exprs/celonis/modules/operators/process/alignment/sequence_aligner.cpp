#include "sequence_aligner.h"

#include <boost/functional/hash.hpp>

#include "legacy_embedded_ctl/conversion.h"
#include "log/log.h"
#include "modules/operators/process/petri_net/a_star/consistent_path_construction.h"
#include "modules/operators/process/petri_net/a_star/iterative_a_star.h"
#include "modules/operators/process/petri_net/petri_net.h"

namespace celonis::accelerator::operators::process::alignment {

namespace {

struct sequence_product final {
  struct transition_type {
    bool move_first{false};
    bool move_second{false};
    bool is_tau{false};
    // NOLINTNEXTLINE(modernize-use-nullptr,readability-implicit-bool-conversion)
    auto operator<=>(const transition_type& rhs) const = default;
  };

  struct marking_type final {
    size_t first_index{0};
    size_t second_index{0};
    bool operator==(const marking_type& rhs) const = default;
  };

  struct marking_hash_type {
    size_t operator()(const marking_type& marking) const {
      auto result{marking.first_index};
      boost::hash_combine(result, marking.second_index);
      return result;
    }
  };
  using transition_list_type = petri_net::transitions_container_t<transition_type>;

  [[nodiscard]] transition_list_type get_enabled_transitions(const marking_type& marking) const {
    transition_list_type result{};
    if (marking.first_index < first.size()) {
      result.emplace_back(transition_type{true, false, false});
      if (marking.second_index < second.size() && first[marking.first_index] == second[marking.second_index].second) {
        result.emplace_back(transition_type{true, true, false});
      }
    }
    if (marking.second_index < second.size()) {
      result.emplace_back(transition_type{false, true, false});
    }
    return result;
  }

  [[nodiscard]] static marking_type fire(const marking_type& marking, transition_type transition) {
    return {marking.first_index + static_cast<size_t>(transition.move_first),
            marking.second_index + static_cast<size_t>(transition.move_second)};
  }

  [[nodiscard]] static marking_type fire_inverse(const marking_type& marking, transition_type transition) {
    return {marking.first_index - static_cast<size_t>(transition.move_first),
            marking.second_index - static_cast<size_t>(transition.move_second)};
  }

  [[nodiscard]] static marking_type get_initial_marking() { return {}; }

  std::span<const sequence_aligner::visible_label_type> first;
  std::span<const std::pair<petri_net::petri_net_transition_id, sequence_aligner::visible_label_type>> second;
};

struct sequence_product_heuristic {
  using transition_type = sequence_product::transition_type;
  using marking_type = sequence_product::marking_type;
  using cost_type = int32_t;

  [[nodiscard]] static cost_type get_weight(transition_type t) {
    return t.is_tau || (t.move_first == t.move_second) ? 0 : 1;  // "null transition" (move neither) also has weight 0
  }

  [[nodiscard]] std::optional<cost_type> estimate(const marking_type& marking) const {
    if (marking.first_index < petri_net.first.size() && marking.second_index < petri_net.second.size()) {
      if (petri_net.second[marking.second_index].second == string_to_int_mapper::get_tau_transition_id()) {
        return {0};
      }
      return petri_net.first[marking.first_index] != petri_net.second[marking.second_index].second;
    }

    const auto remaining_first{petri_net.first.size() - marking.first_index};
    const auto remaining_second{petri_net.second.size() - marking.second_index};
    return {remaining_first > remaining_second ? remaining_first - remaining_second : 0};
  }

  [[nodiscard]] bool is_target(const marking_type& marking) const {
    return marking.first_index == petri_net.first.size() && marking.second_index == petri_net.second.size();
  }

  sequence_product& petri_net;
};

struct sequence_product_path_construction
    : public petri_net::a_star::consistent_path_construction<
          sequence_product::marking_type, sequence_product::transition_type, sequence_product_heuristic::cost_type,
          sequence_product::marking_hash_type> {
  using marking_type = sequence_product::marking_type;

  explicit sequence_product_path_construction(const common::execution_context& context)
      : petri_net::a_star::consistent_path_construction<
            sequence_product::marking_type, sequence_product::transition_type, sequence_product_heuristic::cost_type,
            sequence_product::marking_hash_type>{context} {}

  // If we have successive model and log moves, all combinations of these (e.g., LLM, LML, and MLL) are equivalent.
  // We only need to explore one of these paths. Here, we use the convention that we only explore paths where all
  // consecutive log moves come before all consecutive model moves.
  // In other words, filter out paths with a model move followed by a log move.
  [[nodiscard]] auto get_filter(const marking_type& marking) const {
    // look up transition that got us here
    const auto it{get_partial_paths().find(marking)};
    auto previously_model_move{false};
    if (it == end(get_partial_paths())) {
      log::jdebug("A* search on synchronous product: Trying to create filter from previously unseen marking");
    } else {
      previously_model_move = !it->second.move_first && it->second.move_second;
    }
    return [previously_model_move](const auto& transition) {
      const auto is_log_move{transition.move_first && !transition.move_second};
      return !(previously_model_move && is_log_move);
    };
  }
};

}  // namespace

trace_alignment_t sequence_aligner::align_sequence_to_run(std::span<const row_id> trace, const sequence_type& model_run,
                                                          int max_iterations,
                                                          const common::execution_context& context) {
  sequence_product petri_net{trace, model_run};
  // search for shortest path
  auto path{petri_net::a_star::a_star_search(  // we could use a more specialized implementation of A*
      petri_net, sequence_product_heuristic{petri_net}, sequence_product_path_construction{context},
      sequence_product::get_initial_marking(), legacy_embedded_ctl::cast<int>(trace.size() + model_run.size()) + 1, max_iterations)};

  if (std::holds_alternative<petri_net::a_star::nothing_found>(path)) {
    // A correctly built a_star search for this type of problem must always return given enough time
    legacy_embedded_debug_assert(std::get<petri_net::a_star::nothing_found>(path) == petri_net::a_star::nothing_found::YET);
    return std::nullopt;
  }

  // construct final alignment
  trace_alignment result{};
  using transitions_type = sequence_product::transition_list_type;

  auto trace_it{std::cbegin(trace)};
  auto transitions_it{std::cbegin(model_run)};
  using petri_net::trace_variable;

  for (const auto& t : std::get<transitions_type>(path)) {
    const auto move_on_log{t.move_first};
    const auto move_on_model{t.move_second};
    legacy_embedded_debug_assert(move_on_log || move_on_model);

    if (move_on_log && move_on_model) {
      result.add(alignment_move::sync(*trace_it, transitions_it->first));
      ++transitions_it;
      ++trace_it;
    } else if (move_on_log) {
      result.add(alignment_move::log(*trace_it));
      ++trace_it;
    } else if (move_on_model) {
      result.add(alignment_move::model(transitions_it->second, transitions_it->first));
      ++transitions_it;
    }
  }

  legacy_embedded_debug_assert(trace_it == std::cend(trace));
  legacy_embedded_debug_assert(transitions_it == std::cend(model_run));
  return result;
}

trace_alignment sequence_aligner::operator()(std::span<const row_id> trace,
                                             const common::execution_context& context) const {
  const auto sequence_alignment{align_sequence_to_run(trace, baseline_, max_iterations_, context)};
  if (sequence_alignment) {
    return sequence_alignment.value();
  }

  trace_alignment result{};
  // fallback: Add all log moves followed by all model moves
  for (const auto& activity : trace) {
    result.add(alignment_move::log(activity));
  }
  for (const auto& [transition_id, label] : baseline_) {
    result.add(alignment_move::model(label, transition_id));
  }
  return result;
}

}  // namespace celonis::accelerator::operators::process::alignment
