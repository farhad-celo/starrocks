#include "join_data_handler.h"

#include <ctl/static_array.h>

namespace celonis::accelerator::memory {

namespace {

template <typename JOIN_TYPE>
join_raw_t create_raw_join_impl(row_id row_count, zero_init_t initialize_to_0,
                                [[maybe_unused]] const common::execution_context& context) {
  if (row_count < 0) {
    throw common::internal_exception{"No negative row count allowed but is [{}]", row_count};
  }

  auto data{initialize_to_0.get()
                ? ctl::make_shared_static_array_value_init<JOIN_TYPE>(row_count, ALLOC_MSG(ctl::RETURN_VALUE_MSG))
                : ctl::make_shared_static_array_for_overwrite<JOIN_TYPE>(row_count, ALLOC_MSG(ctl::RETURN_VALUE_MSG))};
  return data;
}

}  // namespace

join_raw_t create_raw_join(row_id fact_table_size, row_id dim_table_size, zero_init_t initialize_to_0,
                           const common::execution_context& context) {
  if (dim_table_size > std::numeric_limits<join_32_t>::max()) {
    return create_raw_join_impl<join_64_t>(fact_table_size, initialize_to_0, context);
  }
  return create_raw_join_impl<join_32_t>(fact_table_size, initialize_to_0, context);
}

join_data_handler_t create_join_from_raw_data(const join_raw_t& raw_join, const std::string& description) {
  return cast_execute_join(
      [&description](const auto& join) { return join_data_handler_t{create_join_from_raw_data(join, description)}; },
      raw_join);
}

size_t get_join_vector_size(const join_data_handler_t& join) {
  return cast_execute_join([](const auto& join) { return join->get_size(); }, join);
}

size_t get_join_vector_size(const join_raw_t& join) {
  return cast_execute_join([](const auto& join) { return join.size(); }, join);
}

}  // namespace celonis::accelerator::memory