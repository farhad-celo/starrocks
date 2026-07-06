#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

// Performs SHORTENED(VARIANT(activity_column), max_cycle) function, which
// finds all the variants of the activity column where the cycles are capped
// at 'max_cycle' length.
template <LogicalType ActivityLT>
class CelonisShortenedVariant {
public:
    DEFINE_VECTORIZED_FN(celonis_shortened_variant);
};

} // namespace starrocks
