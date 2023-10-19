#pragma once

#include <optional>

#include "modules/common/execution_context_fwd.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/row_id.h"

namespace celonis::accelerator::memory {

// Basically the same as the join projection vector but with a more generalized name
using value_idx_to_group_id_mapping_t = join_projection_vector_t;

/** Simple proxy which allows to provide a value index to group ID mapping together with the optional group ID domain */
struct group_id_mapping_and_group_id_domain final {
  /** The group ID domain value (largest value contained within the mapping + 1) */
  [[nodiscard]] row_id get_or_compute_group_id_domain();

  // Note: The 'density' of the group IDs directly affects how sparse the group ID to trace ID mapping in the
  // variant_trace_cache will be. To reduce the memory overhead, try to keep the mapping dense.
  value_idx_to_group_id_mapping_t value;
  // If the following is not set, it must be fetched from the mapping which has the cost of iterating over it
  // The value must be at least +1 the largest group ID value in the mapping (as the group IDs will be used to index an
  // array allocated with that size)
  std::optional<row_id> optional_group_id_domain{std::nullopt};
};

namespace transform {

/** Creates the group ID mapping from a case ID column and returns it together with the group ID domain */
[[nodiscard]] group_id_mapping_and_group_id_domain case_id_column_to_mapping_and_group_id_domain(
    const column_t& case_id_column, const common::execution_context& ctx);

}  // namespace transform

}  // namespace celonis::accelerator::memory
