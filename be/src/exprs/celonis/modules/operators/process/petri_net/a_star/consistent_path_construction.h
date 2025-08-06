#pragma once
#include <functional>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

#include "legacy_embedded_ctl/type_traits.h"
#include "legacy_embedded_ctl/utils/allocation_messages.h"
#include "modules/memory/management/memory_checked_containers.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::a_star {

/**
 * Bookkeeping of explored and to-be-explored paths for the Petri net A* implementation
 *
 * This class assumes that the heuristic used is CONSISTENT, i.e., the estimate for a marking is never larger than the
 * cost to reach any of its neighbors plus that neighbor's estimate.
 *
 * @tparam MARKING the type of marking
 * @tparam TRANSITION the type of transition
 * @tparam COST the type of the cost value (from the heuristic)
 */
template <typename MARKING, typename TRANSITION, typename COST, typename MARKING_HASH = std::hash<MARKING>,
          typename MARKING_EQUAL_TO = std::equal_to<MARKING>>
class consistent_path_construction {
 public:
  using marking_value_type = legacy_embedded_ctl::remove_all_cvr_t<MARKING>;
  using marking_const_reference_type = std::add_lvalue_reference_t<std::add_const_t<MARKING>>;
  using transition_value_type = legacy_embedded_ctl::remove_all_cvr_t<TRANSITION>;
  using cost_value_type = legacy_embedded_ctl::remove_all_cvr_t<COST>;

  explicit consistent_path_construction(const common::execution_context& context)
      : open_set_{std::greater<>{},
                  memory::management::checked_vector_t<open_node_type>{
                      memory::management::checked_allocator<memory::management::checked_vector_t<open_node_type>>(
                          context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMBER_INIT_MSG))}},
        partial_paths_{
            memory::management::checked_allocator<partial_path_t>(context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMBER_INIT_MSG))} {}

  // NOLINTNEXTLINE(bugprone-exception-escape)
  struct marking_wrapper {
    marking_value_type marking;
    cost_value_type cost;
  };

  [[nodiscard]] const marking_wrapper& top() const noexcept { return open_set_.top(); }

  void pop() {
    // add top to the history of incomplete paths
    auto& to_be_popped{open_set_.top()};
    partial_paths_.emplace(std::move(to_be_popped.marking), std::move(to_be_popped.enabling_transition));
    open_set_.pop();
  }

  [[nodiscard]] size_t size() const noexcept { return open_set_.size(); }

  [[nodiscard]] bool empty() const noexcept { return open_set_.empty(); }

  [[nodiscard]] std::optional<transition_value_type> trace_back(marking_const_reference_type marking) const {
    const auto it{partial_paths_.find(marking)};
    if (it != end(partial_paths_)) {
      return it->second;
    }
    if (!open_set_.empty() && open_set_.top().marking == marking) {
      return {open_set_.top().enabling_transition};
    }
    return {};
  }

  void enqueue(transition_value_type enabling_transition, marking_const_reference_type marking, cost_value_type cost,
               cost_value_type estimate) {
    if (partial_paths_.find(marking) == end(partial_paths_)) {
      open_set_.emplace(open_node_type{{std::move(marking), cost}, estimate, enabling_transition});
    }
  }

 private:
  // NOLINTNEXTLINE(bugprone-exception-escape)
  struct open_node_type : marking_wrapper {
    cost_value_type estimate;
    transition_value_type enabling_transition;
    constexpr bool operator>(const open_node_type& rhs) const {
      // Break ties in a depth-first manner.
      return std::forward_as_tuple(this->cost + estimate, estimate, enabling_transition) >
             std::forward_as_tuple(rhs.cost + rhs.estimate, rhs.estimate, enabling_transition);
    }
  };

  using open_set_type =
      std::priority_queue<open_node_type, memory::management::checked_vector_t<open_node_type>, std::greater<>>;
  using partial_path_t = memory::management::checked_ska_hash_map_t<marking_value_type, transition_value_type,
                                                                    MARKING_HASH, MARKING_EQUAL_TO>;

  open_set_type open_set_;
  partial_path_t partial_paths_;

 protected:
  [[nodiscard]] const open_set_type& get_open_set() const noexcept { return open_set_; }
  [[nodiscard]] const auto& get_partial_paths() const noexcept { return partial_paths_; }
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::a_star
