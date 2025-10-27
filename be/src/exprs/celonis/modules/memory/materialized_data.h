#pragma once

#include <limits>
#include <memory>
#include <sstream>

#include "legacy_embedded_ctl/static_array.h"
#include "modules/common/exceptions.h"
#include "modules/common/execution_context.h"
#include "modules/common/shared_types.h"
#include "modules/memory/management/const_bitset_data_accessor.h"
#include "modules/memory/management/managed_memory_group.h"
#include "modules/memory/management/pointer_data_handler.h"
#include "modules/memory/management/raw_data_handler.h"
#include "modules/memory/management/swappable_bitset.h"
#include "modules/memory/materialized_data_fwd.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::memory {

class materialized_data {
 public:
  [[nodiscard]] row_id get_size() const { return size; }

  std::shared_ptr<management::swappable_bitset> get_null_flags() { return null_flags; }

  [[nodiscard]] data_type get_data_type() const { return type; }

  virtual void add_to_group(std::shared_ptr<management::managed_memory_group> managed_group) = 0;

  virtual void swap_in(common::execution_context& context) = 0;

#ifndef CELOSTAR
  virtual void swap_out(common::execution_context& context) = 0;

  virtual bool write_out(common::execution_context& context) = 0;
#endif

  [[nodiscard]] virtual bool is_swappable() const = 0;

  [[nodiscard]] virtual bool swap_file_broken() const = 0;

  [[nodiscard]] virtual std::string get_string_value(row_id row, const common::execution_context& context) const = 0;

  [[nodiscard]] virtual std::optional<std::string> get_string_value_opt(
      row_id row, const common::execution_context& context) const = 0;

  [[nodiscard]] virtual management::load_status get_load_status() const = 0;

  [[nodiscard]] virtual usage_time_t time_of_last_usage() const = 0;

  virtual void set_delete_from_disk_when_destructed(bool value) = 0;

  materialized_data(materialized_data&&) = delete;
  materialized_data(const materialized_data&) = delete;
  materialized_data& operator=(materialized_data&&) = delete;
  materialized_data& operator=(const materialized_data&) = delete;
  virtual ~materialized_data() = default;

 protected:
  materialized_data(data_type type, row_id size, std::shared_ptr<management::swappable_bitset> null_flags)
      : type(type), size(size), null_flags(std::move(null_flags)) {}

  const data_type type;                                      // NOLINT(misc-non-private-member-variables-in-classes)
  const row_id size;                                         // NOLINT(misc-non-private-member-variables-in-classes)
  std::shared_ptr<management::swappable_bitset> null_flags;  // NOLINT(misc-non-private-member-variables-in-classes)
};

template <typename T>
class materialized_typed_data : public materialized_data {
 public:
  materialized_typed_data(row_id size, std::shared_ptr<management::swappable_bitset> null_flags,
                          management::raw_data_handler_t<T> data)
      : materialized_data(get_matching_data_type<T>(), size, std::move(null_flags)), data(std::move(data)) {}

  using const_data_accessor_t = typename management::raw_data_handler<T>::const_data_accessor_t;

  [[nodiscard]] const_data_accessor_t get_const_data(const common::execution_context& context = {}) const {
    return data->get_const_data(context);
  }

  void swap_in(common::execution_context& context) override {
    data->swap_in(context);
    null_flags->swap_in(context);
  }

#ifndef CELOSTAR
  void swap_out(common::execution_context& context) override {
    data->swap_out(context);
    null_flags->swap_out(context);
  }
#endif

  [[nodiscard]] bool is_swappable() const override { return data->is_swappable() && null_flags->is_swappable(); }

#ifndef CELOSTAR
  bool write_out(common::execution_context& context) override {
    data->write_out(context);
    null_flags->write_out(context);
    return true;
  }
#endif

  [[nodiscard]] std::string get_string_value(row_id row, const common::execution_context& context) const override {
    return get_string_value_opt(row, context).value_or("NULL");
  }

  [[nodiscard]] std::optional<std::string> get_string_value_opt(
      row_id row, const common::execution_context& context) const override {
    if (const auto size = get_size(); row < 0 || row >= size) {
      throw common::out_of_bounds_exception{"materialized_data::get_string_value_opt", row_id{0}, (size - 1), row};
    }
    if (null_flags->get_const_data(context)[row]) {
      return std::nullopt;
    }

    const auto value = get_const_data()[row];
    std::ostringstream converter;
    converter << std::fixed << value;
    return converter.str();
  }

  [[nodiscard]] management::load_status get_load_status() const override { return data->get_load_status(); }

  [[nodiscard]] usage_time_t time_of_last_usage() const override { return data->get_last_usage(); }

