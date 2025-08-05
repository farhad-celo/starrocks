#pragma once

#include <memory>
#include <type_traits>

#include "legacy_embedded_ctl/assert.h"
#include "legacy_embedded_ctl/static_array.h"
#include "modules/common/int_types.h"
#include "modules/common/shared_types.h"

namespace celonis::accelerator::memory::management {

template <typename T, typename Enabled = void>
class const_data_accessor;

template <typename T>
class const_data_accessor<
    T, typename std::enable_if_t<!std::is_pointer_v<T> && !std::is_array_v<T> && !std::is_reference_v<T>>>
    final {
 private:
  using value_type_t = T;

 public:
  using const_iterator = const value_type_t*;

  explicit const_data_accessor(legacy_embedded_ctl::shared_static_array<const value_type_t> ptr_data)
      : ptr_data_{std::move(ptr_data)} {}

  [[nodiscard]] value_type_t operator[](const size_t idx) const { return ptr_data_[idx]; }
  [[nodiscard]] value_type_t at(const size_t idx) const { return ptr_data_.at(idx); }

  [[nodiscard]] size_t size() const noexcept { return ptr_data_.size(); }

  [[nodiscard]] const_iterator begin() const noexcept { return ptr_data_.get(); }

  [[nodiscard]] const_iterator end() const noexcept { return ptr_data_.get() + ptr_data_.size(); }

  [[nodiscard]] const value_type_t* get() const noexcept { return ptr_data_.get(); }

  /**
   * @deprecated use the const_data_accessor instance instead
   */
  [[nodiscard]] legacy_embedded_ctl::shared_static_array<const value_type_t> shared() const noexcept { return ptr_data_; }

 private:
  legacy_embedded_ctl::shared_static_array<const value_type_t> ptr_data_;
};

template <typename T>
class const_data_accessor<
    T, typename std::enable_if_t<std::is_pointer_v<T> && !std::is_pointer_v<std::remove_pointer_t<T>> &&
                                 !std::is_array_v<std::remove_pointer_t<T>> &&
                                 !std::is_reference_v<std::remove_pointer_t<T>>>>
    final {
 private:
  using storage_type_t = std::remove_pointer_t<T>;
  using value_type_t = const storage_type_t*;

 public:
  using const_iterator = const value_type_t*;

  const_data_accessor(legacy_embedded_ctl::shared_static_array<const storage_type_t> pointer_buffer_data,
                      legacy_embedded_ctl::shared_static_array<const value_type_t> ptr_data)
      : pointer_buffer_data_{std::move(pointer_buffer_data)}, ptr_data_{std::move(ptr_data)} {}

  [[nodiscard]] value_type_t operator[](const size_t idx) const {
    legacy_embedded_debug_assert(validate(idx));
    return ptr_data_[idx];
  }

  [[nodiscard]] value_type_t at(const size_t idx) const {
    legacy_embedded_debug_assert(validate(idx));
    return ptr_data_.at(idx);
  }

  [[nodiscard]] bool validate(const size_t idx) const {
    const auto* ptr{ptr_data_.at(idx)};
    const auto* buffer_begin{pointer_buffer_data_.get()};
    const auto* buffer_end{pointer_buffer_data_.get() + pointer_buffer_data_.size()};
    if constexpr (std::is_same_v<T, cel_string_t>) {
      return ptr >= buffer_begin && ptr < buffer_end;
    } else {
      if (pointer_buffer_data_.size() == 0) {
        return ptr == buffer_begin;
      }
      return ptr >= buffer_begin && ptr < buffer_end;
    }
  }

  [[nodiscard]] size_t size() const noexcept { return ptr_data_.size(); }

  [[nodiscard]] const_iterator begin() const noexcept { return ptr_data_.get(); }

  [[nodiscard]] const_iterator end() const noexcept { return ptr_data_.get() + ptr_data_.size(); }

  [[nodiscard]] const value_type_t* get() const noexcept { return ptr_data_.get(); }

  [[nodiscard]] size_t buffer_size() const noexcept { return pointer_buffer_data_.size(); }

  [[nodiscard]] const storage_type_t* buffer_begin() const noexcept { return pointer_buffer_data_.get(); }

  [[nodiscard]] const storage_type_t* buffer_end() const noexcept { return std::next(buffer_begin(), buffer_size()); }

  [[nodiscard]] const storage_type_t* buffer_get() const noexcept { return buffer_begin(); }

  [[nodiscard]] legacy_embedded_ctl::shared_static_array<const value_type_t> data_shared() const noexcept { return ptr_data_; }

  [[nodiscard]] legacy_embedded_ctl::shared_static_array<const storage_type_t> buffer_shared() const noexcept {
    return pointer_buffer_data_;
  }

 private:
  legacy_embedded_ctl::shared_static_array<const storage_type_t> pointer_buffer_data_;
  legacy_embedded_ctl::shared_static_array<const value_type_t> ptr_data_;
};

}  // namespace celonis::accelerator::memory::management
