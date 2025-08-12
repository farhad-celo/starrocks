#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

// Computes the weekday number of the given timestamp expression (starting from Unix epoch).
class CelonisRemapTimestampWeekday {
public:
    DEFINE_VECTORIZED_FN(celonis_remap_timestamp_weekday);

    DEFINE_VECTORIZED_FN(celonis_remap_timestamp_weekday_scalar);

private:
    template <bool has_null>
    static ColumnPtr
    _celonis_remap_timestamp_weekday_impl(FunctionContext* context, const TimestampColumn& timestamp_elements,
                                          const UInt32Column& timestamp_offsets,
                                          NullColumnPtr timestamp_nulls, NullColumnPtr array_nulls);
};

} // namespace starrocks
