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

#include "weekday_calendar.h"
#include "exprs/celonis/agg/util.h"
#include "gutil/strings/strcat.h"
#include "modules/query/calendars.pb.h"

namespace starrocks {

namespace {

// SR places an upper limit of 1M of STRING. We use 900K which is less than 1M.
static const size_t MAX_STRING_SIZE = 900000;

static const int64_t NUM_MILLISECONDS_PER_DAY = 86400000L;

int64_t to_millis(const std::string& hhmm) {
    std::istringstream iss(hhmm);
    int hours, minutes;
    char delim;
    // note that we allow hours = 24 or minutes = 60
    if (!(iss >> hours >> delim >> minutes) || delim != ':' || hours < 0 || hours > 24 || minutes < 0 ||
        minutes > 60) {
        return -1;
    }
    int64_t total_minutes = hours * 60 + minutes;
    return total_minutes * 60 * 1000;
}

}

WeekdayCalendarAggregateState::~WeekdayCalendarAggregateState() {
    if (weekday != nullptr) {
        weekday.reset(nullptr);
    }
    if (shift_begin != nullptr) {
        shift_begin.reset(nullptr);
    }
    if (shift_end != nullptr) {
        shift_end.reset(nullptr);
    }
    if (calendar_id != nullptr) {
        calendar_id.reset(nullptr);
    }
    if (is_calendar_id_null != nullptr) {
        is_calendar_id_null.reset(nullptr);
    }
}

void WeekdayCalendarAggregateFunction::create(FunctionContext* ctx, AggDataPtr __restrict ptr) const {
    auto num = ctx->get_num_args();
    DCHECK(num == 4);
    auto* state = new(ptr) WeekdayCalendarAggregateState;
    state->weekday = std::make_unique<BinaryColumn>();
    state->shift_begin = std::make_unique<Int64Column>();
    state->shift_end = std::make_unique<Int64Column>();
    state->calendar_id = std::make_unique<BinaryColumn>();
    state->is_calendar_id_null = std::make_unique<BooleanColumn>();
}

void
WeekdayCalendarAggregateFunction::reset(FunctionContext* ctx, const Columns& args, AggDataPtr __restrict state) const {
    auto& state_impl = this->data(state);
    if (state_impl.weekday != nullptr) {
        state_impl.weekday.reset(nullptr);
    }
    if (state_impl.shift_begin != nullptr) {
        state_impl.shift_begin.reset(nullptr);
    }
    if (state_impl.shift_end != nullptr) {
        state_impl.shift_end.reset(nullptr);
    }
    if (state_impl.calendar_id != nullptr) {
        state_impl.calendar_id.reset(nullptr);
    }
    if (state_impl.is_calendar_id_null != nullptr) {
        state_impl.is_calendar_id_null.reset(nullptr);
    }
}

void WeekdayCalendarAggregateFunction::update(FunctionContext* ctx, const Column** columns, AggDataPtr __restrict state,
                                              size_t row_num) const {
    DCHECK(ctx->get_num_args() == 4);
    for (auto i = 0; i < ctx->get_num_args(); ++i) {
        if (UNLIKELY(columns[i]->size() <= row_num)) {
            ctx->set_error(std::string(get_name() + "'s update row number overflow").c_str(), false);
            return;
        }
    }
    // If weekday, shift_begin or shift_end is NULL, ignore.
    for (auto i = 0; i < 3; ++i) {
        if ((columns[i]->is_nullable() && columns[i]->is_null(row_num)) || columns[i]->only_null()) {
            return;
        }
    }

    auto& state_impl = this->data(state);
    state_impl.weekday->append_datum(columns[0]->get(row_num));
    if (std::holds_alternative<Slice>(columns[1]->get(row_num).convert2DatumKey())) {
        state_impl.shift_begin->append_datum(Datum(to_millis(columns[1]->get(row_num).get_slice().to_string())));
    } else {
        state_impl.shift_begin->append_datum(columns[1]->get(row_num));
    }
    if (std::holds_alternative<Slice>(columns[2]->get(row_num).convert2DatumKey())) {
        state_impl.shift_end->append_datum(Datum(to_millis(columns[2]->get(row_num).get_slice().to_string())));
    } else {
        state_impl.shift_end->append_datum(columns[2]->get(row_num));
    }
    if (columns[3]->get(row_num).is_null()) {
        state_impl.calendar_id->append_default();
        state_impl.is_calendar_id_null->append_datum(true);
    } else {
        state_impl.calendar_id->append_datum(columns[3]->get(row_num));
        state_impl.is_calendar_id_null->append_datum(false);
    }
}

void WeekdayCalendarAggregateFunction::merge(FunctionContext* ctx, const Column* column, AggDataPtr __restrict state,
                                             size_t row_num) const {
    auto& input_columns = down_cast<const StructColumn*>(ColumnHelper::get_data_column(column))->fields();
    auto& state_impl = this->data(state);

    auto weekday_column = down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_columns.at(0).get()));
    auto shift_begin_column = down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_columns.at(1).get()));
    auto shift_end_column = down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_columns.at(2).get()));
    auto calendar_id_column = down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_columns.at(3).get()));
    auto is_calendar_id_null_column = down_cast<const ArrayColumn*>(ColumnHelper::get_data_column(input_columns.at(4).get()));
    auto& offsets = weekday_column->offsets().get_data();
    const auto start = offsets[row_num];
    const auto end = offsets[row_num + 1];
    for (auto i = start; i < end; ++i) {
        state_impl.weekday->append_datum(weekday_column->elements().get(i));
        state_impl.shift_begin->append_datum(shift_begin_column->elements().get(i));
        state_impl.shift_end->append_datum(shift_end_column->elements().get(i));
        state_impl.calendar_id->append_datum(calendar_id_column->elements().get(i));
        state_impl.is_calendar_id_null->append_datum(is_calendar_id_null_column->elements().get(i));
    }
}

