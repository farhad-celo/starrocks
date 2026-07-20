#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisMatchActivitiesFunctions {
public:
    DEFINE_VECTORIZED_FN(celonis_match_activities);
};

} // namespace starrocks
