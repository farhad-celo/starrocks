#pragma once

#include <memory>
#include <variant>

#include <ctl/static_array.h>

#include "modules/memory/raw_dictionary.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::memory {

/* Merge n dictionaries (of the same type, otherwise, an exception is thrown)
 *
 * @deprecated in favor of merge_n_dictionaries_raw which returns a raw dictionary
 *
 * @return a dictionary and a vector of shared_pointers for remapping the column pointers
 */
std::pair<dictionary_t, std::vector<ctl::shared_static_array<row_id>>> merge_n_dictionaries(
    const std::vector<std::pair<dictionary_t, std::string>>& dictionaries, const std::string& description,
    const std::string& op_name, common::execution_context& context);

struct merge_result {
  std::variant<raw_dictionary_t, dictionary_t> dictionary_variant;
  ctl::static_array<ctl::static_array<row_id>> mappings;
  // the dict size could be taken from the dictionary, but it is more convenient to store it separately
  row_id dict_size{};
};

using dictionary_info_t = std::pair<dictionary_t, std::string>;
using dictionary_infos_t = std::vector<dictionary_info_t>;

/* Merge n dictionaries (of the same type, otherwise, an exception is thrown)
 *
 * @return a raw dictionary and a vector of static_arrays for remapping the column pointers
 */
merge_result merge_n_dictionaries_raw(const dictionary_infos_t& dictionaries, const std::string& op_name,
                                      const common::execution_context& context);

}  // namespace celonis::accelerator::memory
