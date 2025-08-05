#pragma once

#include "legacy_embedded_ctl/checked_ptr_fwd.h"
#include "modules/memory/column_fwd.h"
#include "modules/memory/join_projection_vector.h"
#include "modules/memory/table_fwd.h"

namespace celonis::accelerator::memory {

/**
 * @brief Strong type explicitly modeling the join relationship between the given input.
 * This should help to make interfaces more robust as all three arguments which depend on each other (i.e., table,
 * column, and join between these) must be explicitly wrapped together.
 * @note It is the caller's responsibility to ensure that the passed arguments are indeed fitting together:
 * The given table (one-side) must be connected to the given column (n-side) via the given join projection vector. This
 * type does not validate this relationship.
 * @example The relationship between the case table (one-side) and the activity column (n-side)
 */
struct table_to_column_projection final {
  /** Table on the one-side of the join */
  const legacy_embedded_ctl::checked_raw_ptr<const table> table_one_side;
  /** Column on the n-side of the join */
  const column_t column_n_side;
  /** Projection from the n-side (column) to the one-side (table) */
  const join_projection_vector_t projection;
};

}  // namespace celonis::accelerator::memory
