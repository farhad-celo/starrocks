// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "column/array_column.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/hash_set.h"
#include "column/struct_column.h"
#include "column/type_traits.h"
#include "exec/sorting/sorting.h"
#include "exprs/agg/aggregate.h"
#include "exprs/function_context.h"
#include "runtime/mem_pool.h"
#include "runtime/runtime_state.h"
#include "types/logical_type.h"
#include "util/defer_op.h"

namespace starrocks {

struct WeekdayCalendarAggregateState {
    ~WeekdayCalendarAggregateState();

    std::unique_ptr<Column> weekday = nullptr;
    std::unique_ptr<Column> shift_begin = nullptr;
    std::unique_ptr<Column> shift_end = nullptr;
    std::unique_ptr<Column> calendar_id = nullptr;
    std::unique_ptr<Column> is_calendar_id_null = nullptr;
};

class WeekdayCalendarAggregateFunction
        : public AggregateFunctionBatchHelper<WeekdayCalendarAggregateState, WeekdayCalendarAggregateFunction> {
public:
    void create(FunctionContext* ctx, AggDataPtr __restrict ptr) const override;

    void reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const override;

    void update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                size_t row_num) const override;

    void merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state, size_t row_num) const override;

    bool support_nullable_immediate_input() const override { return true; }

    void serialize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;

    void finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state, Column* to) const override;

    void convert_to_serialize_format(FunctionContext* ctx, const Columns& src, size_t chunk_size,
                                     ColumnPtr* dst) const override;

    std::string get_name() const override;
};

} // namespace starrocks
