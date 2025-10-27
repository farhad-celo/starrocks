#include "variant_trace_utils.h"

#include <algorithm>

#include "modules/common/int_types.h"
#include "modules/memory/cache/variant_trace_cache.h"

namespace celonis::accelerator::cube {

namespace details {

namespace {

// Since the trace lengths are stored in an extra raw data handler, it's impossible to have a template
// specialization for pointer_data_handler<trace_type> to sort by itself. Thus, we sort the trace pointer
// data handlers explicitly here.
[[nodiscard]] legacy_embedded_ctl::static_array<trace_buffer_type> sort_trace_buffer(
    legacy_embedded_ctl::static_array<trace_type>& traces,
    const legacy_embedded_ctl::static_array<trace_length_type>& trace_lengths, const size_t trace_buffer_size) {
  auto sorted_trace_buffer{legacy_embedded_ctl::make_static_array_for_overwrite<trace_buffer_type>(
      trace_buffer_size, LEGACY_EMBEDDED_ALLOC_MSG(legacy_embedded_ctl::TEMPORARY_STORAGE_MSG))};

  auto* buffer_ptr{sorted_trace_buffer.data()};
  for (size_t i{0}; i < traces.size(); ++i) {
    const auto* start_ptr{buffer_ptr};
    buffer_ptr = std::copy_n(traces[i], trace_lengths[i], buffer_ptr);
    traces[i] = start_ptr;
  }

  return sorted_trace_buffer;
}

template <typename COL_PTRS_TYPE>
[[nodiscard]] memory::column_ptrs_t create_non_cached_column_ptrs_from_typed_owned_data_internal(
    common::owned_column_ptr_data<COL_PTRS_TYPE> col_ptr_data) {
  auto temporary_col_ptrs_handler{memory::management::raw_data_handler<COL_PTRS_TYPE>::create_temp_data_handler(
      std::move(col_ptr_data).release_data())};
  return std::make_shared<memory::column_ptrs_impl<COL_PTRS_TYPE>>(std::move(temporary_col_ptrs_handler));
}

template <typename COL_PTRS_TYPE>
[[nodiscard]] memory::column_ptrs_t create_cached_column_ptrs_from_typed_owned_data_internal(
    common::owned_column_ptr_data<COL_PTRS_TYPE> col_ptr_data, const cached_variants::caching_meta_data& caching_data) {
  auto col_ptrs_handler{memory::management::raw_data_handler<COL_PTRS_TYPE>::create_data_handler(
      std::move(col_ptr_data).release_data(), caching_data.cache_id + memory::management::COLUMN_PTR_ENDING,
      caching_data.swap_info, caching_data.description + " " + memory::management::COLUMN_PTR_DESC)};
  return std::make_shared<memory::column_ptrs_impl<COL_PTRS_TYPE>>(std::move(col_ptrs_handler));
}

}  // anonymous namespace

namespace non_cached_variants {

trace_handlers create_trace_handlers_internal(trace_array_t traces, trace_buffer_array_t trace_buffer,
                                              trace_length_array_t trace_lengths) {
  trace_buffer = sort_trace_buffer(traces, trace_lengths, trace_buffer.size());
  auto temp_data_handler{memory::management::pointer_data_handler<trace_type>::create_temp_data_handler(
      std::move(traces), std::move(trace_buffer))};
  auto temp_trace_lengths_data_handler{
      memory::management::raw_data_handler<trace_length_type>::create_temp_data_handler(std::move(trace_lengths))};
  return {std::move(temp_data_handler), std::move(temp_trace_lengths_data_handler)};
}

memory::column_ptrs_t create_column_ptrs_from_owned_data_internal(
    common::owned_column_ptr_data_t owned_column_ptr_data) {
  // consumes group_row_to_trace to create actual column pointers
  return std::visit(
      [](auto array) { return create_non_cached_column_ptrs_from_typed_owned_data_internal(std::move(array)); },
      std::move(owned_column_ptr_data));
}
}  // namespace non_cached_variants

namespace cached_variants {

trace_handlers create_trace_handlers_internal(trace_array_t traces, trace_buffer_array_t trace_buffer,
                                              trace_length_array_t trace_lengths,
                                              const caching_meta_data& caching_data) {
  // The flag SWAPPED_MATERIALIZED indicates no sorting on the fly will be done during writing the data to the swap
  // file.
  trace_buffer = sort_trace_buffer(traces, trace_lengths, trace_buffer.size());

  const auto& [_, cache_id, swap_info, description]{caching_data};

  auto data_handler{memory::management::pointer_data_handler<trace_type>::create_data_handler(
      std::move(traces), std::move(trace_buffer), cache_id,
      memory::management::pointer_data_handler_swap_type::SWAPPED_MATERIALIZED, swap_info, description)};
  auto trace_lengths_data_handler{memory::management::raw_data_handler<trace_length_type>::create_data_handler(
      std::move(trace_lengths), cache_id + ".lengths", swap_info, description + " Lengths")};
  return {std::move(data_handler), std::move(trace_lengths_data_handler)};
}

memory::column_ptrs_t create_column_ptrs_from_owned_data_internal(common::owned_column_ptr_data_t owned_column_ptr_data,
                                                                  const caching_meta_data& caching_data) {
  // consumes group_row_to_trace to create actual column pointers
  return std::visit(
      [&caching_data](auto array) {
        return create_cached_column_ptrs_from_typed_owned_data_internal(std::move(array), caching_data);
      },
      std::move(owned_column_ptr_data));
}
}  // namespace cached_variants

}  // namespace details

memory::cache::variant_entries_t make_non_cached_variant_entries_with_group_mapping(
    trace_array_t traces, trace_buffer_array_t trace_buffer, trace_length_array_t trace_lengths,
    common::owned_column_ptr_data_t group_id_to_trace_id) {
  static const std::string UNUSED_CACHE_KEY{"make_non_cached_variant_entries_with_group_mapping"};
  auto [temporary_trace_data_handler,
        temporary_trace_length_handler]{details::non_cached_variants::create_trace_handlers_internal(
      std::move(traces), std::move(trace_buffer), std::move(trace_lengths))};
  auto temporary_group_id_to_trace_id_mapping_col_ptrs{
      details::non_cached_variants::create_column_ptrs_from_owned_data_internal(std::move(group_id_to_trace_id))};

  return std::make_shared<memory::cache::variant_trace_cache>(
      std::move(temporary_trace_data_handler), std::move(temporary_trace_length_handler), UNUSED_CACHE_KEY,
      std::move(temporary_group_id_to_trace_id_mapping_col_ptrs));
}

bool is_valid_variant_id(const row_id variant_id) {
  return variant_id != memory::cache::variant_trace_cache::INVALID_VARIANT_ID.get();
}

}  // namespace celonis::accelerator::cube
