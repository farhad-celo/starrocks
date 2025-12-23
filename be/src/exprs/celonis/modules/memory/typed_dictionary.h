#pragma once

#include <memory>

#include "legacy_embedded_ctl/static_array_fwd.h"
#include "modules/common/execution_context_fwd.h"
#include "modules/common/shared_types_fwd.h"
#include "modules/memory/dictionary.h"
#include "modules/memory/dictionary_fwd.h"
#include "modules/memory/management/const_data_accessor.h"
#include "modules/memory/management/pointer_data_handler.h"
#include "modules/memory/management/raw_data_handler.h"
#include "modules/memory/row_id.h"
#include "modules/memory/types.h"
#include "types/uuid/uuid_storage.h"

namespace celonis::accelerator::date {
class celonis_date_storage;
}

namespace celonis::accelerator::memory {

/**
 * The derived class from the dictionary class that holds the data_handler for a specific type T.
 */
template <typename T>
class typed_dictionary : public dictionary {
 public:
  explicit typed_dictionary(management::raw_data_handler_t<T> data_handler);

  using const_data_accessor_t = typename management::raw_data_handler<T>::const_data_accessor_t;

  [[nodiscard]] const_data_accessor_t get_const_data(const common::execution_context& context = {}) const;

  void swap_in(common::execution_context& context) override;

#ifndef CELOSTAR
  void swap_out(common::execution_context& context) override;

  bool write_out(common::execution_context& context) override;
#endif

  [[nodiscard]] management::load_status get_load_status() const override;

  [[nodiscard]] bool is_swappable() const override;

  [[nodiscard]] bool swap_file_broken() const override;

  [[nodiscard]] row_id get_size() const override;

  /**
   *
   * @param v The value for which the index(row_id) is required.
   * @param context execution context
   * @return The index(row_id) for the value v
   */
  [[nodiscard]] row_id get_row_id_for(const T& v, const common::execution_context& context) const;

  /**
   *
   * @param v Value to compare the elements to.
   * @param context The execution context.
   * @return Index pointing to the first element in the data that is not less than value v,
   * or size of the data_handler if no such element exists.
   */
  [[nodiscard]] row_id lower_bound(const T& v, const common::execution_context& context) const;

  /**
   *
   * @param v Value to compare the elements to.
   * @param context The execution context.
   * @return Index pointing to the first element in the data that is greater than value v,
   * or size of the data_handler if no such element exists.
   */
  [[nodiscard]] row_id upper_bound(const T& v, const common::execution_context& context) const;

  // returns a list of target mapped value_ids of the other dictionary as compared to the first dictionary.
  dictionary_map create_dictionary_mapping(const std::shared_ptr<dictionary>& other,
                                           const common::execution_context& context) override;

  [[nodiscard]] std::string get_string_value(row_id ptr) const override;
  [[nodiscard]] std::optional<std::string> get_string_value_opt(row_id ptr) const override;

  [[nodiscard]] size_t get_size_in_memory() const override;

  [[nodiscard]] usage_time_t time_of_last_usage() const override;

  void add_to_group(std::shared_ptr<management::managed_memory_group> managed_group) override;

  [[nodiscard]] raw_dictionary_t copy_to_raw_dictionary(common::execution_context& context) override;

  void set_delete_from_disk_when_destructed(bool value) override;

  /**
   *
   * @param data The data for the dictionary.
   * @param swap_file_name Name of the swap file.
   * @param sinfo Swap info containing swap-related meta-data.
   * @param description The description of the swap file.
   * @return A typed dictionary of datatype T.
   */
  [[nodiscard]] static dictionary_t create_dictionary(legacy_embedded_ctl::static_array<T>&& data,
                                                      const std::string& swap_file_name,
                                                      const management::swap_info& sinfo,
                                                      const std::string& description);

#ifndef CELOSTAR
  /**
   *
   * @param swap_file_name The name of the swap file.
   * @param sinfo Swap info containing swap-related meta-data.
   * @param description The description of the swap_file
   * @return The dictionary if it exists in the swap.
   */
  static std::shared_ptr<dictionary> init_from_swap(const std::string& swap_file_name,
                                                    const management::swap_info& sinfo, const std::string& description);
#endif

 private:
  management::raw_data_handler_t<T> data_handler;
};

/**
 * For string data type as we have data as well as buffer to create a dictionary and also the comparison are
 * specialized, we create a Specialization of the derived class typed_dictionary for the string data type.
 */
template <>
class typed_dictionary<cel_string_t> : public dictionary {
 public:
  explicit typed_dictionary(std::shared_ptr<management::string_data_handler> string_data);

  using const_data_accessor_t = management::string_data_handler::const_data_accessor_t;

  [[nodiscard]] const_data_accessor_t get_const_data(const common::execution_context& context = {}) const;

  void swap_in(common::execution_context& context) override;

#ifndef CELOSTAR
  void swap_out(common::execution_context& context) override;

  bool write_out(common::execution_context& context) override;
#endif

