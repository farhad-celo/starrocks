#include "modules/memory/typed_dictionary.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <ostream>
#include <utility>

#include <tbb/parallel_for.h>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/conversion.h"
#include "modules/common/date/celonis_date_storage.h"
#include "modules/common/exceptions.h"
#include "modules/common/execution_context_fwd.h"
#include "modules/common/shared_types.h"
#include "modules/common/shared_types_comparison_functions.h"
#include "modules/memory/management/const_data_accessor.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/managed_memory_group.h"
#include "modules/memory/management/swap_info.h"
#include "modules/memory/raw_dictionary.h"
#include "modules/memory/tracking/static_array_with_context_tracking.h"
#include "types/uuid/uuid_storage.h"

namespace celonis::accelerator::memory {

template <typename T>
dictionary_map dictionary_map::create(const typed_dictionary<T>& source_dict, const typed_dictionary<T>& target_dict,
                                      const common::execution_context& context) {
  const auto source_data = source_dict.get_const_data();
  const auto target_data = target_dict.get_const_data();
  auto target_map = memory::tracking::make_static_array_for_overwrite<row_id>(
      source_data.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_COLUMN_MSG), context);
  // The index 0 is initialized to 0 as null is at index 0 in all dictionaries.
  target_map[0] = 0;
  constexpr less<T> less_than{};
  constexpr equal_to<T> equals_to{};

  // NULL is at index 0 so we start from 1.
  tbb::parallel_for(
      tbb::blocked_range<row_id>(1, static_cast<row_id>(source_data.size()), 100'000), [&](const auto range) {
        const auto* target_it =
            std::lower_bound(target_data.begin() + 1, target_data.end(), source_data[range.begin()], less_than);
        for (row_id source_index = range.begin(); source_index < range.end(); source_index++) {
          const auto source_val = source_data[source_index];
          // Find the matching record.
          target_it = std::find_if_not(target_it, target_data.end(),
                                       [source_val, less_than](auto b) { return less_than(b, source_val); });

          if (target_it == target_data.end()) {
            std::fill(target_map.begin() + source_index, target_map.begin() + range.end(), VALUE_NOT_FOUND);
            break;
          }

          target_map[source_index] = equals_to(*target_it, source_val)
                                         ? static_cast<row_id>(target_it - target_data.begin())
                                         : VALUE_NOT_FOUND;
        }
      });

  return dictionary_map{std::move(target_map), legacy_embedded_ctl::cast<row_id>(target_data.size())};
}

template <typename T>
typed_dictionary<T>::typed_dictionary(management::raw_data_handler_t<T> data_handler)
    : dictionary{get_matching_data_type<T>()}, data_handler{std::move(data_handler)} {}

template <typename T>
typename typed_dictionary<T>::const_data_accessor_t typed_dictionary<T>::get_const_data(
    const common::execution_context& context) const {
  return data_handler->get_const_data(context);
}

template <typename T>
void typed_dictionary<T>::swap_in(common::execution_context& context) {
  data_handler->swap_in(context);
}

#ifndef CELOSTAR
template <typename T>
void typed_dictionary<T>::swap_out(common::execution_context& context) {
  data_handler->swap_out(context);
}

template <typename T>
bool typed_dictionary<T>::write_out(common::execution_context& context) {
  data_handler->write_out(context);
  return true;
}
#endif

template <typename T>
management::load_status typed_dictionary<T>::get_load_status() const {
  return data_handler->get_load_status();
}

template <typename T>
bool typed_dictionary<T>::is_swappable() const {
  return data_handler->is_swappable();
}

template <typename T>
bool typed_dictionary<T>::swap_file_broken() const {
  return data_handler->swap_file_broken();
}

template <typename T>
row_id typed_dictionary<T>::get_size() const {
  return legacy_embedded_ctl::cast<row_id>(data_handler->get_size());
}

template <typename T>
row_id typed_dictionary<T>::get_row_id_for(const T& v, const common::execution_context& context) const {
  const auto data = get_const_data(context);
  // no null
  const auto* start{std::next(data.get())};
  const auto* end{std::next(data.get(), get_size())};
  const auto* value{std::lower_bound(start, end, v)};
  if (value == end || v != *value) {
    return VALUE_NOT_FOUND;
  }
  // this will return the index
  return static_cast<row_id>(std::distance(data.get(), value));
}

