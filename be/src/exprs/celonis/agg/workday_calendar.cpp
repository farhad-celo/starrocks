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

#include "workday_calendar.h"
#include "exprs/celonis/agg/util.h"
#include "modules/query/calendars.pb.h"

namespace starrocks {

namespace {

// SR places an upper limit of 1M of STRING. We use 900K which is less than 1M.
static const size_t MAX_STRING_SIZE = 900000;

}

WorkdayCalendarAggregateState::~WorkdayCalendarAggregateState() {
    if (year != nullptr) {
        year.reset(nullptr);
    }
    if (is_workdays != nullptr) {
        is_workdays.reset(nullptr);
    }
    if (calendar_id != nullptr) {
        calendar_id.reset(nullptr);
    }
    if (is_calendar_id_null != nullptr) {
        is_calendar_id_null.reset(nullptr);
    }
}

void WorkdayCalendarAggregateFunction::create(FunctionContext* ctx, AggDataPtr __restrict ptr) const {
    auto num = ctx->get_num_args();
    DCHECK(num == 3);
    auto* state = new(ptr) WorkdayCalendarAggregateState;
    state->year = std::make_unique<Int64Column>();
    state->is_workdays = std::make_unique<BinaryColumn>();
    state->calendar_id = std::make_unique<BinaryColumn>();
    state->is_calendar_id_null = std::make_unique<BooleanColumn>();
}

void
WorkdayCalendarAggregateFunction::reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const {
    auto& state_impl = this->data(state);
    if (state_impl.year != nullptr) {
        state_impl.year.reset(nullptr);
    }
    if (state_impl.is_workdays != nullptr) {
        state_impl.is_workdays.reset(nullptr);
    }
    if (state_impl.calendar_id != nullptr) {
        state_impl.calendar_id.reset(nullptr);
    }
    if (state_impl.is_calendar_id_null != nullptr) {
        state_impl.is_calendar_id_null.reset(nullptr);
    }
}

void WorkdayCalendarAggregateFunction::update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                                              size_t row_num) const {
    DCHECK(ctx->get_num_args() == 3);
    for (auto i = 0; i < ctx->get_num_args(); ++i) {
        if (UNLIKELY(columns[i]->size() <= row_num)) {
            ctx->set_error(std::string(get_name() + "'s update row number overflow").c_str(), false);
            return;
        }
    }
    // If year or is_workdays is NULL, ignore.
    for (auto i = 0; i < 2; ++i) {
        if ((columns[i]->is_nullable() && columns[i]->is_null(row_num)) || columns[i]->only_null()) {
            return;
        }
    }

    auto& state_impl = this->data(state);
    state_impl.year->append_datum(columns[0]->get(row_num));
    state_impl.is_workdays->append_datum(columns[1]->get(row_num));
    if (columns[2]->get(row_num).is_null()) {
        state_impl.calendar_id->append_default();
        state_impl.is_calendar_id_null->append_datum(true);
    } else {
        state_impl.calendar_id->append_datum(columns[2]->get(row_num));
        state_impl.is_calendar_id_null->append_datum(false);
    }
}

void WorkdayCalendarAggregateFunction::merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state,
                                             size_t row_num) const {
    auto& input_columns = down_cast<const StructColumn*>(ColumnHelper::get_data_column(column))->fields();
    auto& state_impl = this->data(state);
    state_impl.year->append_datum(input_columns.at(0)->get(row_num));
    state_impl.is_workdays->append_datum(input_columns.at(1)->get(row_num));
    state_impl.calendar_id->append_datum(input_columns.at(2)->get(row_num));
    state_impl.is_calendar_id_null->append_datum(input_columns.at(3)->get(row_num));
}

void WorkdayCalendarAggregateFunction::serialize_to_column(starrocks::FunctionContext* ctx,
                                                           __restrict starrocks::ConstAggDataPtr state,
                                                           starrocks::Column* to) const {
    auto& state_impl = this->data(state);
    auto& columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
    const auto size = state_impl.year->size();
    if (to->is_nullable()) {
        down_cast<NullableColumn*>(to)->mutable_null_column()->get_data().resize(size, 0);
    }
    down_cast<NullableColumn*>(columns[0].get())->mutable_null_column()->get_data().resize(size, 0);
    down_cast<NullableColumn*>(columns[1].get())->mutable_null_column()->get_data().resize(size, 0);
    down_cast<NullableColumn*>(columns[2].get())->mutable_null_column()->get_data().resize(size, 0);
    down_cast<NullableColumn*>(columns[3].get())->mutable_null_column()->get_data().resize(size, 0);
    auto year = down_cast<Int64Column*>(ColumnHelper::get_data_column(columns[0].get()));
    for (size_t i = 0; i < size; ++i) {
        year->append(state_impl.year->get(i).get_int64());
    }
    auto is_workdays = down_cast<BinaryColumn*>(ColumnHelper::get_data_column(columns[1].get()));
    for (size_t i = 0; i < size; ++i) {
        is_workdays->append(state_impl.is_workdays->get(i).get_slice());
    }
    auto calendar_id = down_cast<BinaryColumn*>(ColumnHelper::get_data_column(columns[2].get()));
    for (size_t i = 0; i < size; ++i) {
        calendar_id->append(state_impl.calendar_id->get(i).get_slice());
    }
    auto is_calendar_id_null = down_cast<BooleanColumn*>(ColumnHelper::get_data_column(columns[3].get()));
    for (size_t i = 0; i < size; ++i) {
        is_calendar_id_null->append(state_impl.is_calendar_id_null->get(i).get_uint8());
    }
}

