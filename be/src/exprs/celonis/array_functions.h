#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisArrayFunctions {
public:
    DEFINE_VECTORIZED_FN(array_is_sorted);

    DEFINE_VECTORIZED_FN(null_to_empty);
};

} // namespace starrocks