template <typename T>
row_id typed_dictionary<T>::lower_bound(const T& v, const common::execution_context& context) const {
  const auto data = get_const_data(context);
  // no null
  const auto* start{std::next(data.get())};
  const auto* end{std::next(data.get(), get_size())};
  return static_cast<row_id>(std::distance(data.get(), std::lower_bound(start, end, v)));
}

template <typename T>
row_id typed_dictionary<T>::upper_bound(const T& v, const common::execution_context& context) const {
  const auto data = get_const_data(context);
  // no null
  const auto* start{std::next(data.get())};
  const auto* end{std::next(data.get(), get_size())};
  return static_cast<row_id>(std::distance(data.get(), std::upper_bound(start, end, v)));
}

template <typename T>
dictionary_map typed_dictionary<T>::create_dictionary_mapping(const std::shared_ptr<dictionary>& other,
                                                              const common::execution_context& context) {
  if (type != other->type) {
    throw common::cpm_exception{
        "create_dictionary_mapping failed because types of dictionaries do not match, [{}] and [{}].",
        convert_to_string(type), convert_to_string(other->type)};
  }
  const std::shared_ptr<typed_dictionary<T>> casted_other = std::static_pointer_cast<typed_dictionary<T>>(other);
  return dictionary_map::create(*this, *casted_other, context);
}

template <typename T>
std::string typed_dictionary<T>::get_string_value(row_id ptr) const {
  return get_string_value_opt(ptr).value_or("NULL");
}

template <typename T>
std::optional<std::string> typed_dictionary<T>::get_string_value_opt(row_id ptr) const {
  if (ptr == 0) {
    return std::nullopt;
  }

  const auto value = get_const_data().at(ptr);
  std::ostringstream converter;
  converter << std::fixed << value;
  return converter.str();
}

template <typename T>
size_t typed_dictionary<T>::get_size_in_memory() const {
  return data_handler->get_size_in_memory();
}

template <typename T>
usage_time_t typed_dictionary<T>::time_of_last_usage() const {
  return data_handler->get_last_usage();
}

template <>
inline std::optional<std::string> typed_dictionary<cel_boolean_t>::get_string_value_opt(row_id ptr) const {
  if (ptr == 0) {
    return std::nullopt;
  }

  const cel_boolean_t bool_val = get_const_data().at(ptr);
  return bool_val ? "TRUE" : "FALSE";
}

// Specialization of the get_string_value for boolean data_type
template <>
inline std::string typed_dictionary<cel_boolean_t>::get_string_value(row_id ptr) const {
  return get_string_value_opt(ptr).value_or("NULL");
}

template <>
inline std::optional<std::string> typed_dictionary<cel_date_t>::get_string_value_opt(row_id ptr) const {
  if (ptr == 0) {
    return std::nullopt;
  }

  const cel_date_t date_val = get_const_data().at(ptr);
  std::ostringstream converter;
  converter << date_val.to_timestamp();
  return converter.str();
}

// Specialization of the get_string_value for date data_type
template <>
inline std::string typed_dictionary<cel_date_t>::get_string_value(row_id ptr) const {
  return get_string_value_opt(ptr).value_or("NULL");
}

template <typename T>
void typed_dictionary<T>::add_to_group(std::shared_ptr<management::managed_memory_group> managed_group) {
  managed_group->add_to_group(data_handler);
}

template <typename T>
raw_dictionary_t typed_dictionary<T>::copy_to_raw_dictionary(common::execution_context& context) {
  legacy_embedded_ctl::static_array<T> output_array;
  {
    const auto data{get_const_data(context)};
    output_array =
        memory::tracking::make_static_array_for_overwrite<T>(data.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context);
    std::copy(data.begin(), data.end(), output_array.begin());
  }

  return std::make_unique<typed_raw_dictionary<T>>(std::move(output_array));
}

template <typename T>
void typed_dictionary<T>::set_delete_from_disk_when_destructed(const bool value) {
  data_handler->set_delete_from_disk_when_destructed(value);
}

template <typename T>
dictionary_t typed_dictionary<T>::create_dictionary(legacy_embedded_ctl::static_array<T>&& data, const std::string& swap_file_name,
                                                    const management::swap_info& sinfo,
                                                    const std::string& description) {
  return std::make_shared<typed_dictionary<T>>(management::raw_data_handler<T>::create_data_handler(
      std::move(data), swap_file_name + memory::management::DICT_ENDING, sinfo, description));
}

