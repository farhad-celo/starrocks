#pragma once

#include <functional>
#include <optional>
#include <utility>

#include <boost/heap/pairing_heap.hpp>
#include <bytell_hash_map.hpp>

#include "legacy_embedded_ctl/type_traits.h"
#include "modules/memory/management/memory_checked_containers.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::a_star {

/**
 * Bookkeeping of explored and to-be-explored paths for the Petri net A* implementation
 *
 * This class assumes that the heuristic used is admissible, but not necessarily consistent.
 * Please use the consistent_path_construction if you know that the heuristic is consistent.
 *
 * @tparam MARKING the type of marking
 * @tparam TRANSITION the type of transition
 * @tparam COST the type of the cost value (from the heuristic)
 * @tparam MARKING_HASH the type used to hash markings
 * @tparam MARKING_EQUALS the type used to compare markings
 */
template <typename MARKING, typename TRANSITION, typename COST,
          typename MARKING_HASH = std::hash<legacy_embedded_ctl::remove_all_cvr_t<MARKING>>,
          typename MARKING_EQUALS = std::equal_to<legacy_embedded_ctl::remove_all_cvr_t<MARKING>>>
class inconsistent_path_construction {
 public:
  using marking_value_type = legacy_embedded_ctl::remove_all_cvr_t<MARKING>;
  using marking_const_reference_type = std::add_lvalue_reference_t<std::add_const_t<MARKING>>;
  using transition_value_type = legacy_embedded_ctl::remove_all_cvr_t<TRANSITION>;
  using cost_value_type = legacy_embedded_ctl::remove_all_cvr_t<COST>;

  // NOLINTNEXTLINE(bugprone-exception-escape)
  struct marking_wrapper {
    marking_value_type marking;
    cost_value_type cost;
  };

  explicit inconsistent_path_construction(const common::execution_context& context)
      : open_set_{context},
        partial_paths_{
            memory::management::checked_allocator<partial_paths_type>(context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMBER_INIT_MSG))} {}

  [[nodiscard]] const marking_wrapper& top() const noexcept { return open_set_.top(); }

  void pop() {
    // add top to the history of incomplete paths
    auto& node{open_set_.top()};
    if (const auto it{partial_paths_.find(node.marking)}; it == end(partial_paths_) || it->second.cost > node.cost) {
      partial_paths_.insert_or_assign(it, node.marking, transition_wrapper{node.enabling_transition, node.cost});
    }

    open_set_.pop();
  }

  [[nodiscard]] size_t size() const noexcept { return open_set_.size(); }

  [[nodiscard]] bool empty() const noexcept { return open_set_.empty(); }

  [[nodiscard]] std::optional<transition_value_type> trace_back(marking_const_reference_type marking) const {
    if (const auto it{partial_paths_.find(marking)}; it != end(partial_paths_)) {
      return it->second.transition;
    }
    if (!open_set_.empty() && open_set_.top().marking == marking) {
      return {open_set_.top().enabling_transition};
    }
    return {};
  }

  void enqueue(transition_value_type enabling_transition, marking_const_reference_type marking, cost_value_type cost,
               cost_value_type estimate) {
    if (const auto it{partial_paths_.find(marking)}; it == end(partial_paths_) || cost < it->second.cost) {
      open_set_.insert_or_try_increase(enabling_transition, marking, cost, estimate);
    }
  }

 private:
  struct transition_wrapper {
    transition_value_type transition;
    cost_value_type cost;
  };

  class open_set_type {
   public:
    explicit open_set_type(const common::execution_context& context)
        : lookup_{memory::management::checked_allocator<lookup_type>(context, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::MEMBER_INIT_MSG))} {}
    // NOLINTNEXTLINE(bugprone-exception-escape)
    struct node_type : marking_wrapper {
      cost_value_type estimate;
      transition_value_type enabling_transition;
      friend constexpr bool operator>(const node_type& lhs, const node_type& rhs) noexcept {
        return std::tuple{lhs.cost + lhs.estimate, lhs.estimate, lhs.enabling_transition} >
               std::tuple{rhs.cost + rhs.estimate, rhs.estimate, rhs.enabling_transition};
      }
    };

    [[nodiscard]] bool empty() const { return lookup_.empty(); }

    [[nodiscard]] const node_type& top() const { return heap_.top(); }

    void pop() {
      lookup_.erase(heap_.top().marking);
      heap_.pop();
    }

    void insert_or_try_increase(transition_value_type enabling_transition, marking_const_reference_type marking,
                                cost_value_type cost, cost_value_type estimate) {
      const auto it{lookup_.find(marking)};
      if (it == end(lookup_)) {  // insert
        const auto handle{heap_.emplace(node_type{{marking, cost}, estimate, enabling_transition})};
        lookup_.emplace(std::move(marking), handle);
      } else {  // update, but only if we have found a shorter path to this marking
        const auto& handle{it->second};
        auto& node{*handle};
        // check if the new node is superior to the previous node
        if (node.cost > cost) {
          node.cost = cost;
          node.enabling_transition = enabling_transition;
          heap_.increase(handle);  // this is a max heap, so DE-creasing cost+estimate IN-creases the priority
        }
      }
    }

   private:
    // The boost::heap::pairing_heap constructors do not allow us to inject a tracking allocator, so use the default
    using heap_type = boost::heap::pairing_heap<node_type, boost::heap::compare<std::greater<>>>;

    using lookup_type = memory::management::checked_ska_hash_map_t<marking_value_type, typename heap_type::handle_type,
                                                                   MARKING_HASH, MARKING_EQUALS>;

    heap_type heap_{};
    lookup_type lookup_;
  };

  open_set_type open_set_;

  using partial_paths_type =
      memory::management::checked_ska_hash_map_t<marking_value_type, transition_wrapper, MARKING_HASH, MARKING_EQUALS>;
  partial_paths_type partial_paths_;
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::a_star
