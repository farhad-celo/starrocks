#pragma once

#include <limits>

#include "modules/common/int_types.h"

namespace celonis::accelerator {
#ifdef ROW_ID_64
#ifdef CELOSTAR
#error "Celostar uses 32 bit hashes for row ids and does not support ROW_ID_64."
#endif
using row_id = int64_t;
#else
using row_id = int32_t;
#endif
static constexpr row_id VALUE_NOT_FOUND{-1};
static constexpr row_id ROW_ID_MAX{std::numeric_limits<row_id>::max()};

}  // namespace celonis::accelerator