  [[nodiscard]] management::load_status get_load_status() const override;

  [[nodiscard]] bool is_swappable() const override;

  [[nodiscard]] bool swap_file_broken() const override;

  [[nodiscard]] row_id get_size() const override;

  [[nodiscard]] size_t get_string_buffer_size() const;

  void add_to_group(std::shared_ptr<management::managed_memory_group> managed_group) override;

  row_id get_row_id_for(cel_string_t v, const common::execution_context& context) const;

  [[nodiscard]] row_id get_row_id_for(const std::string& v, const common::execution_context& context) const;

  [[nodiscard]] row_id lower_bound(const std::string& v, const common::execution_context& context) const;

  [[nodiscard]] row_id upper_bound(const std::string& v, const common::execution_context& context) const;

  // returns a list of target mapped value_ids of the other dictionary as compared to the first dictionary.
  dictionary_map create_dictionary_mapping(const std::shared_ptr<dictionary>& other,
                                           const common::execution_context& context) override;

  [[nodiscard]] std::string get_string_value(row_id ptr) const override;
  [[nodiscard]] std::optional<std::string> get_string_value_opt(row_id ptr) const override;

  [[nodiscard]] std::string_view get_string_value_view(row_id ptr) const;
  [[nodiscard]] std::optional<std::string_view> get_string_value_view_opt(row_id ptr) const;

  [[nodiscard]] size_t get_size_in_memory() const override;

  [[nodiscard]] usage_time_t time_of_last_usage() const override;

  [[nodiscard]] raw_dictionary_t copy_to_raw_dictionary(common::execution_context& context) override;

  void set_delete_from_disk_when_destructed(bool value) override;

  /**
   * @param ptr Pointers to the string data for the dictionary.
   * @param str_buffer The buffer containing the string data.
   * @param swap_file Swap file for the dictionary.
   * @param sinfo Swap info containing swap-related meta-data.
   * @param description The description of the swap file
   * @return A typed dictionary of the string data type.
   */
  [[nodiscard]] static dictionary_t create_dictionary(legacy_embedded_ctl::static_array<cel_string_t>&& ptr,
                                                      legacy_embedded_ctl::static_array<char>&& buffer,
                                                      const std::string& swap_file, const management::swap_info& sinfo,
                                                      const std::string& description);

#ifndef CELOSTAR
  /**
   *
   * @param swap_file_name The name of the swap file.
   * @param sinfo Swap info containing swap-related meta-data.
   * @param description The description of the swap_file
   * @return The dictionary if it exists in the swap else nullptr.
   */
  static std::shared_ptr<dictionary> init_from_swap(const std::string& swap_file_name,
                                                    const management::swap_info& sinfo, const std::string& description);
#endif

 private:
  std::shared_ptr<management::string_data_handler> string_data_;
};

using boolean_dictionary = typed_dictionary<cel_boolean_t>;
using date_dictionary = typed_dictionary<cel_date_t>;
using float_dictionary = typed_dictionary<cel_float_t>;
using int_dictionary = typed_dictionary<cel_int_t>;
using string_dictionary = typed_dictionary<cel_string_t>;
using uuid_dictionary = typed_dictionary<cel_uuid_t>;
using null_dictionary = typed_dictionary<cel_null_t>;

using typed_dict_variant_t =
    std::variant<std::shared_ptr<boolean_dictionary>, std::shared_ptr<date_dictionary>,
                 std::shared_ptr<float_dictionary>, std::shared_ptr<int_dictionary>, std::shared_ptr<string_dictionary>,
                 std::shared_ptr<uuid_dictionary>, std::shared_ptr<null_dictionary>>;

inline typed_dict_variant_t convert_to_typed_dict(const dictionary_t& dictionary) {
  if (auto casted_dict{std::dynamic_pointer_cast<boolean_dictionary>(dictionary)}; casted_dict != nullptr) {
    return casted_dict;
  }
  if (auto casted_dict{std::dynamic_pointer_cast<date_dictionary>(dictionary)}; casted_dict != nullptr) {
    return casted_dict;
  }
  if (auto casted_dict{std::dynamic_pointer_cast<float_dictionary>(dictionary)}; casted_dict != nullptr) {
    return casted_dict;
  }
  if (auto casted_dict{std::dynamic_pointer_cast<int_dictionary>(dictionary)}; casted_dict != nullptr) {
    return casted_dict;
  }
  if (auto casted_dict{std::dynamic_pointer_cast<string_dictionary>(dictionary)}; casted_dict != nullptr) {
    return casted_dict;
  }
  if (auto casted_dict{std::dynamic_pointer_cast<uuid_dictionary>(dictionary)}; casted_dict != nullptr) {
    return casted_dict;
  }
  if (auto casted_dict{std::dynamic_pointer_cast<null_dictionary>(dictionary)}; casted_dict != nullptr) {
    return casted_dict;
  }
  legacy_embedded_ctl::assert_unreachable();
}

}  // namespace celonis::accelerator::memory
