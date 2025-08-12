#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisTimeFunctions {
public:
    DEFINE_VECTORIZED_FN(timestamp_millis);
};

} // namespace starrocks
