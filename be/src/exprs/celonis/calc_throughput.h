#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisCalcThroughputFunctions {
public:
    DEFINE_VECTORIZED_FN(celonis_calc_throughput);
};

} // namespace starrocks
