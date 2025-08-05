#include "join_data_handler.h"

#include "legacy_embedded_ctl/static_array.h"
#ifndef CELOSTAR
#include "modules/io/storage_manager.h"
#endif
#include "modules/memory/management/swap_info.h"
#include "modules/memory/tracking/static_array_with_context_tracking.h"

namespace celonis::accelerator::memory {

namespace {

template <typename JOIN_TYPE>
join_raw_t create_raw_join_impl(row_id row_count, zero_init_t initialize_to_0,
                                const common::execution_context& context) {
  if (row_count < 0) {
    throw common::internal_exception{"No negative row count allowed but is [{}]", row_count};
  }

  auto data{initialize_to_0.get() ? memory::tracking::make_shared_static_array_value_init<JOIN_TYPE>(
                                        row_count, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG), context)
                                  : memory::tracking::make_shared_static_array_for_overwrite<JOIN_TYPE>(
                                        row_count, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::RETURN_VALUE_MSG), context)};
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

#ifndef CELOSTAR
std::optional<join_data_handler_t> create_join_from_swap(const std::string& swap_file,
                                                         const management::swap_info& sinfo,
                                                         const std::string& description) {
  const io::storage_manager& sm = sinfo.storage_manager();
  const auto data_type{sm.read_swap_data_type(swap_file, sinfo)};
  if (!data_type.has_value()) {
    return std::nullopt;
  }

  if (*data_type == io::swap_data_types::INT32) {
    return management::raw_data_handler<join_32_t>::init_from_swap(swap_file, sinfo, description);
  }
  if (*data_type == io::swap_data_types::INT64) {
    return management::raw_data_handler<join_64_t>::init_from_swap(swap_file, sinfo, description);
  }
  throw common::internal_exception{"The join swap file [{}] has type {} which is not an integer type.", swap_file,
                                   static_cast<int>(*data_type)};
}
#endif

join_data_handler_t create_join_from_raw_data(const join_raw_t& raw_join, const std::string& file_name,
                                              const std::string& description, const management::swap_info& sinfo) {
  return cast_execute_join(
      [&file_name, &description, &sinfo](const auto& join) {
        return join_data_handler_t{create_join_from_raw_data(join, file_name, description, sinfo)};
      },
      raw_join);
}

size_t get_join_vector_size(const join_data_handler_t& join) {
  return cast_execute_join([](const auto& join) { return join->get_size(); }, join);
}

size_t get_join_vector_size(const join_raw_t& join) {
  return cast_execute_join([](const auto& join) { return join.size(); }, join);
}

}  // namespace celonis::accelerator::memory