#ifndef CELOSTAR
template <typename T>
std::shared_ptr<dictionary> typed_dictionary<T>::init_from_swap(const std::string& swap_file_name,
                                                                const management::swap_info& sinfo,
                                                                const std::string& description) {
  management::raw_data_handler_t<T> dict_swap = management::raw_data_handler<T>::init_from_swap(
      swap_file_name + memory::management::DICT_ENDING, sinfo, description);
  if (dict_swap != nullptr) {
    std::shared_ptr<dictionary> dictionary(new typed_dictionary<T>(dict_swap));
    return dictionary;
  }
  return {};
}
#endif

typed_dictionary<cel_string_t>::typed_dictionary(std::shared_ptr<management::string_data_handler> string_data)
    : dictionary(data_type::cel_string), string_data_(std::move(string_data)) {}

using const_data_accessor_t = management::string_data_handler::const_data_accessor_t;

[[nodiscard]] typed_dictionary<cel_string_t>::const_data_accessor_t typed_dictionary<cel_string_t>::get_const_data(
    const common::execution_context& context) const {
  return string_data_->get_const_data(context);
}

void typed_dictionary<cel_string_t>::swap_in(common::execution_context& context) { string_data_->swap_in(context); }

#ifndef CELOSTAR
void typed_dictionary<cel_string_t>::swap_out(common::execution_context& context) { string_data_->swap_out(context); }

bool typed_dictionary<cel_string_t>::write_out(common::execution_context& context) {
  return string_data_->write_out(context);
}
#endif

management::load_status typed_dictionary<cel_string_t>::get_load_status() const {
  return string_data_->get_load_status();
}

[[nodiscard]] bool typed_dictionary<cel_string_t>::is_swappable() const { return string_data_->is_swappable(); }

[[nodiscard]] bool typed_dictionary<cel_string_t>::swap_file_broken() const { return string_data_->swap_file_broken(); }

[[nodiscard]] row_id typed_dictionary<cel_string_t>::get_size() const {
  const size_t size{string_data_->get_size()};
  legacy_embedded_debug_assert(size <= static_cast<size_t>(std::numeric_limits<row_id>::max()));
  return static_cast<row_id>(size);
}

[[nodiscard]] size_t typed_dictionary<cel_string_t>::get_string_buffer_size() const {
  return string_data_->get_pointer_buffer_size();
}

void typed_dictionary<cel_string_t>::add_to_group(std::shared_ptr<management::managed_memory_group> managed_group) {
  managed_group->add_to_group(string_data_);
}

raw_dictionary_t typed_dictionary<cel_string_t>::copy_to_raw_dictionary(common::execution_context& context) {
  legacy_embedded_ctl::static_array<char> buffer;
  legacy_embedded_ctl::static_array<cel_string_t> output_array;
  {
    const auto data{get_const_data(context)};
    buffer = memory::tracking::make_static_array_for_overwrite<char>(data.buffer_size(),
                                                                     LEGACY_EMBEDDED_ALLOC_MSG("Allocation for the buffer"), context);
    std::copy_n(data.buffer_begin(), data.buffer_size(), buffer.data());
    output_array = memory::tracking::make_static_array_for_overwrite<cel_string_t>(
        data.size(), LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context);
    std::transform(data.begin(), data.end(), output_array.begin(),
                   [old_buffer = data.buffer_begin(), new_buffer = buffer.data()](const cel_string_t pointer) {
                     return new_buffer + (pointer - old_buffer);
                   });
  }

  return std::make_unique<typed_raw_dictionary<cel_string_t>>(std::move(output_array), std::move(buffer));
}

void typed_dictionary<cel_string_t>::set_delete_from_disk_when_destructed(const bool value) {
  string_data_->set_delete_from_disk_when_destructed(value);
}

row_id typed_dictionary<cel_string_t>::get_row_id_for(const cel_string_t v,
                                                      const common::execution_context& context) const {
  const auto data = get_const_data(context);
  // no null
  const auto* start{std::next(data.get())};
  const auto* end{std::next(data.get(), get_size())};
  const auto* value{std::lower_bound(start, end, v, cel_string_compare{})};
  if (value == end || (strcmp(v, *value) != 0)) {
    return VALUE_NOT_FOUND;
  }
  // this will return the index
  return static_cast<row_id>(std::distance(data.get(), value));
}

