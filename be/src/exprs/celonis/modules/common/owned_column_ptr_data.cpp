#include "owned_column_ptr_data.h"

#include <limits>

namespace celonis::accelerator::common {

namespace {

template <typename COL_PTR_TYPE>
owned_column_ptr_data<COL_PTR_TYPE> make_array(row_id row_count, memory::zero_init_t zero_initialize,
                                               const common::execution_context& context) {
  return zero_initialize.get()
             ? owned_column_ptr_data<COL_PTR_TYPE>{memory::tracking::make_static_array_value_init<COL_PTR_TYPE>(
                   row_count, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)}
             : owned_column_ptr_data<COL_PTR_TYPE>{memory::tracking::make_static_array_for_overwrite<COL_PTR_TYPE>(
                   row_count, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RAW_DATA_ALLOC_MSG), context)};
}

}  // namespace

[[nodiscard]] owned_column_ptr_data_t create_owned_column_data(row_id row_count, row_id dict_size,
                                                               memory::zero_init_t zero_initialize,
                                                               const common::execution_context& context) {
  if constexpr (memory::COL_PTR_64_NEEDED) {
    if (dict_size - 1 > std::numeric_limits<memory::col_ptr_32_t>::max()) {
      return {make_array<memory::col_ptr_64_t>(row_count, zero_initialize, context)};
    }
  }

  if (dict_size - 1 > std::numeric_limits<memory::col_ptr_16_t>::max()) {
    return make_array<memory::col_ptr_32_t>(row_count, zero_initialize, context);
  }

  if (dict_size - 1 > std::numeric_limits<memory::col_ptr_8_t>::max()) {
    return make_array<memory::col_ptr_16_t>(row_count, zero_initialize, context);
  }
  return make_array<memory::col_ptr_8_t>(row_count, zero_initialize, context);
}

}  // namespace celonis::accelerator::common
