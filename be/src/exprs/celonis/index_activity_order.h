#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisIndexActivityOrder {
public:
    DEFINE_VECTORIZED_FN(celonis_index_activity_order);
};

} // namespace starrocks
