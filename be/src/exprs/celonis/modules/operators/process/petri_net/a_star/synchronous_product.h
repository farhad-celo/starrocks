#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include <boost/functional/hash.hpp>

#include "modules/operators/process/petri_net/petri_net.h"
#include "modules/operators/process/petri_net/petri_net_entities.h"

namespace celonis::accelerator::operators::process::alignment::petri_net::a_star {

class synchronous_product {
 public:
  struct transition_type {
    bool move_on_log{false};
    // If the transition advances the model (i.e. move_on_log == false), then this indicates whether
    // petri_net_transition is a tau transition. If move_on_log == true, then is_visible_model should rather be false,
    // but if you are correctly using this class, you shouldn't care about the value of is_visible_model unless it's
    // a sync or model move
    bool is_visible_model{false};
    std::optional<petri_net_transition_id> petri_net_transition{};

    // NOLINTNEXTLINE(modernize-use-nullptr,readability-implicit-bool-conversion)
    auto operator<=>(const transition_type& rhs) const = default;

    [[nodiscard]] constexpr bool is_log_move() const { return move_on_log && !petri_net_transition.has_value(); }
    [[nodiscard]] constexpr bool is_model_move() const { return !move_on_log && petri_net_transition.has_value(); }
    [[nodiscard]] constexpr bool is_synchronous_move() const { return move_on_log && petri_net_transition.has_value(); }
    [[nodiscard]] constexpr bool is_visible_model_move() const { return is_model_move() && is_visible_model; }
    [[nodiscard]] constexpr bool is_tau_move() const { return is_model_move() && !is_visible_model; }
  };

  // NOLINTNEXTLINE(bugprone-exception-escape)
  struct marking_type {
    petri_net::marking_type petri_net_marking;
    using variant_place_type = size_t;
    variant_place_type variant_place{};  // this is also the index of the next "variant"-transition
    [[nodiscard]] bool operator==(const marking_type& rhs) const {
      return petri_net_marking == rhs.petri_net_marking && variant_place == rhs.variant_place;
    }
    [[nodiscard]] bool operator!=(const marking_type& rhs) const { return !operator==(rhs); }
    [[nodiscard]] bool operator<(const marking_type& rhs) const {
      return std::pair{petri_net_marking, variant_place} < std::pair{rhs.petri_net_marking, rhs.variant_place};
    }
  };
  using transition_list_type = transitions_container_t<transition_type>;

  synchronous_product(const petri_net_accessor& petri_net, std::span<const row_id> variant);

  [[nodiscard]] transition_list_type get_enabled_transitions(const marking_type& marking) const;

  [[nodiscard]] marking_type fire(marking_type marking, const transition_type& transition) const;

  [[nodiscard]] marking_type fire_inverse(marking_type marking, const transition_type& transition) const;

  [[nodiscard]] marking_type get_initial_marking() const { return {petri_net_.get_initial_marking(), {}}; }

 private:
  const petri_net_accessor& petri_net_;
  marking_type::variant_place_type variant_length_;
  using variant_transition_type =
      marking_type::variant_place_type;  // NB This is the index of the *transition*, whereas variant_place_type is the
                                         //  index of the *place* if we think of the variant as a linear Petri net

  struct synchronous_transition_type {
    synchronous_transition_type(variant_transition_type variant_index, petri_net_transition_id transition)
        : variant_index{variant_index}, petri_net_transition{transition} {}
    variant_transition_type variant_index{};
    petri_net_transition_id petri_net_transition{};
  };
  std::vector<synchronous_transition_type> synchronous_transitions_;

  [[nodiscard]] transition_list_type get_synchronous_moves(const marking_type& marking) const;

  [[nodiscard]] std::optional<transition_type> get_log_move(const marking_type& marking) const;

  [[nodiscard]] transition_list_type get_model_moves(const marking_type& marking) const;
};

}  // namespace celonis::accelerator::operators::process::alignment::petri_net::a_star

namespace std {
template <>
struct hash<celonis::accelerator::operators::process::alignment::petri_net::a_star::synchronous_product::marking_type> {
  size_t operator()(
      const celonis::accelerator::operators::process::alignment::petri_net::a_star::synchronous_product::marking_type&
          m) const {
    size_t result{0};
    boost::hash_combine(result, m.variant_place);
    boost::hash_combine(result, m.petri_net_marking);
    return result;
  }
};
}  // namespace std