row_id typed_dictionary<cel_string_t>::get_row_id_for(const std::string& v,
                                                      const common::execution_context& context) const {
  const auto data = get_const_data(context);
  // no null
  const auto* start{std::next(data.get())};
  const auto* end{std::next(data.get(), get_size())};
  const auto* value{std::lower_bound(start, end, v, cel_string_compare{})};
  if (value == end || (strcmp(v.c_str(), *value) != 0)) {
    return VALUE_NOT_FOUND;
  }
  // this will return the index
  return static_cast<row_id>(std::distance(data.get(), value));
}

row_id typed_dictionary<cel_string_t>::lower_bound(const std::string& v,
                                                   const common::execution_context& context) const {
  const auto data = get_const_data(context);
  // no null
  const auto* start{std::next(data.get())};
  const auto* end{std::next(data.get(), get_size())};
  return static_cast<row_id>(std::distance(data.get(), std::lower_bound(start, end, v, cel_string_compare{})));
}

row_id typed_dictionary<cel_string_t>::upper_bound(const std::string& v,
                                                   const common::execution_context& context) const {
  const auto data = get_const_data(context);
  // no null
  const auto* start{std::next(data.get())};
  const auto* end{std::next(data.get(), get_size())};
  return static_cast<row_id>(std::distance(data.get(), std::upper_bound(start, end, v, cel_string_compare{})));
}

size_t typed_dictionary<cel_string_t>::get_size_in_memory() const { return string_data_->get_size_in_memory(); }

usage_time_t typed_dictionary<cel_string_t>::time_of_last_usage() const { return string_data_->get_last_usage(); }

// returns a list of target mapped value_ids of the other dictionary as compared to the first dictionary.
dictionary_map typed_dictionary<cel_string_t>::create_dictionary_mapping(const std::shared_ptr<dictionary>& other,
                                                                         const common::execution_context& context) {
  if (type != other->type) {
    throw common::cpm_exception{
        "create_dictionary_mapping failed because types of columns do not match, [{}] and [{}].",
        convert_to_string(type), convert_to_string(other->type)};
  }
  const std::shared_ptr<typed_dictionary<cel_string_t>> casted_other =
      std::static_pointer_cast<typed_dictionary<cel_string_t>>(other);
  return dictionary_map::create(*this, *casted_other, context);
}

std::optional<std::string> typed_dictionary<cel_string_t>::get_string_value_opt(row_id ptr) const {
  if (ptr == 0) {
    return std::nullopt;
  }

  return std::string(get_const_data().at(ptr));
}

std::string typed_dictionary<cel_string_t>::get_string_value(row_id ptr) const {
  return get_string_value_opt(ptr).value_or("NULL");
}

[[nodiscard]] dictionary_t typed_dictionary<cel_string_t>::create_dictionary(legacy_embedded_ctl::static_array<cel_string_t>&& ptr,
                                                                             legacy_embedded_ctl::static_array<char>&& buffer,
                                                                             const std::string& swap_file,
                                                                             const management::swap_info& sinfo,
                                                                             const std::string& description) {
  return std::make_shared<typed_dictionary<cel_string_t>>(management::string_data_handler::create_data_handler(
      std::move(ptr), std::move(buffer), swap_file, management::pointer_data_handler_swap_type::SWAPPED_DICTIONARY,
      sinfo, description));
}

#ifndef CELOSTAR
std::shared_ptr<dictionary> typed_dictionary<cel_string_t>::init_from_swap(const std::string& swap_file_name,
                                                                           const management::swap_info& sinfo,
                                                                           const std::string& description) {
  std::shared_ptr<management::string_data_handler> dict_swap = management::string_data_handler::init_from_swap(
      swap_file_name, sinfo, management::pointer_data_handler_swap_type::SWAPPED_DICTIONARY, description);
  if (dict_swap != nullptr) {
    std::shared_ptr<dictionary> dictionary(new typed_dictionary<cel_string_t>(dict_swap));
    return dictionary;
  }
  return std::shared_ptr<dictionary>(nullptr);
}
#endif

// These are needed for the correct linkage of the test
template class typed_dictionary<cel_boolean_t>;
template class typed_dictionary<cel_date_t>;
template class typed_dictionary<cel_float_t>;
template class typed_dictionary<cel_int_t>;
template class typed_dictionary<cel_null_t>;
template class typed_dictionary<cel_uuid_t>;

}  // namespace celonis::accelerator::memory
