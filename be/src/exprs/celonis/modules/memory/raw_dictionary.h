#pragma once

#include <legacy_embedded_ctl/static_array.h>

#include "modules/common/shared_types.h"
#include "modules/memory/data_array_types.h"
#include "modules/memory/management/pointer_data_handler.h"
#include "modules/memory/management/raw_data_handler.h"
#include "modules/memory/typed_dictionary.h"
#include "raw_dictionary_fwd.h"

namespace celonis::accelerator::memory {

class raw_dictionary {
 public:
  explicit raw_dictionary(const data_type type) : type_(type) {}
  [[nodiscard]] data_type get_type() const noexcept { return type_; }
  virtual ~raw_dictionary() = default;
  [[nodiscard]] virtual size_t get_size() const noexcept = 0;
  [[nodiscard]] virtual dictionary_t convert_to_dictionary_t_release_data(const std::string& swap_file,
                                                                          const management::swap_info& sinfo,
                                                                          const std::string& description) = 0;
  [[nodiscard]] virtual data_array_types_t release_data_variant() && noexcept = 0;

 private:
  data_type type_;
};

using raw_dictionary_t = std::unique_ptr<raw_dictionary>;

template <class T>
class typed_raw_dictionary : public raw_dictionary {
 public:
  explicit typed_raw_dictionary(legacy_embedded_ctl::static_array<T>&& data)
      : raw_dictionary(get_matching_data_type<T>()), data_(std::move(data)) {}
  legacy_embedded_ctl::static_array<T>&& release_data() && noexcept { return std::move(data_); }
  dictionary_t convert_to_dictionary_t_release_data(const std::string& swap_file, const management::swap_info& sinfo,
                                                    const std::string& description) final {
    auto handler = management::raw_data_handler<T>::create_data_handler(
        std::move(data_), swap_file + management::DICT_ENDING, sinfo, description);

    return std::make_shared<typed_dictionary<T>>(handler);
  }
  [[nodiscard]] data_array_types_t release_data_variant() && noexcept final { return std::move(data_); }
  [[nodiscard]] size_t get_size() const noexcept final { return data_.size(); }

 private:
  legacy_embedded_ctl::static_array<T> data_;
};

template <>
class typed_raw_dictionary<cel_string_t> : public raw_dictionary {
 public:
  typed_raw_dictionary(legacy_embedded_ctl::static_array<cel_string_t>&& data,
                       legacy_embedded_ctl::static_array<char>&& buffer)
      : raw_dictionary{data_type::cel_string}, data_{std::move(data)}, buffer_{std::move(buffer)} {}

  [[nodiscard]] legacy_embedded_ctl::static_array<cel_string_t>&& release_data() && noexcept {
    return std::move(data_);
  }
  [[nodiscard]] legacy_embedded_ctl::static_array<char>&& release_buffer() && noexcept { return std::move(buffer_); }
  [[nodiscard]] size_t buffer_size() const noexcept { return buffer_.size(); }
  [[nodiscard]] dictionary_t convert_to_dictionary_t_release_data(const std::string& swap_file,
                                                                  const management::swap_info& sinfo,
                                                                  const std::string& description) final {
    const auto handler{management::string_data_handler::create_data_handler(
        std::move(data_), std::move(buffer_), swap_file, management::pointer_data_handler_swap_type::SWAPPED_DICTIONARY,
        sinfo, description)};

    return std::make_shared<typed_dictionary<cel_string_t>>(handler);
  }
  [[nodiscard]] data_array_types_t release_data_variant() && noexcept final { return std::move(data_); }
  [[nodiscard]] size_t get_size() const noexcept final { return data_.size(); }

 private:
  legacy_embedded_ctl::static_array<cel_string_t> data_;
  legacy_embedded_ctl::static_array<char> buffer_;
};
}  // namespace celonis::accelerator::memory
