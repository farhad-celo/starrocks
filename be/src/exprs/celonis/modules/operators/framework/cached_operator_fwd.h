#pragma once

#include <vector>

#include "modules/memory/column_fwd.h"

namespace celonis::accelerator::operators {
class cached_operator;
using operator_input_columns_t = std::vector<memory::column_t>;
}  // namespace celonis::accelerator::operators