void WorkdayCalendarAggregateFunction::finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
                                                          Column* to) const {
    auto defer = DeferOp([&]() {
        if (ctx->has_error() && to != nullptr) {
            to->append_default();
        }
    });
    if (UNLIKELY(!ColumnHelper::get_data_column(to)->is_array())) {
        ctx->set_error(std::string("The output column of " + get_name() +
                                   " finalize_to_column() is not array, but is " + to->get_name())
                               .c_str(),
                       false);
        return;
    }
    auto& state_impl = this->data(state);
    // TimestampValue epoch = TimestampValue::create(1970, 1, 1, 0, 0, 0);
    const auto n_rows = state_impl.year->size();
    DCHECK(state_impl.is_workdays->size() == n_rows);
    DCHECK(state_impl.calendar_id->size() == n_rows);
    DCHECK(state_impl.is_calendar_id_null->size() == n_rows);
    celonis::accelerator::Calendar calendar_proto;
    for (int i = 0; i < n_rows; ++i) {
        celonis::accelerator::WorkdayCalendarEntry entry;
        const int64_t year = state_impl.year->get(i).get_int64();
        const std::string is_workdays = state_impl.is_workdays->get(i).get_slice().to_string();
        const std::string calendar_id = state_impl.calendar_id->get(i).get_slice().to_string();
        const bool calendar_id_not_null = !static_cast<bool>(state_impl.is_calendar_id_null->get(i).get_uint8());
        entry.set_year(year);
        for (char is_workday: is_workdays) {
            if (is_workday == '0') {
                entry.add_is_workday(false);
            } else {
                entry.add_is_workday(true);
            }
        }
        if (calendar_id_not_null) {
            entry.set_calendar_id(calendar_id);
        }
        *calendar_proto.mutable_workday_calendar()->add_entries() = entry;
    }
    std::string calendar_string = to_base64_encoded_string(calendar_proto);
    std::vector<std::string> calendar_pieces;
    calendar_pieces.reserve((calendar_string.size() + MAX_STRING_SIZE - 1) / MAX_STRING_SIZE);
    for (size_t i = 0; i < calendar_string.size(); i += MAX_STRING_SIZE) {
        calendar_pieces.emplace_back(calendar_string.substr(i, MAX_STRING_SIZE));
    }
    DatumArray array;
    for (const auto& calendar_piece: calendar_pieces) {
        array.emplace_back(calendar_piece.c_str());
    }
    to->append_datum(array);
}

// convert each cell of a row to a [nullable] array in a struct
void WorkdayCalendarAggregateFunction::convert_to_serialize_format(FunctionContext* ctx, const Columns& src,
                                                                   size_t chunk_size,
                                                                   ColumnPtr* dst) const {
    DCHECK(src.size() == 3);
    std::vector<size_t> valid_indexes;
    for (size_t row = 0; row < chunk_size; ++row) {
        if ((src[0]->is_nullable() && src[0]->is_null(row)) || src[0]->only_null()) {
            continue;
        }
        if ((src[1]->is_nullable() && src[1]->is_null(row)) || src[1]->only_null()) {
            continue;
        }
        valid_indexes.push_back(row);
    }
    auto columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(dst->get()))->fields_column();
    if (dst->get()->is_nullable()) {
        for (size_t i = 0; i < valid_indexes.size(); i++) {
            down_cast<NullableColumn*>(dst->get())->null_column_data().emplace_back(0);
        }
    }
    for (auto& column: columns) {
        if (column.get()->is_nullable()) {
            down_cast<NullableColumn*>(column.get())->mutable_null_column()->get_data().resize(valid_indexes.size(), 0);
        }
    }
    auto year = down_cast<Int64Column*>(ColumnHelper::get_data_column(columns[0].get()));
    for (auto i: valid_indexes) {
        year->append_datum(src[0]->get(i));
    }
    auto is_workdays = down_cast<BinaryColumn*>(ColumnHelper::get_data_column(columns[1].get()));
    for (auto i: valid_indexes) {
        is_workdays->append_datum(src[1]->get(i));
    }
    auto calendar_id = down_cast<BinaryColumn*>(ColumnHelper::get_data_column(columns[2].get()));
    auto is_calendar_id_null = down_cast<BooleanColumn*>(ColumnHelper::get_data_column(columns[3].get()));
    for (auto i: valid_indexes) {
        if (src[2]->is_null(i)) {
            calendar_id->append_default();
            is_calendar_id_null->append(true);
        } else {
            calendar_id->append_datum(src[2]->get(i));
            is_calendar_id_null->append(false);
        }
    }
}

std::string WorkdayCalendarAggregateFunction::get_name() const { return "celonis_make_workday_calendar"; }

} // namespace starrocks