  [[nodiscard]] bool swap_file_broken() const override {
    return data->swap_file_broken() || null_flags->swap_file_broken();
  }

  void add_to_group(std::shared_ptr<management::managed_memory_group> managed_group) override {
    managed_group->add_to_group(data);
    managed_group->add_to_group(null_flags);
  }

  void set_delete_from_disk_when_destructed(const bool value) override {
    data->set_delete_from_disk_when_destructed(value);
    null_flags->set_delete_from_disk_when_destructed(value);
  }

  static std::shared_ptr<materialized_typed_data<T>> init_materialized_data(
      const std::string& id, const management::swap_info& s_info, const std::string& description, row_id row_count,
      legacy_embedded_ctl::static_array<T> data, const memory::null_flags_t& null_flags) {
    std::shared_ptr<management::swappable_bitset> bitset(management::swappable_bitset::create_data_handler(
        null_flags, id + management::NULL_FLAGS_NEW_ENDING, s_info, description + management::NULL_FLAGS_DESC));
    management::raw_data_handler_t<T> data_handler = management::raw_data_handler<T>::create_data_handler(
        std::move(data), id + management::MATERIALIZED_DATA_NEW_ENDING, s_info,
        description + management::MATERIALIZED_DATA_DESC);

    return std::make_shared<materialized_typed_data<T>>(row_count, std::move(bitset), std::move(data_handler));
  }

  static std::shared_ptr<materialized_typed_data<T>> init_materialized_data(
      const std::string& id, const management::swap_info& s_info, const std::string& description, row_id row_count,
      const legacy_embedded_ctl::shared_static_array<T>& data, const memory::null_flags_t& null_flags) {
    std::shared_ptr<management::swappable_bitset> bitset(management::swappable_bitset::create_data_handler(
        null_flags, id + management::NULL_FLAGS_NEW_ENDING, s_info, description + management::NULL_FLAGS_DESC));
    management::raw_data_handler_t<T> data_handler = management::raw_data_handler<T>::create_data_handler(
        data, id + management::MATERIALIZED_DATA_NEW_ENDING, s_info, description + management::MATERIALIZED_DATA_DESC);

    return std::make_shared<materialized_typed_data<T>>(row_count, std::move(bitset), std::move(data_handler));
  }

#ifndef CELOSTAR
  static std::shared_ptr<materialized_typed_data<T>> init_from_swap(const std::string& id,
                                                                    const management::swap_info& s_info,
                                                                    const std::string& description) {
    std::shared_ptr<management::swappable_bitset> bitset(management::swappable_bitset::init_from_swap(
        id + management::NULL_FLAGS_NEW_ENDING, s_info, description + management::NULL_FLAGS_DESC));
    if (bitset != nullptr) {
      management::raw_data_handler_t<T> data_handler = management::raw_data_handler<T>::init_from_swap(
          id + management::MATERIALIZED_DATA_NEW_ENDING, s_info, description + management::MATERIALIZED_DATA_DESC);
      if (data_handler != nullptr) {
        if (bitset->get_size() != data_handler->get_size()) {
          throw common::internal_exception{
              "Size of materialized data [{}] does not match size of materialized null flags [{}] for [{}].",
              data_handler->get_size(), bitset->get_size(), description};
        }
        if (data_handler->get_size() > static_cast<size_t>(std::numeric_limits<row_id>::max())) {
          throw common::cpm_exception{
              "Size of materialized data for [{}] exceeds the limit of [{}]. Size of materialized data for [{}] is "
              "[{}].",
              description, std::numeric_limits<row_id>::max(), description, data_handler->get_size()};
        }
        return std::make_shared<materialized_typed_data<T>>(static_cast<row_id>(data_handler->get_size()),
                                                            std::move(bitset), std::move(data_handler));
      }
    }
    return std::shared_ptr<materialized_typed_data<T>>(nullptr);
  }
#endif

  ~materialized_typed_data() override = default;

 private:
  management::raw_data_handler_t<T> data;
};

template <>
class materialized_typed_data<cel_string_t> : public materialized_data {
 public:
  materialized_typed_data(row_id size, std::shared_ptr<management::swappable_bitset> null_flags,
                          std::shared_ptr<management::string_data_handler> string_data)
      : materialized_data(data_type::cel_string, size, std::move(null_flags)), string_data(std::move(string_data)) {}

  using const_data_accessor_t = management::string_data_handler::const_data_accessor_t;

  [[nodiscard]] const_data_accessor_t get_const_data(const common::execution_context& context = {}) const {
    return string_data->get_const_data(context);
  }

  void swap_in(common::execution_context& context) override {
    string_data->swap_in(context);
    null_flags->swap_in(context);
  }

#ifndef CELOSTAR
  void swap_out(common::execution_context& context) override {
    string_data->swap_out(context);
    null_flags->swap_out(context);
  }
#endif

