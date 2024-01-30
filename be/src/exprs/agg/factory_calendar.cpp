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

#include "exprs/agg/factory_calendar.h"
#include "modules/query/calendars.pb.h"
#include <google/protobuf/util/json_util.h>

namespace starrocks {

namespace {

// SR places an upper limit of 1M of STRING. We use 900K which is less than 1M.
static const size_t MAX_STRING_SIZE = 900000;

}

FactoryCalendarAggregateState::~FactoryCalendarAggregateState() {
    if (start_timestamp != nullptr) {
        start_timestamp.reset(nullptr);
    }
    if (end_timestamp != nullptr) {
        end_timestamp.reset(nullptr);
    }
    if (calendar_id != nullptr) {
        calendar_id.reset(nullptr);
    }
}

void FactoryCalendarAggregateFunction::create(FunctionContext* ctx, AggDataPtr __restrict ptr) const {
    auto num = ctx->get_num_args();
    DCHECK(num == 3);
    auto* state = new(ptr) FactoryCalendarAggregateState;
    state->start_timestamp = std::make_unique<TimestampColumn>();
    state->end_timestamp = std::make_unique<TimestampColumn>();
    state->calendar_id = std::make_unique<BinaryColumn>();
    state->is_calendar_id_null = std::make_unique<BooleanColumn>();
}

void
FactoryCalendarAggregateFunction::reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const {
    auto& state_impl = this->data(state);
    if (state_impl.start_timestamp != nullptr) {
        state_impl.start_timestamp.reset(nullptr);
    }
    if (state_impl.end_timestamp != nullptr) {
        state_impl.end_timestamp.reset(nullptr);
    }
    if (state_impl.calendar_id != nullptr) {
        state_impl.calendar_id.reset(nullptr);
    }
    if (state_impl.is_calendar_id_null != nullptr) {
        state_impl.is_calendar_id_null.reset(nullptr);
    }
}

void FactoryCalendarAggregateFunction::update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                                              size_t row_num) const {
    DCHECK(ctx->get_num_args() == 3);
    for (auto i = 0; i < ctx->get_num_args(); ++i) {
        if (UNLIKELY(columns[i]->size() <= row_num)) {
            ctx->set_error(std::string(get_name() + "'s update row number overflow").c_str(), false);
            return;
        }
    }
    // If start_timestamp or end_timestamp is NULL, ignore.
    for (auto i = 0; i < 2; ++i) {
        if ((columns[i]->is_nullable() && columns[i]->is_null(row_num)) || columns[i]->only_null()) {
            return;
        }
    }

    auto& state_impl = this->data(state);
    state_impl.start_timestamp->append_datum(columns[0]->get(row_num));
    state_impl.end_timestamp->append_datum(columns[1]->get(row_num));
    if (!columns[2]->get(row_num).is_null()) {
        state_impl.calendar_id->append_datum(columns[2]->get(row_num));
        state_impl.is_calendar_id_null->append_datum(false);
    } else {
        state_impl.calendar_id->append_default();
        state_impl.is_calendar_id_null->append_datum(true);
    }
}

void FactoryCalendarAggregateFunction::merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state,
                                             size_t row_num) const {
    auto& input_columns = down_cast<const StructColumn*>(ColumnHelper::get_data_column(column))->fields();
    auto& state_impl = this->data(state);
    state_impl.start_timestamp->append_datum(input_columns.at(0)->get(row_num));
    state_impl.end_timestamp->append_datum(input_columns.at(1)->get(row_num));
    if (!input_columns.at(2)->get(row_num).is_null()) {
        state_impl.calendar_id->append_datum(input_columns.at(2)->get(row_num));
        state_impl.is_calendar_id_null->append_datum(false);
    } else {
        state_impl.calendar_id->append_default();
        state_impl.is_calendar_id_null->append_datum(true);
    }
}

