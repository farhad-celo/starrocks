#pragma once

#include <variant>

#include "modules/memory/column_pointers.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::memory {

/* Const accessor that allows as fast as possible virtual column pointer access. Compared to the get_ptr_slow member
 * function of column_ptrs_abstract, this will not create a const data accessor (which might need to do swap in of data)
 * with every access, but just do it once using RAII. The column pointer data then stays in memory until this class is
 * destroyed. */
class const_abstract_column_ptrs_accessor {
 public:
  const_abstract_column_ptrs_accessor(const memory::column_ptrs_abstract& column)  // NOLINT
      : col_ptrs_variant_{memory::cast_execute_column_pointers(
            [](const auto& t) { return col_ptrs_variant_type{std::get<0>(t).get_const_accessor()}; }, column)} {}

  const_abstract_column_ptrs_accessor(const memory::raw_column_ptrs_abstract& column)  // NOLINT
      : col_ptrs_variant_{memory::cast_execute_column_pointers(
            [](const auto& t) { return col_ptrs_variant_type{std::get<0>(t).get_const_accessor()}; }, column)} {}

  [[nodiscard]] row_id operator[](int64_t i) const {
    return std::visit([i](auto& a) { return static_cast<row_id>(a[i]); }, col_ptrs_variant_);
  }

  [[nodiscard]] row_id size() const {
    return std::visit([](auto& a) { return static_cast<row_id>(a.size()); }, col_ptrs_variant_);
  }

  template <class VISITOR>
  void visit(const VISITOR& visitor) const {
    std::visit([&visitor](auto& a) { visitor(a); }, col_ptrs_variant_);
  }

 private:
  using col_ptrs_variant_type = std::variant<memory::raw_column_ptrs_impl<memory::col_ptr_8_t>::const_data_accessor_t,
                                             memory::raw_column_ptrs_impl<memory::col_ptr_16_t>::const_data_accessor_t,
                                             memory::raw_column_ptrs_impl<memory::col_ptr_32_t>::const_data_accessor_t
#ifdef ROW_ID_64
                                             ,
                                             memory::raw_column_ptrs_impl<memory::col_ptr_64_t>::const_data_accessor_t
#endif
                                             >;

  col_ptrs_variant_type col_ptrs_variant_;
};

}  // namespace celonis::accelerator::memory
