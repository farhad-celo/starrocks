#pragma once

#include <variant>

#include "legacy_embedded_ctl/concepts.h"
#include "legacy_embedded_ctl/static_array_fwd.h"
#include "modules/memory/column_pointers.h"

namespace celonis::accelerator::common {

// simple wrapper around legacy_embedded_ctl::static_array to avoid copies since copying a non-shared static_array would make a copy of
// the underlying buffer
template <
    legacy_embedded_ctl::one_of<memory::col_ptr_8_t, memory::col_ptr_16_t, memory::col_ptr_32_t, memory::col_ptr_64_t> COL_PTR_TYPE>
class owned_column_ptr_data {
 public:
  using value_type = COL_PTR_TYPE;
  using reference = value_type&;

  owned_column_ptr_data() noexcept = default;
  explicit owned_column_ptr_data(legacy_embedded_ctl::static_array<COL_PTR_TYPE>&& ptrs) noexcept : ptrs_{std::move(ptrs)} {}

  owned_column_ptr_data(const owned_column_ptr_data& other) = delete;
  owned_column_ptr_data& operator=(const owned_column_ptr_data& other) = delete;
  owned_column_ptr_data(owned_column_ptr_data&& other) noexcept = default;
  owned_column_ptr_data& operator=(owned_column_ptr_data&& other) noexcept = default;

  [[nodiscard]] size_t size() const noexcept { return ptrs_.size(); }

  [[nodiscard]] reference operator[](size_t index) {
    legacy_embedded_debug_assert(index < ptrs_.size());
    return ptrs_[index];
  }

  [[nodiscard]] legacy_embedded_ctl::static_array<COL_PTR_TYPE> release_data() && { return std::move(ptrs_); }

 private:
  legacy_embedded_ctl::static_array<COL_PTR_TYPE> ptrs_;
};

using owned_column_ptr_data_t =
    std::variant<owned_column_ptr_data<memory::col_ptr_8_t>, owned_column_ptr_data<memory::col_ptr_16_t>,
                 owned_column_ptr_data<memory::col_ptr_32_t>, owned_column_ptr_data<memory::col_ptr_64_t>>;

[[nodiscard]] owned_column_ptr_data_t create_owned_column_data(row_id row_count, row_id dict_size,
                                                               memory::zero_init_t zero_initialize,
                                                               const common::execution_context& context);

}  // namespace celonis::accelerator::common