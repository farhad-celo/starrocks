#pragma once

#include <cstring>
#include <memory>
#include <string>
#include <type_traits>

#include <ctl/assert.h>
#include <ctl/static_array.h>

#include "modules/common/shared_types.h"
#include "modules/memory/dictionary_fwd.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/raw_dictionary_fwd.h"
#include "modules/memory/row_id.h"
#include "modules/memory/types.h"

namespace celonis::accelerator::memory {
// forward declare for "dictionary_map"
class dictionary_map;

/**
 * Abstract class for the dictionary containing the data type of the dictionary. The typed dictionary
 * containing the data handler of a concrete type inherits from the dictionary class.
 */
class dictionary {
 public:
  const data_type type;  // NOLINT(misc-non-private-member-variables-in-classes)

  /**
   * Adds a data_handler to data_handler group.
   *
   * @param managed_group managed_memory_group groups multiple data handlers together.
   */
  virtual void add_to_group(std::shared_ptr<management::managed_memory_group> managed_group) = 0;

  [[nodiscard]] virtual management::load_status get_load_status() const = 0;

  [[nodiscard]] virtual row_id get_size() const = 0;

  [[nodiscard]] virtual size_t get_size_in_memory() const = 0;

  [[nodiscard]] virtual usage_time_t time_of_last_usage() const = 0;

  /**
   * Creates a list of target mapped value_ids of the other dictionary as compared to the first dictionary.
   *
   * @param other The second dictionary to create target mapping compared to the first dictionary.
   * @return a list of target mapped value_ids of the other dictionary as compared to the first dictionary.
   */
  virtual dictionary_map create_dictionary_mapping(const std::shared_ptr<dictionary>& other,
                                                   const common::execution_context& context) = 0;

  [[nodiscard]] virtual std::string get_string_value(row_id ptr) const = 0;
  [[nodiscard]] virtual std::optional<std::string> get_string_value_opt(row_id ptr) const = 0;

  [[nodiscard]] virtual raw_dictionary_t copy_to_raw_dictionary(common::execution_context& context) = 0;

  dictionary(const dictionary&) = delete;
  dictionary& operator=(const dictionary&) = delete;
  dictionary(dictionary&&) = delete;
  dictionary& operator=(dictionary&&) = delete;
  virtual ~dictionary() = default;

  static inline bool is_null(row_id row_ptr) { return row_ptr == 0; }

 protected:
  explicit dictionary(data_type type) : type(type) {}
};

/**
 * Class to compare string values in different combinations of cel_string_t and std::string
 */
class cel_string_compare {
  static constexpr auto NOEXCEPT_STRCMP =
      noexcept(std::strcmp(std::declval<cel_string_t>(), std::declval<cel_string_t>()));

 public:
  bool operator()(const cel_string_t& it_value, const cel_string_t& compare_value) const noexcept(NOEXCEPT_STRCMP) {
    return std::strcmp(it_value, compare_value) < 0;
  }
  bool operator()(const std::string& it_value, const cel_string_t& compare_value) const noexcept(NOEXCEPT_STRCMP) {
    return std::strcmp(it_value.c_str(), compare_value) < 0;
  }
  bool operator()(const cel_string_t& it_value, const std::string& compare_value) const noexcept(NOEXCEPT_STRCMP) {
    return std::strcmp(it_value, compare_value.c_str()) < 0;
  }
};

/**
 * Class to store the information on the mapping of value ids from one dictionary to other.
 */
class dictionary_map {
 public:
  dictionary_map(dictionary_map&) = delete;

  dictionary_map& operator=(dictionary_map&) = delete;

  dictionary_map(dictionary_map&& map) noexcept = default;

  dictionary_map& operator=(dictionary_map&& other) noexcept = default;

  ~dictionary_map() = default;

  template <typename T>
  static dictionary_map create(const typed_dictionary<T>& source_dict, const typed_dictionary<T>& target_dict,
                               const common::execution_context& context);

  // getters
  [[nodiscard]] bool is_mapped(row_id source_idx) const noexcept {
    debug_assert(source_idx < source_size());
    return (lookup_[source_idx] != VALUE_NOT_FOUND);
  }

  [[nodiscard]] row_id get_mapping(row_id source_idx) const noexcept {
    debug_assert(source_idx < source_size());
    return lookup_[source_idx];
  }

  [[nodiscard]] row_id source_size() const noexcept { return static_cast<row_id>(lookup_.size()); }

  [[nodiscard]] row_id target_size() const noexcept { return target_size_; }

 private:
  explicit dictionary_map(ctl::static_array<row_id>&& lookup, row_id target_size)
      : lookup_{std::move(lookup)}, target_size_{target_size} {
    debug_assert(!lookup_.empty());
    debug_assert(lookup_[0] == 0);
  }

  ctl::static_array<row_id> lookup_;
  row_id target_size_;
};

}  // namespace celonis::accelerator::memory