  [[nodiscard]] bool is_swappable() const override { return string_data->is_swappable() && null_flags->is_swappable(); }

#ifndef CELOSTAR
  bool write_out(common::execution_context& context) override {
    const bool success = string_data->write_out(context);
    null_flags->write_out(context);
    return success;
  }
#endif

  [[nodiscard]] std::string get_string_value(row_id row, const common::execution_context& context) const override {
    return get_string_value_opt(row, context).value_or("NULL");
  }

  [[nodiscard]] std::optional<std::string> get_string_value_opt(
      row_id row, const common::execution_context& context) const override {
    if (const auto size = get_size(); row < 0 || row >= size) {
      throw common::out_of_bounds_exception{"materialized_data::get_string_value_opt", row_id{0}, (size - 1), row};
    }
    if (null_flags->get_const_data(context)[row]) {
      return std::nullopt;
    }

    return std::string(get_const_data()[row]);
  }

  [[nodiscard]] management::load_status get_load_status() const override { return string_data->get_load_status(); }

  [[nodiscard]] usage_time_t time_of_last_usage() const override { return string_data->get_last_usage(); }

  [[nodiscard]] bool swap_file_broken() const override {
    return string_data->swap_file_broken() || null_flags->swap_file_broken();
  }

  void add_to_group(std::shared_ptr<management::managed_memory_group> managed_group) override {
    managed_group->add_to_group(string_data);
    managed_group->add_to_group(null_flags);
  }

  void set_delete_from_disk_when_destructed(const bool value) override {
    string_data->set_delete_from_disk_when_destructed(value);
    null_flags->set_delete_from_disk_when_destructed(value);
  }

  static std::shared_ptr<materialized_typed_data<cel_string_t>> init_materialized_data(
      const std::string& id, const management::swap_info& s_info, const std::string& description, row_id row_count,
      const legacy_embedded_ctl::shared_static_array<cel_string_t>& ptrs, size_t /*str_bfr_size*/,
      const legacy_embedded_ctl::shared_static_array<char>& string_bfr, const memory::null_flags_t& null_flags) {
    std::shared_ptr<management::swappable_bitset> bitset(management::swappable_bitset::create_data_handler(
        null_flags, id + management::NULL_FLAGS_ENDING, s_info, description + management::NULL_FLAGS_DESC));
    std::shared_ptr<management::string_data_handler> data_handler =
        management::string_data_handler::create_data_handler(
            ptrs, string_bfr, id, management::pointer_data_handler_swap_type::SWAPPED_MATERIALIZED, s_info,
            description + management::MATERIALIZED_DATA_DESC);
    return std::make_shared<materialized_typed_data<cel_string_t>>(row_count, std::move(bitset),
                                                                   std::move(data_handler));
  }

#ifndef CELOSTAR
  static std::shared_ptr<materialized_typed_data<cel_string_t>> init_from_swap(const std::string& id,
                                                                               const management::swap_info& s_info,
                                                                               const std::string& description) {
    std::shared_ptr<management::swappable_bitset> bitset(management::swappable_bitset::init_from_swap(
        id + management::NULL_FLAGS_ENDING, s_info, description + management::NULL_FLAGS_DESC));
    if (bitset != nullptr) {
      std::shared_ptr<management::string_data_handler> data_handler = management::string_data_handler::init_from_swap(
          id, s_info, management::pointer_data_handler_swap_type::SWAPPED_MATERIALIZED, description);
      if (data_handler != nullptr) {
        if (bitset->get_size() != data_handler->get_size()) {
          throw common::internal_exception{
              "Size of materialized data [{}] does not match size of materialized null flags [{}] for [{}].",
              data_handler->get_size(), bitset->get_size(), description};
        }
        if (data_handler->get_size() > static_cast<size_t>(std::numeric_limits<row_id>::max())) {
          throw common::cpm_exception{
              "Size of materialized data for [{}] exceeds the limit of [{}]. Size of materialized data for [{}] is "
              "[{}].",
              description, std::numeric_limits<row_id>::max(), description, data_handler->get_size()};
        }
        return std::make_shared<materialized_typed_data<cel_string_t>>(static_cast<row_id>(data_handler->get_size()),
                                                                       std::move(bitset), std::move(data_handler));
      }
    }
    return std::shared_ptr<materialized_typed_data<cel_string_t>>(nullptr);
  }
#endif

  ~materialized_typed_data() override = default;

  [[nodiscard]] size_t get_str_buf_size() const { return string_data->get_pointer_buffer_size(); }

 private:
  std::shared_ptr<management::string_data_handler> string_data;
};
}  // namespace celonis::accelerator::memory
