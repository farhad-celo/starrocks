#pragma once

#include <boost/date_time/gregorian/gregorian.hpp>

#include "column/column_viewer.h"
#include "column/type_traits.h"
#include "common/config.h"
#include "exprs/table_function/table_function.h"
#include "runtime/integer_overflow_arithmetics.h"
#include "types/logical_type.h"

namespace starrocks {

template <LogicalType LT, LogicalType StepLT>
class CelonisGenerateRange final : public TableFunction {
    struct MyState final : public TableFunctionState {
        ~MyState() override = default;

        void on_new_params() override { set_offset(0); }
    };

public:
    ~CelonisGenerateRange() override = default;

    Status init(const TFunction& fn, TableFunctionState** state) const override {
        *state = new MyState();
        return Status::OK();
    }

    Status prepare(TableFunctionState* /*state*/) const override { return Status::OK(); }

    Status open(RuntimeState* /*runtime_state*/, TableFunctionState* /*state*/) const override { return Status::OK(); }

    Status close(RuntimeState* /*runtime_state*/, TableFunctionState* state) const override {
        delete state;
        return Status::OK();
    }

    std::pair<Columns, UInt32Column::Ptr> process(RuntimeState* runtime_state,
                                                  TableFunctionState* base_state) const override {
        using CppType = RunTimeCppType<LT>;
        auto max_chunk_size = runtime_state->chunk_size();
        auto state = down_cast<MyState*>(base_state);
        auto res = RunTimeColumnType<LT>::create();
        auto offsets = UInt32Column::create();
        auto arg_step = ColumnViewer<StepLT>(state->get_columns()[0]);
        auto arg_range_start = ColumnViewer<LT>(state->get_columns()[1]);
        auto arg_range_end = ColumnViewer<LT>(state->get_columns()[2]);
        auto curr_row = state->processed_rows();

        auto move_to_next_row = [&]() {
            curr_row++;
            state->set_processed_rows(curr_row);
            state->set_offset(0);
        };

        while (res->size() < max_chunk_size && curr_row < arg_range_start.size()) {
            offsets->append(res->size());
            if (arg_range_start.is_null(curr_row) || arg_range_end.is_null(curr_row) || arg_step.is_null(curr_row)) {
                move_to_next_row();
            } else {
                auto range_start = arg_range_start.value(curr_row);
                auto range_end = arg_range_end.value(curr_row);
                if (range_start > range_end) {
                    move_to_next_row();
                    continue;
                }
                auto step = arg_step.value(curr_row);
                auto current = range_start;
                // For TYPE_DATETIME
                std::function<TimestampValue(const TimestampValue&)> increase_timestamp_func;

                if constexpr (LT == TYPE_DATETIME) {
                    increase_timestamp_func = get_increase_timestamp_func(range_start, step, state);
                    if (increase_timestamp_func == nullptr) {
                        break;
                    }
                    current.set_timestamp(range_start.timestamp() + state->get_offset());
                    if (!current.is_valid() || current > range_end) {
                        move_to_next_row();
                        continue;
                    }
                } else {
                    if (step <= 0) {
                        state->set_status(Status::InvalidArgument("step size must be positive"));
                        break;
                    }
                    auto offset = static_cast<CppType>(state->get_offset());
                    if (add_overflow(range_start, offset, &current) || current > range_end) {
                        move_to_next_row();
                        continue;
                    }
                }

                bool overflow = false;
                if constexpr (LT == TYPE_DATETIME) {
                    auto& data = res->get_data();
                    auto count = max_chunk_size - res->size();
                    for (decltype(count) i = 0; i < count; i++) {
                        data.push_back(current);
                        current = increase_timestamp_func(current);
                        overflow = !current.is_valid();
                        if (current > range_end || overflow) {
                            break;
                        }
                    }
                } else {
                    auto count = (range_end - current) / step + 1;
                    if (count > max_chunk_size - res->size()) {
                        count = max_chunk_size - res->size();
                    }

                    auto old_size = res->size();
                    resize_column_uninitialized(res.get(), old_size + count);
                    auto *data = res->get_data().data();
                    for (decltype(count) i = 0; i < count; i++) {
                        data[old_size + i] = current;
                        overflow = add_overflow(current, step, &current);
                        if (overflow) {
                            break;
                        }
                    }
                }

                if (current > range_end || overflow) {
                    move_to_next_row();
                } else {
                    if constexpr (LT == TYPE_DATETIME) {
                        state->set_offset(current.timestamp() - range_start.timestamp());
                    } else {
                        state->set_offset(current - range_start);
                    }
                }
            }
        } // while
        offsets->append(res->size());
        return std::make_pair(Columns{res}, offsets);
    }

private:
    static void resize_column_uninitialized(Column* column, size_t new_size) {
        if (column->size() == 0) {
            column->resize_uninitialized(new_size);
        } else {
            column->resize(new_size);
        }
    }

