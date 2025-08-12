#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisArrayFunctions {
public:
    DEFINE_VECTORIZED_FN(array_is_sorted);
};

} // namespace starrocks