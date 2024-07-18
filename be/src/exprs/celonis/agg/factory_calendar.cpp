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

#include "factory_calendar.h"
#include "exprs/celonis/agg/util.h"
#include "gutil/strings/strcat.h"
#include "modules/query/calendars.pb.h"

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
    if (is_calendar_id_null != nullptr) {
        is_calendar_id_null.reset(nullptr);
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
    if (columns[2]->get(row_num).is_null()) {
        state_impl.calendar_id->append_default();
        state_impl.is_calendar_id_null->append_datum(true);
    } else {
        state_impl.calendar_id->append_datum(columns[2]->get(row_num));
        state_impl.is_calendar_id_null->append_datum(false);
    }
}

void FactoryCalendarAggregateFunction::merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state,
                                             size_t row_num) const {
    auto& input_columns = down_cast<const StructColumn*>(ColumnHelper::get_data_column(column))->fields();
    auto& state_impl = this->data(state);
    auto start_timestamp_column = down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_columns.at(0).get()));
    auto end_timestamp_column = down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_columns.at(1).get()));
    auto calendar_id_column = down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_columns.at(2).get()));
    auto is_calendar_id_null_column = down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_columns.at(3).get()));
    auto& offsets = start_timestamp_column->offsets().get_data();
    const auto start = offsets[row_num];
    const auto end = offsets[row_num + 1];
    for (auto i = start; i < end; ++i) {
       state_impl.start_timestamp->append_datum(start_timestamp_column->elements().get(i));
       state_impl.end_timestamp->append_datum(end_timestamp_column->elements().get(i));
       state_impl.calendar_id->append_datum(calendar_id_column->elements().get(i));
       state_impl.is_calendar_id_null->append_datum(is_calendar_id_null_column->elements().get(i));
    }
}

void FactoryCalendarAggregateFunction::serialize_to_column(starrocks::FunctionContext* ctx,
                                                           __restrict starrocks::ConstAggDataPtr state,
                                                           starrocks::Column* to) const {
    auto& state_impl = this->data(state);
    auto& columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
    if (!state_impl.start_timestamp->empty()) {
        if (to->is_nullable()) {
            down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
        }
        ::starrocks::serialize_to_column(state_impl.start_timestamp, columns.at(0));
        ::starrocks::serialize_to_column(state_impl.end_timestamp, columns.at(1));
        ::starrocks::serialize_to_column(state_impl.calendar_id, columns.at(2));
        ::starrocks::serialize_to_column(state_impl.is_calendar_id_null, columns.at(3));
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
        const bool calendar_id_not_null = !static_cast<bool>(state_impl.is_calendar_id_null->get(i).get_uint8());
        entry.set_start_date(start_date);
        entry.set_end_date(end_date);
        if (calendar_id_not_null) {
            entry.set_calendar_id(calendar_id);
        }
        *calendar_proto.mutable_factory_calendar()->add_entries() = entry;
    }
    std::optional<std::string> calendar_string = to_base64_encoded_string(calendar_proto);
    if (!calendar_string.has_value()) {
        ctx->set_error(StrCat("Calendar proto serialized size (", calendar_proto.ByteSizeLong(),
                              " bytes) exceeds maximum supported length (1GB)").c_str(), false);
        return;
    }

    std::vector<std::string> calendar_pieces;
    calendar_pieces.reserve((calendar_string->size() + MAX_STRING_SIZE - 1) / MAX_STRING_SIZE);
    for (size_t i = 0; i < calendar_string->size(); i += MAX_STRING_SIZE) {
        calendar_pieces.emplace_back(calendar_string->substr(i, MAX_STRING_SIZE));
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
    if (!valid_indexes.empty()) {
        if (dst->get()->is_nullable()) {
            down_cast<NullableColumn*>(dst->get())->null_column_data().emplace_back(0);
        }
        DatumArray start_timestamp_array;
        DatumArray end_timestamp_array;
        DatumArray calendar_id_array;
        DatumArray is_calendar_id_null_array;
        for (auto i : valid_indexes) {
            start_timestamp_array.push_back(src[0]->get(i));
            end_timestamp_array.push_back(src[1]->get(i));
            if (src[2]->is_null(i)) {
                calendar_id_array.push_back("");
                is_calendar_id_null_array.push_back(true);
            } else {
                calendar_id_array.push_back(src[2]->get(i));
                is_calendar_id_null_array.push_back(false);
            }
        }
        columns[0]->append_datum(start_timestamp_array);
        columns[1]->append_datum(end_timestamp_array);
        columns[2]->append_datum(calendar_id_array);
        columns[3]->append_datum(is_calendar_id_null_array);
    }
}

std::string FactoryCalendarAggregateFunction::get_name() const { return "celonis_make_factory_calendar"; }

} // namespace starrocks