    std::function<TimestampValue(const TimestampValue&)> get_increase_timestamp_months_func(
            const TimestampValue& range_start, int count) const {
        JulianDate range_start_date = timestamp::to_julian(range_start.timestamp());

        int year, month, range_start_day;
        date::to_date_with_cache(range_start_date, &year, &month, &range_start_day);
        int max_day = DAYS_IN_MONTH[date::is_leap(year)][month];
        bool is_max_day = range_start_day == max_day;

        return [count, range_start_day, is_max_day](TimestampValue tv) {
            Timestamp timestamp = tv.timestamp();
            JulianDate julian = timestamp::to_julian(timestamp);
            int year, month, day;
            date::to_date_with_cache(julian, &year, &month, &day);

            int months = year * 12 + month - 1 + count;
            if (months < 0) {
                // @INFO: NOT SUPPORT BCE
                julian = date::INVALID_DATE;
            } else {
                year = months / 12;
                month = (months % 12) + 1;
                int max_day = DAYS_IN_MONTH[date::is_leap(year)][month];
                if (is_max_day) {
                    day = max_day;
                } else {
                    day = range_start_day > max_day ? max_day : range_start_day;
                }
                julian = date::from_date(year, month, day);
            }
            if (julian > date::MAX_DATE || julian < date::MIN_DATE) {
                julian = date::INVALID_DATE;
            }

            return TimestampValue{date::to_timestamp(julian) | timestamp::to_time(timestamp)};
        };
    }

    // Returns a function increasing timestamp by a step. Returns nullptr and sets status of state if there is an error.
    std::function<TimestampValue(const TimestampValue&)> get_increase_timestamp_func(
            const TimestampValue& range_start, Slice step_slice, MyState* state) const {
        char* endptr;
        int64_t step = strtol(step_slice.get_data(), &endptr, 10);
        if (step <= 0) {
            state->set_status(Status::InternalError("step size must be positive"));
            return nullptr;
        }
        switch (*endptr) {
            case 'm':
                return [step](TimestampValue tv) {
                    return TimestampValue{timestamp::add<TimeUnit::MINUTE>(tv.timestamp(), step)};
                };
            case 'h':
                return [step](TimestampValue tv) {
                    return TimestampValue{timestamp::add<TimeUnit::HOUR>(tv.timestamp(), step)};
                };
            case 'D':
                return [step](TimestampValue tv) {
                    return TimestampValue{timestamp::add<TimeUnit::DAY>(tv.timestamp(), step)};
                };
            case 'W':
                return [step](TimestampValue tv) {
                    return TimestampValue{timestamp::add<TimeUnit::WEEK>(tv.timestamp(), step)};
                };
            case 'M':
                return get_increase_timestamp_months_func(range_start, step);
            case 'Q':
                return get_increase_timestamp_months_func(range_start, step * 3);
            case 'Y':
                return get_increase_timestamp_months_func(range_start, step * 12);
            default:
                state->set_status(Status::InternalError("Invalid step unit. It must be [mhDMYWQ]"));
                return nullptr;
        }
    }
};

} // namespace starrocks
