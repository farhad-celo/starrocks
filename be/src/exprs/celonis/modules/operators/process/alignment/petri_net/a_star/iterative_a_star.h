#pragma once

#include <algorithm>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "modules/common/exceptions.h"
#include "modules/operators/process/alignment/petri_net/a_star/shared_types.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::a_star {

namespace detail {

template <typename T, typename = void>
struct has_get_filter : std::false_type {};

template <typename T>
struct has_get_filter<T, std::void_t<decltype(std::declval<T>().get_filter(std::declval<typename T::marking_type>()))>>
    : std::true_type {};

template <typename T>
inline constexpr bool has_get_filter_v{has_get_filter<T>::value};

}  // namespace detail

enum class exit_code {
  SEARCHING,
  FOUND_FIT,
  DONE,
};

template <typename PETRI_NET, typename HEURISTIC, typename PATH_CONSTRUCTION, typename MARKING>
class iterative_a_star {
 public:
  iterative_a_star(PETRI_NET petri_net, HEURISTIC heuristic, PATH_CONSTRUCTION path_construction,
                   MARKING initial_marking,
                   typename HEURISTIC::cost_type max_cost = std::numeric_limits<typename HEURISTIC::cost_type>::max())
      : petri_net_{std::move(petri_net)},
        heuristic_{std::move(heuristic)},
        path_construction_{std::move(path_construction)},
        initial_marking_{std::move(initial_marking)},
        max_cost_{max_cost} {
    explore(initial_marking_, typename HEURISTIC::cost_type{});
  }

  [[nodiscard]] exit_code operator()() {
    if (path_construction_.empty()) {
      return exit_code::DONE;
    }
    const auto candidate{path_construction_.top().marking};
    const auto cost{path_construction_.top().cost};
    if (heuristic_.is_target(candidate)) {
      return exit_code::FOUND_FIT;
    }
    path_construction_.pop();
    explore(candidate, cost);
    return exit_code::SEARCHING;
  }

  /*
   * Not idempotent as we pop from the open set.
   */
  [[nodiscard]] auto reconstruct() {
    using transition_list_type = typename PETRI_NET::transition_list_type;
    if (path_construction_.empty()) {
      return std::optional<transition_list_type>{};
    }
    transition_list_type result{};
    for (auto marking{path_construction_.top().marking}; marking != initial_marking_; /*update in body*/) {
      const auto transition{path_construction_.trace_back(marking)};
      if (!transition.has_value()) {
        throw common::internal_exception{"Petri net A* search: Failed to reconstruct path"};
      }
      result.emplace_back(transition.value());
      marking = petri_net_.fire_inverse(std::move(marking), transition.value());
    }
    std::reverse(std::begin(result), std::end(result));

    // We want to be able to keep exploring a graph
    path_construction_.pop();

    return std::optional{std::move(result)};
  }

 private:
  PETRI_NET petri_net_;
  HEURISTIC heuristic_;
  PATH_CONSTRUCTION path_construction_;
  MARKING initial_marking_;

  typename HEURISTIC::cost_type max_cost_{std::numeric_limits<typename HEURISTIC::cost_type>::max()};

  template <typename COST>
  void explore(const MARKING& marking, COST cost) {
    auto transitions{petri_net_.get_enabled_transitions(marking)};

    // filter on the transitions, if possible
    // NB this has great potential to improve performance, but you need to know what you're doing
    //   e.g., if the heuristic is not consistent (or you don't know what that means), this is probably not for you
    using std::begin, std::end;
    auto end_transitions{end(transitions)};
    if constexpr (detail::has_get_filter_v<PATH_CONSTRUCTION>) {
      end_transitions = std::remove_if(begin(transitions), end_transitions, path_construction_.get_filter(marking));
    }

    // expand the remaining transitions
    for (auto it{begin(transitions)}; it != end_transitions; ++it) {
      auto marking_after_transition{petri_net_.fire(marking, *it)};
      const auto weight{heuristic_.get_weight(*it)};
      const auto estimate{heuristic_.estimate(marking_after_transition)};
      if (estimate.has_value() && cost + weight <= max_cost_) {
        path_construction_.enqueue(*it, std::move(marking_after_transition), cost + weight, estimate.value());
      }
    }
  }
};

template <typename PETRI_NET, typename HEURISTIC, typename PATH_CONSTRUCTION, typename MARKING, typename MAX_COST>
std::variant<typename PETRI_NET::transition_list_type, nothing_found> a_star_search(
    const PETRI_NET& petri_net, const HEURISTIC& heuristic, const PATH_CONSTRUCTION& path_construction,
    const MARKING& initial_marking, MAX_COST max_cost, int max_iterations) {
  if (max_iterations <= 0) {
    return {nothing_found::YET};
  }
  if (heuristic.is_target(initial_marking)) {
    return {typename PETRI_NET::transition_list_type{}};
  }
  iterative_a_star searcher{petri_net, heuristic, path_construction, initial_marking, max_cost};
  auto search_exit_code{searcher()};
  while (search_exit_code == exit_code::SEARCHING && max_iterations > 0) {
    --max_iterations;
    search_exit_code = searcher();
  }
  if (search_exit_code != exit_code::FOUND_FIT) {
    return {search_exit_code == exit_code::DONE ? nothing_found::AT_ALL : nothing_found::YET};
  }
  return searcher.reconstruct().value();
}

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::a_star
