#pragma once

#include <string>

#include "legacy_embedded_ctl/static_array_fwd.h"
#include "modules/common/execution_context_fwd.h"
#include "modules/common/owned_column_ptr_data.h"
#include "modules/common/trace_types.h"
#ifndef CELOSTAR
#include "modules/common/window_utils.h"
#include "modules/cube/cross_table_filters.h"
#include "modules/cube/execution/common_table_computations.h"
#include "modules/cube/filter_bitset_fwd.h"
#include "modules/cube/query_scope.h"
#endif
#include "modules/cube/variant_trace_cache_manager_fwd.h"
#ifndef CELOSTAR
#include "modules/memory/builders/result_column_builder_fwd.h"
#endif
#include "modules/memory/cache/variant_trace_cache_fwd.h"
#include "modules/memory/column_fwd.h"
#ifndef CELOSTAR
#include "modules/memory/column_lookup.h"
#endif
#include "modules/memory/column_pointers.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/table_fwd.h"
#include "modules/memory/table_to_column_projection.h"
#include "modules/memory/transform/join_projection_factories.h"
#ifndef CELOSTAR
#include "modules/operators/aggregation/projection.h"
#endif
#include "modules/operators/process/variant_constants.h"

namespace celonis::accelerator::operators::aggregation {

#ifndef CELOSTAR
[[nodiscard]] memory::builders::result_column_builder_t execute_variant_operator(
    common::execution_context& context, const std::string& cache_key,
    cube::variant_trace_cache_manager& variant_trace_cache_manager_instance, memory::table* case_table,
    const memory::join_projection_vector_t& projection_vector, const memory::column_t& activity_column,
    size_t grain_size);

[[nodiscard]] memory::builders::result_column_builder_t execute_parallel_string_agg_operator(
    const std::string& op_name, common::execution_context& context, const projection_vector_t& projection,
    const std::shared_ptr<cube::filter_bitset_t>& accepted_rows, const memory::column_t& source_column,
    row_id group_count, const std::string& delimiter,
    const legacy_embedded_ctl::static_array<row_id>& group_aligned_permutation, size_t grain_size);

[[nodiscard]] std::pair<cube::execution::columns_and_common_table, std::vector<OrderByDirection>>
compute_orderby_fetcher_and_order_directions(const OrderByExpressions& order_by_expressions,
                                             const cube::query_scope& query_scope,
                                             const common::execution_context& context,
                                             const memory::column_lookup_t& order_by_expression_columns);

[[nodiscard]] legacy_embedded_ctl::static_array<row_id> compute_group_aligned_permutation_simple(
    const memory::column_t& source_column, common::execution_context& context, const projection_vector_t& projection,
    row_id target_table_size);

/**
 * The caller is responsible for pulling up the source_column and orderby columns to the common table.
 */
[[nodiscard]] legacy_embedded_ctl::static_array<row_id> compute_group_aligned_permutation_order_by_columns(
    const std::string& op_name, const memory::column_t& source_column, common::execution_context& context,
    const cube::query_scope& query_scope, const std::vector<memory::column_t>& orderby_columns,
    const std::vector<OrderByDirection>& orderby_directions, const projection_vector_t& projection,
    row_id target_table_size);

/** Only exposed for testing (verify cache entries exist) */
[[nodiscard]] std::string make_generalized_variant_row_ids_computation_cache_key(const memory::column_t& values,
                                                                                 const std::string& mapping_cache_key);
#endif

struct caching_meta_data final {
  // A cache key for the group IDs. Could be e.g., some source table name of the group IDs (e.g., the case table)
  std::string group_ids_cache_key;
  // The variant cache manager where the variant entries should be cached in
  cube::variant_trace_cache_manager& variant_cache_manager_instance;
};

// Used for the generalized variants computation. In case caching is desired, this optional must be set
using optional_caching_meta_data_t = std::optional<caching_meta_data>;

// Basically the same as the join projection vector but with a more generalized name
using value_idx_to_group_id_mapping_t = memory::join_projection_vector_t;

/**
 * @brief Interface for a 'generalized' internal variant computation.
 * Here 'generalized' refers to the following:
 * - no case-events relationship is expected for the input
 * - allowing for temporary (non-cached/non-swappable) AND permanent variant entries
 * - the mapping does not need any semantics for its group ID values (such as referring to the case id table)
 * @param optional_caching_meta_data if set, indicates that the variant entries should be cached and stored
 * @param mapping_and_group_id_domain Contains the mapping from a value index to its group ID and optionally the domain
 * @param values The values to aggregate into groups (TODO:n.weber: Generalize to some accessor to work with arrays)
 * @return Either 'permanent' or 'temporary' variant entries
 *
 * @note: This function guarantees that variant-ids are assigned in a stable manner: that is, between two
 * calls to the function with the same input, ids assigned to variants will not change. This is useful if the output
 * of an operator depends on variant-id assignment.
 *
 */
[[nodiscard]] memory::cache::variant_entries_t generalized_variant_row_ids_computation(  //
    optional_caching_meta_data_t optional_caching_meta_data,                             //
    memory::group_id_mapping_and_group_id_domain mapping_and_group_id_domain,            //
    const memory::column_t& values,                                                      //
    const common::execution_context& context,                                            //
    size_t grain_size = operators::process::COMPUTE_VARIANTS_GRAIN_SIZE);

// Deprecated: Use the more general interface above 'generalized_variant_row_ids_computation'
#ifdef CELOSTAR
[[nodiscard]] memory::cache::variant_trace_cache_t compute_variant_row_ids(
    const memory::table_to_column_projection& table_to_column_projection, const common::execution_context& context,
    size_t grain_size = operators::process::COMPUTE_VARIANTS_GRAIN_SIZE);
#else
[[nodiscard]] memory::cache::variant_trace_cache_t compute_variant_row_ids(
    const memory::table_to_column_projection& table_to_column_projection,
    cube::variant_trace_cache_manager& variant_trace_cache_manager_instance, const common::execution_context& context,
    size_t grain_size = operators::process::COMPUTE_VARIANTS_GRAIN_SIZE);
#endif

[[nodiscard]] memory::cache::variant_trace_cache_t compute_variant_row_ids(
    common::execution_context& context, const std::string& cache_key, const std::string& activity_table_name,
    row_id num_case_rows, const memory::join_projection_vector_t& projection_vector,
    const legacy_embedded_ctl::shared_static_array<row_id>& activity_column,
    cube::variant_trace_cache_manager& variant_trace_cache_manager_instance,
    size_t grain_size = operators::process::COMPUTE_VARIANTS_GRAIN_SIZE);

struct variant_row_id_result {
  legacy_embedded_ctl::static_array<trace_type> trace_ptrs;
  legacy_embedded_ctl::static_array<trace_buffer_type> trace_buffer_data;
  legacy_embedded_ctl::static_array<trace_length_type> trace_lengths;
  common::owned_column_ptr_data_t group_id_to_trace_id;
  row_id num_unique_variants;
};

#ifndef CELOSTAR
[[nodiscard]] variant_row_id_result compute_variant_row_ids(
    common::execution_context& context, row_id num_case_rows, const memory::join_projection_vector_t& projection_vector,
    const legacy_embedded_ctl::shared_static_array<row_id>& activity_column,
    size_t grain_size = operators::process::COMPUTE_VARIANTS_GRAIN_SIZE);
#endif

}  // namespace celonis::accelerator::operators::aggregation
