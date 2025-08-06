#pragma once

#include <cpml/constants.h>

#include <limits>
#ifdef CELOSTAR
#include <span>
#endif
#include <string>
#include <unordered_map>
#include <vector>

#include "modules/memory/row_id.h"
#include "modules/memory/typed_dictionary.h"
#include "modules/operators/process/petri_net/petri_net_entities.h"

namespace celonis::accelerator::operators::process::alignment {

class string_to_int_mapper {
 public:
  using label_id_t = row_id;

  [[nodiscard]] static constexpr label_id_t get_tau_transition_id() noexcept { return cpml::TAU_ID; }

  [[nodiscard]] static constexpr bool is_tau_transition(label_id_t label_id) noexcept {
    return label_id == get_tau_transition_id();
  }

  string_to_int_mapper() = default;
  explicit string_to_int_mapper(const std::shared_ptr<memory::string_dictionary>& dict);

  static string_to_int_mapper from(std::span<const cel_string_t> dict);

  [[nodiscard]] label_id_t get_label_id(const std::string& transition_str_id);

  [[nodiscard]] std::string get_string(label_id_t label_id) const;

  [[nodiscard]] const std::unordered_map<label_id_t, std::string>& get_data() const noexcept { return int_to_string_; }

  /**
   * Returns whether a label was assigned during construction, or added later
   * @param label a valid label (i.e., a result of calling `get_label_id` )
   * @return A boolean indicating whether the label was assigned after construction or not
   *
   * Note that we don't have the means to detect labels that have not been assigned here!
   */
  [[nodiscard]] bool was_added_after_construction(label_id_t label) const noexcept { return label > cur_int_id_; }

 private:
  label_id_t cur_int_id_{ROW_ID_MAX - 1};

  std::unordered_map<label_id_t, std::string> int_to_string_{};
  std::unordered_map<std::string, label_id_t> string_to_int_{};
};

/** Maps from integer place/label identifiers to place/transitions continuously assigned */
class input_output_mapper {
 public:
  [[nodiscard]] petri_net::petri_net_place_id get_place_for_str_id(const std::string& place_str_id) const;

  [[nodiscard]] petri_net::petri_net_transition_id get_transition_for_str_id(
      const std::string& transition_str_id) const;

  [[nodiscard]] petri_net::petri_net_place_id add_place_for_str_id(const std::string& place_str_id);

  [[nodiscard]] petri_net::petri_net_transition_id add_transition_for_str_id(const std::string& transition_str_id);

  [[nodiscard]] uint16_t get_max_transition_id() const;

  [[nodiscard]] uint16_t get_max_place_id() const;

 private:
  /// Int counter for transition ID (0 is reserved for NULL assignment)
  uint16_t transition_id_counter_{1};

  /// Maps transition string id to transition_t
  std::unordered_map<std::string, petri_net::petri_net_transition_id> transition_str_id_to_transition_{};

  /// Int counter for place ID
  uint16_t place_id_counter_{0};

  /// Maps place string id to uint16_t (and back)
  std::unordered_map<std::string, petri_net::petri_net_place_id> place_str_id_to_place_{};
};

}  // namespace celonis::accelerator::operators::process::alignment
