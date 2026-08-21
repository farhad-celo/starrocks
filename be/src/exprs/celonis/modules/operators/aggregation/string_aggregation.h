#pragma once

#include <string>

#include <ctl/static_array_fwd.h>

#include "modules/common/execution_context_fwd.h"
#include "modules/common/trace_types.h"
#include "modules/memory/cache/variant_trace_cache_fwd.h"
#include "modules/memory/column_pointers.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/table_to_column_projection.h"
#include "modules/operators/process/variant_constants.h"

namespace celonis::accelerator::operators::aggregation {

[[nodiscard]] memory::cache::variant_trace_cache_t compute_variant_row_ids(
    const memory::table_to_column_projection& table_to_column_projection, const common::execution_context& context,
    size_t grain_size = operators::process::COMPUTE_VARIANTS_GRAIN_SIZE);

[[nodiscard]] memory::cache::variant_trace_cache_t compute_variant_row_ids(
    common::execution_context& context, const std::string& cache_key, const std::string& activity_table_name,
    row_id num_case_rows, const memory::join_projection_vector_t& projection_vector,
    const ctl::shared_static_array<row_id>& activity_column,
    size_t grain_size = operators::process::COMPUTE_VARIANTS_GRAIN_SIZE);

}  // namespace celonis::accelerator::operators::aggregation
