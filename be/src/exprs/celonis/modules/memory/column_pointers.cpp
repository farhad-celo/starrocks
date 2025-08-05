#include "column_pointers.h"

#include <memory>

#include "legacy_embedded_ctl/assert.h"
#ifndef CELOSTAR
#include "modules/memory/cache/column_register.h"
#include "modules/memory/table.h"
#endif

namespace celonis::accelerator::memory {

namespace {
template <class COL_PTR_TYPE>
[[nodiscard]] column_ptrs_t create_column_pointers(const raw_column_ptrs_impl<COL_PTR_TYPE>* raw_column_pointers,
                                                   const std::string& cache_id, const std::string& cache_description,
                                                   const management::swap_info& sinfo) {
  auto column_pointer_handle = management::raw_data_handler<COL_PTR_TYPE>::create_data_handler(
      raw_column_pointers->get_data(), cache_id + management::COLUMN_PTR_ENDING, sinfo,
      cache_description + management::COLUMN_PTR_DESC);
  return std::make_shared<column_ptrs_impl<COL_PTR_TYPE>>(column_pointer_handle);
}

}  // namespace

size_t type_to_size(col_pointer_type type) {
  switch (type) {
    case col_pointer_type::PTR_8:
      return sizeof(details::col_ptr_enum_to_type<col_pointer_type::PTR_8>::type);
    case col_pointer_type::PTR_16:
      return sizeof(details::col_ptr_enum_to_type<col_pointer_type::PTR_16>::type);
    case col_pointer_type::PTR_32:
      return sizeof(details::col_ptr_enum_to_type<col_pointer_type::PTR_32>::type);
    case col_pointer_type::PTR_64:
      return sizeof(details::col_ptr_enum_to_type<col_pointer_type::PTR_64>::type);
  }
  // Should not be reached
  throw common::internal_exception{"unknown type"};
}

column_ptrs_t create_column_pointers(const raw_column_ptrs_t& raw_column_pointers, const std::string& cache_id,
                                     const std::string& cache_description, const management::swap_info& sinfo) {
  if constexpr (COL_PTR_64_NEEDED) {
    if (raw_column_pointers->get_type() == col_pointer_type::PTR_64) {
      return create_column_pointers(dynamic_cast<raw_column_ptrs_impl<col_ptr_64_t>*>(raw_column_pointers.get()),
                                    cache_id, cache_description, sinfo);
    }
  }
  if (raw_column_pointers->get_type() == col_pointer_type::PTR_32) {
    return create_column_pointers(dynamic_cast<raw_column_ptrs_impl<col_ptr_32_t>*>(raw_column_pointers.get()),
                                  cache_id, cache_description, sinfo);
  }
  if (raw_column_pointers->get_type() == col_pointer_type::PTR_16) {
    return create_column_pointers(dynamic_cast<raw_column_ptrs_impl<col_ptr_16_t>*>(raw_column_pointers.get()),
                                  cache_id, cache_description, sinfo);
  }
  return create_column_pointers(dynamic_cast<raw_column_ptrs_impl<col_ptr_8_t>*>(raw_column_pointers.get()), cache_id,
                                cache_description, sinfo);
}

raw_column_ptrs_t create_raw_column_pointer(const row_id row_count, const row_id dict_size_with_null,
                                            const zero_init_t initialize_to_0,
                                            const common::execution_context& context) {
  if constexpr (COL_PTR_64_NEEDED) {
    if (dict_size_with_null - 1 > std::numeric_limits<col_ptr_32_t>::max()) {
      return create_raw_column_pointer<col_ptr_64_t>(row_count, initialize_to_0, context);
    }
  }
  if (dict_size_with_null - 1 > std::numeric_limits<col_ptr_16_t>::max()) {
    return create_raw_column_pointer<col_ptr_32_t>(row_count, initialize_to_0, context);
  }
  if (dict_size_with_null - 1 > std::numeric_limits<col_ptr_8_t>::max()) {
    return create_raw_column_pointer<col_ptr_16_t>(row_count, initialize_to_0, context);
  }
  return create_raw_column_pointer<col_ptr_8_t>(row_count, initialize_to_0, context);
}

namespace {
template <class COL_PTR_TYPE>
column_ptrs_t create_tmp_column_pointers(const raw_column_ptrs_impl<COL_PTR_TYPE>* raw_column_pointers) {
  auto column_pointer_handle{management::raw_data_handler<COL_PTR_TYPE>::create_data_handler(
      raw_column_pointers->get_data(), "TMP", management::no_swap(), "Temp Raw Pointers")};
  return std::make_shared<column_ptrs_impl<COL_PTR_TYPE>>(std::move(column_pointer_handle));
}
}  // namespace

column_ptrs_t create_tmp_column_pointers(const raw_column_ptrs_t& raw_column_pointers) {
  if constexpr (COL_PTR_64_NEEDED) {
    if (raw_column_pointers->get_type() == col_pointer_type::PTR_64) {
      return create_tmp_column_pointers(dynamic_cast<raw_column_ptrs_impl<col_ptr_64_t>*>(raw_column_pointers.get()));
    }
  }
  if (raw_column_pointers->get_type() == col_pointer_type::PTR_32) {
    return create_tmp_column_pointers(dynamic_cast<raw_column_ptrs_impl<col_ptr_32_t>*>(raw_column_pointers.get()));
  }
  if (raw_column_pointers->get_type() == col_pointer_type::PTR_16) {
    return create_tmp_column_pointers(dynamic_cast<raw_column_ptrs_impl<col_ptr_16_t>*>(raw_column_pointers.get()));
  }
  return create_tmp_column_pointers(dynamic_cast<raw_column_ptrs_impl<col_ptr_8_t>*>(raw_column_pointers.get()));
}

template <class COL_PTRS_TYPE>
raw_immutable_column_ptrs_t column_ptrs_impl<COL_PTRS_TYPE>::create_immutable_view(size_t offset, size_t size) const {
  legacy_embedded_debug_assert(offset + size <= this->get_row_count());
  return std::make_shared<raw_immutable_column_ptrs_impl<COL_PTRS_TYPE>>(get()->get_const_data().shared(), offset,
                                                                         size);
}

#ifndef CELOSTAR
template <typename COL_PTRS_TYPE>
column_ptrs_t raw_column_ptrs_impl<COL_PTRS_TYPE>::create_cache_column_pointer(
    const cache::column_register& column_register) {
  return column_register.create_column_pointers(data_);
}
#endif

template <typename COL_PTRS_TYPE>
column_ptrs_t raw_column_ptrs_impl<COL_PTRS_TYPE>::create_temp_column_pointer() {
  return std::make_shared<column_ptrs_impl<COL_PTRS_TYPE>>(
      memory::management::raw_data_handler<COL_PTRS_TYPE>::create_temp_data_handler(data_));
}

template <class COL_PTRS_TYPE>
raw_immutable_column_ptrs_t raw_column_ptrs_impl<COL_PTRS_TYPE>::as_immutable() const {
  return create_immutable_view(0, get_row_count());
}

template <class COL_PTRS_TYPE>
raw_immutable_column_ptrs_t raw_column_ptrs_impl<COL_PTRS_TYPE>::create_immutable_view(size_t offset,
                                                                                       size_t size) const {
  legacy_embedded_debug_assert(offset + size <= this->get_row_count());
  return raw_immutable_column_ptrs_t{
      new raw_immutable_column_ptrs_impl<COL_PTRS_TYPE>{data_, static_cast<row_id>(offset), static_cast<row_id>(size)}};
}

template <class COL_PTRS_TYPE>
raw_column_ptrs_t raw_column_ptrs_impl<COL_PTRS_TYPE>::create_view(size_t offset, size_t size) const {
  legacy_embedded_debug_assert(offset + size <= this->get_row_count());
  return raw_column_ptrs_t{
      new raw_column_ptrs_impl<COL_PTRS_TYPE>{data_, static_cast<row_id>(offset), static_cast<row_id>(size), context}};
}

template <class COL_PTRS_TYPE>
raw_immutable_column_ptrs_t raw_immutable_column_ptrs_impl<COL_PTRS_TYPE>::create_immutable_view(size_t offset,
                                                                                                 size_t size) const {
  legacy_embedded_debug_assert(offset + size <= get_row_count());
  return raw_immutable_column_ptrs_t{
      new raw_immutable_column_ptrs_impl<COL_PTRS_TYPE>{data_, static_cast<row_id>(offset), static_cast<row_id>(size)}};
}

#ifdef CELOSTAR
// TODO(j.kim): Locate where it is instantiated in cpm-query-engine.
template class column_ptrs_impl<col_ptr_64_t>;
#endif

}  // namespace celonis::accelerator::memory