void WeekdayCalendarAggregateFunction::serialize_to_column(starrocks::FunctionContext* ctx,
                                                           __restrict starrocks::ConstAggDataPtr state,
                                                           starrocks::Column* to) const {
    auto& state_impl = this->data(state);
    auto& columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(to))->fields_column();
    if (!state_impl.weekday->empty()) {
        if (to->is_nullable()) {
            down_cast<NullableColumn*>(to)->null_column_data().emplace_back(0);
        }
        ::starrocks::serialize_to_column(state_impl.weekday, columns.at(0));
        ::starrocks::serialize_to_column(state_impl.shift_begin, columns.at(1));
        ::starrocks::serialize_to_column(state_impl.shift_end, columns.at(2));
        ::starrocks::serialize_to_column(state_impl.calendar_id, columns.at(3));
        ::starrocks::serialize_to_column(state_impl.is_calendar_id_null, columns.at(4));
    }
}

void WeekdayCalendarAggregateFunction::finalize_to_column(FunctionContext* ctx, ConstAggDataPtr __restrict state,
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
    const auto size = state_impl.weekday->size();
    DCHECK(state_impl.shift_begin->size() == size);
    DCHECK(state_impl.shift_end->size() == size);
    DCHECK(state_impl.calendar_id->size() == size);
    DCHECK(state_impl.is_calendar_id_null->size() == size);
    std::map<std::optional<std::string>, std::map<std::string, std::vector<std::pair<int64_t, int64_t>>>> id_to_entries;
    celonis::accelerator::Calendar calendar_proto;
    for (int i = 0; i < size; ++i) {
        const std::string weekday = state_impl.weekday->get(i).get_slice().to_string();
        if (weekday != "MONDAY" && weekday != "TUESDAY" && weekday != "WEDNESDAY" && weekday != "THURSDAY" &&
            weekday != "FRIDAY" && weekday != "SATURDAY" && weekday != "SUNDAY") {
            continue;
        }
        const int64_t shift_begin = state_impl.shift_begin->get(i).get_int64();
        const int64_t shift_end = state_impl.shift_end->get(i).get_int64();
        if (shift_begin < 0 || shift_begin > NUM_MILLISECONDS_PER_DAY || shift_end < 0 ||
            shift_end > NUM_MILLISECONDS_PER_DAY) {
            continue;
        }
        std::optional<std::string> calendar_id = std::nullopt;
        if (!static_cast<bool>(state_impl.is_calendar_id_null->get(i).get_uint8())) {
            calendar_id = state_impl.calendar_id->get(i).get_slice().to_string();
        }
        id_to_entries[calendar_id][weekday].emplace_back(shift_begin, shift_end);
    }

    for (auto& [id, entries]: id_to_entries) {
        while (true) {
            celonis::accelerator::WeekdayCalendar weekday_calendar;
            bool is_empty = true;
            for (auto& [day, pairs]: entries) {
                if (pairs.empty()) {
                    continue;
                }
                celonis::accelerator::WeekdayCalendarEntry weekday_calendar_entry;
                is_empty = false;
                int64_t begin = pairs.back().first;
                int64_t end = pairs.back().second;
                pairs.pop_back();
                weekday_calendar_entry.set_use_day(true);
                weekday_calendar_entry.mutable_shift()->set_begin(begin);
                weekday_calendar_entry.mutable_shift()->set_end(end);
                if (day == "MONDAY") {
                    *weekday_calendar.mutable_monday() = weekday_calendar_entry;
                } else if (day == "TUESDAY") {
                    *weekday_calendar.mutable_tuesday() = weekday_calendar_entry;
                } else if (day == "WEDNESDAY") {
                    *weekday_calendar.mutable_wednesday() = weekday_calendar_entry;
                } else if (day == "THURSDAY") {
                    *weekday_calendar.mutable_thursday() = weekday_calendar_entry;
                } else if (day == "FRIDAY") {
                    *weekday_calendar.mutable_friday() = weekday_calendar_entry;
                } else if (day == "SATURDAY") {
                    *weekday_calendar.mutable_saturday() = weekday_calendar_entry;
                } else {
                    *weekday_calendar.mutable_sunday() = weekday_calendar_entry;
                }
            }
            // set calendar_id
            if (id.has_value()) {
                weekday_calendar.set_calendar_id(id.value());
            }
            if (!is_empty) {
                *calendar_proto.mutable_multi_weekday_calendar()->add_calendars() = weekday_calendar;
            }
            if (is_empty) {
                break;
            }
        }
    }
    std::optional<std::string> calendar_string = to_base64_encoded_string(calendar_proto,
                                                                          DEFAULT_CELONIS_PROTO_SIZE_LIMIT, false);
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
void WeekdayCalendarAggregateFunction::convert_to_serialize_format(FunctionContext* ctx, const Columns& src,
                                                                   size_t chunk_size,
                                                                   ColumnPtr* dst) const {
    DCHECK(src.size() == 4);
    std::vector<size_t> valid_indexes;
    for (size_t row = 0; row < chunk_size; ++row) {
        if ((src[0]->is_nullable() && src[0]->is_null(row)) || src[0]->only_null()) {
            continue;
        }
        if ((src[1]->is_nullable() && src[1]->is_null(row)) || src[1]->only_null()) {
            continue;
        }
        if ((src[2]->is_nullable() && src[2]->is_null(row)) || src[2]->only_null()) {
            continue;
        }
        valid_indexes.push_back(row);
    }
    auto columns = down_cast<StructColumn*>(ColumnHelper::get_data_column(dst->get()))->fields_column();
    if (!valid_indexes.empty()) {
        if (dst->get()->is_nullable()) {
            down_cast<NullableColumn*>(dst->get())->null_column_data().emplace_back(0);
        }
        DatumArray weekday_array;
        DatumArray shift_begin_array;
        DatumArray shift_end_array;
        DatumArray calendar_id_array;
        DatumArray is_calendar_id_null_array;
        for (auto i : valid_indexes) {
            weekday_array.push_back(src[0]->get(i));
            if (src[3]->is_null(i)) {
                calendar_id_array.push_back("");
                is_calendar_id_null_array.push_back(true);
            } else {
                calendar_id_array.push_back(src[3]->get(i));
                is_calendar_id_null_array.push_back(false);
            }
        }

        if (std::holds_alternative<Slice>(src[1]->get(valid_indexes.front()).convert2DatumKey())) {
            for (auto i: valid_indexes) {
                shift_begin_array.push_back(Datum(to_millis(src[1]->get(i).get_slice().to_string())));
            }
        } else {
            for (auto i: valid_indexes) {
                shift_begin_array.push_back(src[1]->get(i));
            }
        }
        if (std::holds_alternative<Slice>(src[2]->get(valid_indexes.front()).convert2DatumKey())) {
            for (auto i: valid_indexes) {
                shift_end_array.push_back(Datum(to_millis(src[2]->get(i).get_slice().to_string())));
            }
        } else {
            for (auto i: valid_indexes) {
                shift_end_array.push_back(src[2]->get(i));
            }
        }
        columns[0]->append_datum(weekday_array);
        columns[1]->append_datum(shift_begin_array);
        columns[2]->append_datum(shift_end_array);
        columns[3]->append_datum(calendar_id_array);
        columns[4]->append_datum(is_calendar_id_null_array);
    }
}

std::string WeekdayCalendarAggregateFunction::get_name() const { return "celonis_make_weekday_calendar"; }

} // namespace starrocks
