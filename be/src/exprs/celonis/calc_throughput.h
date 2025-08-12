#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisCalcThroughputFunctions {
public:
    DEFINE_VECTORIZED_FN(celonis_calc_throughput);
private:
    template<typename ActivityColumn, LogicalType ActivityType>
    static ColumnPtr
    _celonis_calc_throughput_impl(const ActivityColumn& activity_elements, const UInt32Column& activity_offsets,
                                  const NullColumn::Container* activity_nulls,
                                  const NullColumn::Container* activity_array_nulls, ColumnPtr timestamp_array,
                                  ColumnPtr start_activity, ColumnPtr end_activity, ColumnPtr start_label, ColumnPtr end_label);
};

} // namespace starrocks
