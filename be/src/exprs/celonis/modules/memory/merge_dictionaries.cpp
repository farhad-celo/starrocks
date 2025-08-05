#include "merge_dictionaries.h"

#include <numeric>

#include "legacy_embedded_ctl/utility.h"
#include "modules/memory/merge_dictionaries_internals.h"

namespace celonis::accelerator::memory {

std::pair<dictionary_t, std::vector<legacy_embedded_ctl::shared_static_array<row_id>>> merge_n_dictionaries(
    const std::vector<std::pair<dictionary_t, std::string>>& dictionaries, const std::string& swap_file_prefix,
    const management::swap_info& sinfo, const std::string& description, const std::string& op_name,
    common::execution_context& context) {
  auto result = merge_n_dictionaries_raw(dictionaries, op_name, context);

  const raw_dictionary_t new_raw_dictionary{
      std::visit(legacy_embedded_ctl::overloaded{[&context](const dictionary_t& existing_dictionary) {
                                   return existing_dictionary->copy_to_raw_dictionary(context);
                                 },
                                 [](raw_dictionary_t&& new_dictionary) { return std::move(new_dictionary); }},
                 std::move(result.dictionary_variant))};

  std::vector<legacy_embedded_ctl::shared_static_array<row_id>> ptr_mappings;
  std::transform(result.mappings.begin(), result.mappings.end(), std::back_inserter(ptr_mappings),
                 [](legacy_embedded_ctl::static_array<row_id>& mapping) { return std::move(mapping); });

  return {new_raw_dictionary->convert_to_dictionary_t_release_data(swap_file_prefix, sinfo, description),
          std::move(ptr_mappings)};
}

/**
 * If columns created from a null constant are merged with a columns containing only null values, it needs to be ensured
 * that one of the latter is chosen for to preserve the type and to ensure that the dict owner doesn't get destroyed
 * too early.
 * To ensure this, this function first checks which type the result should have and then takes the first column that
 * is swappable (to guarantee that it is persistent).
 *
 * Since currently null constant columns are of type int, they need to be treated specially in the code.
 */
size_t get_merge_null_dicts_result_index(const std::vector<std::pair<dictionary_t, std::string>>& dics) {
  data_type result_type = cel_null;
  for (const auto& dic : dics) {
    data_type curr_type = dic.first->type;
    if (curr_type == cel_int) {
      result_type = cel_int;
    } else if (curr_type != cel_null) {
      result_type = curr_type;
      break;
    }
  }

  // To make the loop a bit simpler, if no swappable dict exists, the index of the last dict with the correct type is
  // returned
  size_t result_index = 0;
  for (size_t i = 0; i < dics.size(); ++i) {
    data_type curr_type = dics[i].first->type;
    if (curr_type != result_type) {
      continue;
    }

    if (dics[i].first->is_swappable()) {
      return i;
    }

    result_index = i;
  }
  return result_index;
}

merge_result merge_n_dictionaries_raw(const std::vector<std::pair<dictionary_t, std::string>>& dictionaries,
                                      const std::string& op_name, const common::execution_context& context) {
  auto merge_n_dictionary_context = context.create_sub_context("merge_n_dictionaries_raw", {});
  if (dictionaries.empty()) {
    throw common::internal_exception{"{}: no dictionaries supplied", op_name};
  }

  if (dictionaries.size() == 1) {
    return {dictionaries[0].first, {}, dictionaries[0].first->get_size()};
  }

  std::vector<details::non_null_dictionary> non_null_dict;
  std::vector<size_t> null_indexes;

  for (size_t i{0}; i < dictionaries.size(); ++i) {
    if (dictionaries[i].first->get_size() == 1) {
      null_indexes.push_back(i);
    } else {
      details::non_null_dictionary dict;
      dict.dict = dictionaries[i].first;
      dict.dict_name = dictionaries[i].second;
      non_null_dict.push_back(std::move(dict));
    }
  }
  merge_result result;

  if (non_null_dict.empty()) {
    const size_t result_index = get_merge_null_dicts_result_index(dictionaries);
    result = {dictionaries[result_index].first, {}, dictionaries[result_index].first->get_size()};
  } else if (non_null_dict.size() == 1) {
    row_id dict_size = non_null_dict[0].dict->get_size();
    auto mapping_d1{memory::tracking::make_static_array_for_overwrite<row_id>(
        dict_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};
    std::iota(mapping_d1.begin(), mapping_d1.end(), row_id{0});

    auto mappings{memory::tracking::make_static_array_for_overwrite<legacy_embedded_ctl::static_array<row_id>>(
        1, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG), context)};
    mappings[0] = std::move(mapping_d1);
    result = {non_null_dict[0].dict, std::move(mappings), non_null_dict[0].dict->get_size()};
  } else {
    result = details::merge_n_dictionaries_raw_internal(non_null_dict, op_name, merge_n_dictionary_context);
  }

  if (result.mappings.size() == dictionaries.size()) {
    return result;
  }

  auto new_size{result.mappings.size() + null_indexes.size()};
  auto mappings{memory::tracking::make_static_array_for_overwrite<legacy_embedded_ctl::static_array<row_id>>(
      new_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};
  auto null_index_it{null_indexes.begin()};
  auto* mapping_it{result.mappings.begin()};
  for (std::size_t i{0}; i < new_size; ++i) {
    if (null_index_it != null_indexes.end() && i == *null_index_it) {
      auto mapping{
          memory::tracking::make_static_array_for_overwrite<row_id>(1, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};
      mapping[0] = 0;
      mappings[i] = std::move(mapping);
      ++null_index_it;
    } else if (mapping_it != result.mappings.end()) {
      mappings[i] = std::move(*mapping_it);
      ++mapping_it;
    } else {
      throw common::internal_exception("Adding mappings for dictionaries failed at index [{}]", i);
    }
  }
  legacy_embedded_debug_assert(null_index_it == null_indexes.end());
  legacy_embedded_debug_assert(mapping_it == result.mappings.end());
  result.mappings = std::move(mappings);

  return result;
}

}  // namespace celonis::accelerator::memory
