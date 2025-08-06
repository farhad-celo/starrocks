#include "modules/operators/process/alignment/input_output_mapper.h"

#include <memory>

#include "legacy_embedded_ctl/conversion.h"
#include "modules/memory/typed_dictionary.h"
#include "modules/operators/process/petri_net/petri_net_entities.h"

namespace celonis::accelerator::operators::process::alignment {

string_to_int_mapper::string_to_int_mapper(const std::shared_ptr<memory::string_dictionary>& dict) {
  string_to_int_.reserve(dict->get_size());
  int_to_string_.reserve(dict->get_size());

  // Add activity ids in activity column to string mapper
  // This is only needed to create the output dictionary later
  for (row_id i{0}; i < dict->get_size(); ++i) {
    const auto val{dict->get_string_value(i)};
    int_to_string_.try_emplace(i, val);
    string_to_int_.try_emplace(val, i);
  }
}

string_to_int_mapper string_to_int_mapper::from(std::span<const cel_string_t> dict) {
  string_to_int_mapper result{};
  std::ranges::for_each(dict, [idx = label_id_t{0}, &result](auto* str) mutable {
    const auto [it, success]{result.string_to_int_.try_emplace(std::string{str}, idx)};
    if (success) {
      result.int_to_string_.try_emplace(it->second, it->first);
      ++idx;
    }
  });
  return result;
}

std::string string_to_int_mapper::get_string(row_id label_id) const { return int_to_string_.at(label_id); }

row_id string_to_int_mapper::get_label_id(const std::string& transition_str_id) {
  const auto [entry, success]{string_to_int_.try_emplace(transition_str_id, cur_int_id_)};
  if (success) {
    int_to_string_.try_emplace(cur_int_id_, transition_str_id);
    cur_int_id_--;
  }

  return entry->second;
}

petri_net::petri_net_place_id input_output_mapper::get_place_for_str_id(const std::string& place_str_id) const {
  return place_str_id_to_place_.at(place_str_id);
}

petri_net::petri_net_transition_id input_output_mapper::get_transition_for_str_id(
    const std::string& transition_str_id) const {
  return transition_str_id_to_transition_.at(transition_str_id);
}

petri_net::petri_net_place_id input_output_mapper::add_place_for_str_id(const std::string& place_str_id) {
  const auto ret{petri_net::petri_net_place_id(place_id_counter_)};
  const auto [entry, success]{place_str_id_to_place_.try_emplace(place_str_id, ret)};

  if (success) {
    place_id_counter_++;
  }

  return entry->second;
}

petri_net::petri_net_transition_id input_output_mapper::add_transition_for_str_id(
    const std::string& transition_str_id) {
  const auto transition{petri_net::petri_net_transition_id(transition_id_counter_)};
  const auto [cur_element, inserted]{transition_str_id_to_transition_.try_emplace(transition_str_id, transition)};

  if (inserted) {
    transition_id_counter_++;
  }

  return cur_element->second;
}

uint16_t input_output_mapper::get_max_transition_id() const { return transition_id_counter_; }

uint16_t input_output_mapper::get_max_place_id() const { return place_id_counter_; }

}  // namespace celonis::accelerator::operators::process::alignment
