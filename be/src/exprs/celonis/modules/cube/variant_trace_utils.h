#pragma once

#include <memory>
#include <optional>
#include <string>

#include <fmt/format.h>

#include "legacy_embedded_ctl/named_type.h"
#include "legacy_embedded_ctl/static_array.h"
#include "modules/common/owned_column_ptr_data.h"
#include "modules/common/trace_types.h"
#include "modules/memory/cache/variant_trace_cache_fwd.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/management/data_handler.h"
#include "modules/memory/management/pointer_data_handler.h"
#include "modules/memory/management/swap_info.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::cube {

using trace_array_t = legacy_embedded_ctl::static_array<trace_type>;
using trace_buffer_array_t = legacy_embedded_ctl::static_array<trace_buffer_type>;
using trace_length_array_t = legacy_embedded_ctl::static_array<trace_length_type>;

namespace details {

struct trace_handlers {
  std::shared_ptr<memory::management::pointer_data_handler<trace_type>> trace_data_handler;
  memory::management::raw_data_handler_t<trace_length_type> trace_lengths_data_handler;
};

namespace non_cached_variants {
/** Creates and returns the non-cacheable/non-swappable trace data/pointer handlers for the variant entries. */
[[nodiscard]] trace_handlers create_trace_handlers_internal(trace_array_t traces, trace_buffer_array_t trace_buffer,
                                                            trace_length_array_t trace_lengths);

/** Creates and returns non-cacheable/non-swappable column pointers for the given owned_column_ptr_data. */
[[nodiscard]] memory::column_ptrs_t create_column_ptrs_from_owned_data_internal(
    common::owned_column_ptr_data_t owned_column_ptr_data);
}  // namespace non_cached_variants

namespace cached_variants {
struct caching_meta_data {
  std::string cache_key;
  std::string cache_id;
  memory::management::swap_info swap_info;
  std::string description{fmt::format("Variant Cache {}", cache_key)};
};

/** Creates and returns the cacheable/swappable trace data/pointer handlers for the variant entries. */
[[nodiscard]] trace_handlers create_trace_handlers_internal(trace_array_t traces, trace_buffer_array_t trace_buffer,
                                                            trace_length_array_t trace_lengths,
                                                            const caching_meta_data& caching_data);

/** Creates and returns cacheable/swappable column pointers for the given owned_column_ptr_data. */
[[nodiscard]] memory::column_ptrs_t create_column_ptrs_from_owned_data_internal(
    common::owned_column_ptr_data_t owned_column_ptr_data, const caching_meta_data& caching_data);
}  // namespace cached_variants

}  // namespace details

/** Creates and returns variant entries which are only temporary (non-cached/non-swappable) */
[[nodiscard]] memory::cache::variant_entries_t make_non_cached_variant_entries_with_group_mapping(
    trace_array_t traces, trace_buffer_array_t trace_buffer, trace_length_array_t trace_lengths,
    common::owned_column_ptr_data_t group_id_to_trace_id);

[[nodiscard]] bool is_valid_variant_id(row_id variant_id);

}  // namespace celonis::accelerator::cube