void FactoryCalendarAggregateFunction::serialize_to_column(starrocks::FunctionContext* ctx,
                                                           __restrict starrocks::ConstAggDataPtr state,
                                                           starrocks::Column* to) const {
    auto& state_impl = this->data(state);
    auto& columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
    const auto size = state_impl.start_timestamp->size();
    if (to->is_nullable()) {
        down_cast<NullableColumn*>(to)->mutable_null_column()->get_data().resize(size, 0);
    }
    down_cast<NullableColumn*>(columns[0].get())->mutable_null_column()->get_data().resize(size, 0);
    down_cast<NullableColumn*>(columns[1].get())->mutable_null_column()->get_data().resize(size, 0);
    down_cast<NullableColumn*>(columns[2].get())->mutable_null_column()->get_data().resize(size, 0);
    down_cast<NullableColumn*>(columns[3].get())->mutable_null_column()->get_data().resize(size, 0);
    auto start_timestamp = down_cast<TimestampColumn*>(ColumnHelper::get_data_column(columns[0].get()));
    for (int i = 0; i < size; ++i) {
        start_timestamp->append(state_impl.start_timestamp->get(i).get_timestamp());
    }
    auto end_timestamp = down_cast<TimestampColumn*>(ColumnHelper::get_data_column(columns[1].get()));
    for (int i = 0; i < size; ++i) {
        end_timestamp->append(state_impl.end_timestamp->get(i).get_timestamp());
    }
    auto calendar_id = down_cast<BinaryColumn*>(ColumnHelper::get_data_column(columns[2].get()));
    for (int i = 0; i < size; ++i) {
        calendar_id->append(state_impl.calendar_id->get(i).get_slice());
    }
    auto is_calendar_id_null = down_cast<BooleanColumn*>(ColumnHelper::get_data_column(columns[3].get()));
    for (int i = 0; i < size; ++i) {
        is_calendar_id_null->append(state_impl.is_calendar_id_null->get(i).get_uint8());
    }
}

void FactoryCalendarAggregateFunction::finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
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
    TimestampValue epoch = TimestampValue::create(1970, 1, 1, 0, 0, 0);
    const auto n_rows = state_impl.start_timestamp->size();
    DCHECK(state_impl.end_timestamp->size() == n_rows);
    DCHECK(state_impl.calendar_id->size() == n_rows);
    DCHECK(state_impl.is_calendar_id_null->size() == n_rows);
    celonis::accelerator::Calendar calendar_proto;
    for (int i = 0; i < n_rows; ++i) {
        celonis::accelerator::FactoryCalendarEntry entry;
        const int64_t start_date = state_impl.start_timestamp->get(i).get_timestamp().diff_microsecond(epoch) / 1000L;
        const int64_t end_date = state_impl.end_timestamp->get(i).get_timestamp().diff_microsecond(epoch) / 1000L;
        const std::string calendar_id = state_impl.calendar_id->get(i).get_slice().to_string();
        const bool is_calendar_id_null = state_impl.is_calendar_id_null->get(i).get_uint8() != 0;
        entry.set_start_date(start_date);
        entry.set_end_date(end_date);
        if (!is_calendar_id_null) {
            entry.set_calendar_id(calendar_id);
        }
        *calendar_proto.mutable_factory_calendar()->add_entries() = entry;
    }
    std::string calendar_json;
    google::protobuf::util::MessageToJsonString(calendar_proto, &calendar_json);
    std::vector<std::string> calendar_pieces;
    calendar_pieces.reserve((calendar_json.size() + MAX_STRING_SIZE - 1) / MAX_STRING_SIZE);
    for (size_t i = 0; i < calendar_json.size(); i += MAX_STRING_SIZE) {
        calendar_pieces.emplace_back(calendar_json.substr(i, MAX_STRING_SIZE));
    }
    DatumArray array;
    for (const auto& calendar_piece: calendar_pieces) {
        array.emplace_back(calendar_piece.c_str());
    }
    to->append_datum(array);
}

// convert each cell of a row to a [nullable] array in a struct
void FactoryCalendarAggregateFunction::convert_to_serialize_format(FunctionContext* ctx, const Columns& src,
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
    auto start_timestamp = down_cast<TimestampColumn*>(ColumnHelper::get_data_column(columns[0].get()));
    for (auto i: valid_indexes) {
        start_timestamp->append_datum(src[0]->get(i));
    }
    auto end_timestamp = down_cast<TimestampColumn*>(ColumnHelper::get_data_column(columns[1].get()));
    for (auto i: valid_indexes) {
        end_timestamp->append_datum(src[1]->get(i));
    }
    auto calendar_id = down_cast<BinaryColumn*>(ColumnHelper::get_data_column(columns[2].get()));
    auto is_calendar_id_null = down_cast<BooleanColumn*>(ColumnHelper::get_data_column(columns[3].get()));
    for (auto i: valid_indexes) {
        if (src[2]->get(i).is_null()) {
            calendar_id->append_default();
            is_calendar_id_null->append(true);
        } else {
            calendar_id->append_datum(src[2]->get(i));
            is_calendar_id_null->append(false);
        }
    }
}

std::string FactoryCalendarAggregateFunction::get_name() const { return "celonis_make_factory_calendar"; }

} // namespace starrocks
