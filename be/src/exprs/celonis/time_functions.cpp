#include "exprs/celonis/time_functions.h"

#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/array_column.h"
#include "google/protobuf/util/json_util.h"
#include "modules/query/calendars.pb.h"


namespace starrocks {

namespace {
static const int64_t NANOS_PER_MILLIS = 1000000;

static const std::unordered_map<std::string, int64_t> TIME_UNIT_TO_MS = {
        {"DAYS",         86400000L},
        {"HOURS",        3600000L},
        {"MINUTES",      60000L},
        {"SECONDS",      1000L},
        {"MILLISECONDS", 1L}
};

static const int NUM_DAYS_PER_WEEK = 7;

static const int NUM_MILLISECONDS_PER_DAY = 86400000;

static const int NUM_MICROSECONDS_PER_MILLISECONDS = 1000;
}

StatusOr<ColumnPtr>
CelonisTimeFunctions::timestamp_millis([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 1);

    RETURN_IF_COLUMNS_ONLY_NULL(columns);

    ColumnViewer<TYPE_BIGINT> data_column(columns[0]);

    auto size = columns[0]->size();
    ColumnBuilder<TYPE_DATETIME> result(size);
    for (int row = 0; row < size; ++row) {
        if (data_column.is_null(row)) {
            result.append_null();
            continue;
        }

        auto unix_millis = data_column.value(row);
        if (unix_millis < 0) {
            result.append_null();
            continue;
        }

        int64 seconds = unix_millis / 1000;
        int64 millis = unix_millis % 1000;
        int64 nanoseconds = millis * NANOS_PER_MILLIS;

        Timestamp t;
        int days = seconds / SECS_PER_DAY;
        JulianDate jd = days + date::UNIX_EPOCH_JULIAN;
        t = timestamp::from_julian_and_time(jd,
                                            seconds % SECS_PER_DAY * USECS_PER_SEC + nanoseconds / NANOSECS_PER_USEC);
        TimestampValue ts;
        ts.set_timestamp(t);
        result.append(ts);
    }

    return result.build(ColumnHelper::is_all_const(columns));
}

struct TimeRange {
    int64_t begin_ms;
    int64_t end_ms;

    TimeRange(int64_t begin_ms, int64_t end_ms) : begin_ms(begin_ms), end_ms(end_ms) {}

    virtual ~TimeRange() = default;

    // Computes the overlap in milliseconds with another TimeRange object.
    virtual int64_t compute_overlap(const TimeRange& other) const {
        int64_t start = std::max(begin_ms, other.begin_ms);
        int64_t end = std::min(end_ms, other.end_ms);
        return (end > start) ? end - start : 0;
    }
};

struct PeriodicTimeRange : public TimeRange {
    int64_t period_ms;

    PeriodicTimeRange(int64_t begin_ms, int64_t end_ms, int64_t period_ms) : TimeRange(begin_ms, end_ms),
                                                                             period_ms(period_ms) {}

    ~PeriodicTimeRange() override = default;

    // Computes the overlap in milliseconds with a TimeRange object.
    int64_t compute_overlap(const TimeRange& time_range) const override {
        const int64_t begin = time_range.begin_ms;
        const int64_t end = time_range.end_ms;
        int64_t cur_begin = begin_ms;
        int64_t cur_end = end_ms;
        int64_t abs_period = std::abs(period_ms);
        // move [cur_begin, cur_end) to the left of [begin, end)
        if (cur_end > begin) {
            int64_t n_periods = (cur_end - begin + abs_period - 1) / abs_period;
            cur_begin -= n_periods * abs_period;
            cur_end -= n_periods * abs_period;
        }
        DCHECK(abs_period > 0);
        int64_t rv = 0L;
        // move [cur_begin, cur_end) to right to pass [begin, end)
        while (true) {
            if (cur_begin >= end) {
                break;
            }
            int64_t left = std::max(cur_begin, begin);
            int64_t right = std::min(cur_end, end);
            rv += (right > left) ? (right - left) : 0L;
            cur_begin += abs_period;
            cur_end += abs_period;
        }
        return rv;
    }
};

static int64_t remap_timestamp_ms(const TimestampValue& timestamp) {
    TimestampValue epoch = TimestampValue::create(1970, 1, 1, 0, 0, 0);
    return timestamp.diff_microsecond(epoch) / NUM_MICROSECONDS_PER_MILLISECONDS;
}

static TimeRange get_time_range(const TimestampValue& timestamp) {
    int64_t milliseconds = remap_timestamp_ms(timestamp);
    if (milliseconds >= 0) {
        return TimeRange{0L, milliseconds};
    } else {
        return TimeRange{milliseconds, 0L};
    }
}

class Calendar {
public:
    Calendar(const celonis::accelerator::Calendar& calendar_proto) {
        if (calendar_proto.has_weekday_calendar()) {
            handle_weekday_calendar(calendar_proto.weekday_calendar());
        }
    }

    int64_t remap_timestamp_ms(const TimestampValue& timestamp) {
        const TimestampValue epoch = TimestampValue::create(1970, 1, 1, 0, 0, 0);
        const bool before_epoch = timestamp < epoch;
        const TimeRange time_range = get_time_range(timestamp);
        int64_t rv = 0L;
        for (const auto& cur_time_range: time_ranges_) {
            rv += cur_time_range->compute_overlap(time_range);
        }
        return before_epoch ? -rv : rv;
    }

private:
    void handle_weekday_calendar(const celonis::accelerator::WeekdayCalendar& weekday_calendar) {
        handle_weekday_calendar_entry(weekday_calendar.monday(), 0);
        handle_weekday_calendar_entry(weekday_calendar.tuesday(), 1);
        handle_weekday_calendar_entry(weekday_calendar.wednesday(), 2);
        handle_weekday_calendar_entry(weekday_calendar.thursday(), 3);
        handle_weekday_calendar_entry(weekday_calendar.friday(), 4);
        handle_weekday_calendar_entry(weekday_calendar.saturday(), 5);
        handle_weekday_calendar_entry(weekday_calendar.sunday(), 6);
    }

    void handle_weekday_calendar_entry(const celonis::accelerator::WeekdayCalendarEntry& entry, int index) {
        int64_t begin = entry.shift().begin();
        int64_t end = entry.shift().end();
        if (entry.use_day() && begin < end) {
            int64_t period = NUM_DAYS_PER_WEEK * NUM_MILLISECONDS_PER_DAY;
            std::vector<int> add_days = {4, 5, 6, 0, 1, 2, 3};
            begin += NUM_MILLISECONDS_PER_DAY * add_days.at(index);
            end += NUM_MILLISECONDS_PER_DAY * add_days.at(index);
            time_ranges_.push_back(std::make_shared<PeriodicTimeRange>(begin, end, period));
        }
    }

    std::vector<std::shared_ptr<TimeRange>> time_ranges_;
};

bool json_string_to_calendar(const std::string& calendar_json_string, celonis::accelerator::Calendar& calendar) {
    auto status = google::protobuf::util::JsonStringToMessage(calendar_json_string, &calendar);
    return status.ok();
}

Status validate_weekday_calendar_entry(const celonis::accelerator::WeekdayCalendarEntry& entry) {
    int64_t begin = entry.shift().begin();
    int64_t end = entry.shift().end();
    if (end < begin) {
        return Status::InvalidArgument("shift begin is greater than shift end in weekday calendar.");
    }
    if (begin < 0) {
        return Status::InvalidArgument("shift begin is negative in weekday calendar.");
    }
    if (end > NUM_MILLISECONDS_PER_DAY) {
        return Status::InvalidArgument("shift end is greater than " + std::to_string(NUM_MILLISECONDS_PER_DAY) +
                                       " milliseconds in weekday calendar.");
    }
    return Status::OK();
}

Status validate_calendar(const celonis::accelerator::Calendar& calendar) {
    if (!calendar.has_weekday_calendar()) {
        return Status::InvalidArgument("Non weekday calendar is not supported.");
    }
    const celonis::accelerator::WeekdayCalendar& weekday_calendar = calendar.weekday_calendar();
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.monday()));
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.tuesday()));
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.wednesday()));
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.thursday()));
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.friday()));
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.saturday()));
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.sunday()));
    return Status::OK();
}

static StatusOr<int64_t> convert_time_unit(const std::string& time_unit, int64_t milliseconds) {
    auto iter = TIME_UNIT_TO_MS.find(time_unit);
    if (iter != TIME_UNIT_TO_MS.end()) {
        return milliseconds / iter->second;
    } else {
        return Status::InvalidArgument("Unknown time_unit: " + time_unit);
    }
}

static StatusOr<std::optional<int64_t>>
remap_timestamp_calendar(const TimestampValue& timestamp, const std::string& time_unit,
                         const std::string& calendar_json_string,
                         const std::optional<std::string>& calendar_id_column) {
    int64_t milliseconds = 0L;
    celonis::accelerator::Calendar calendar_proto;
    if (calendar_json_string.empty()) {
        if (calendar_id_column.has_value()) {
            return Status::InvalidArgument(
                    "Calendar ID column should not be set when calendar specification is not set.");
        }
        milliseconds = remap_timestamp_ms(timestamp);
    } else {
        // parse calendar_json_string
        if (!json_string_to_calendar(calendar_json_string, calendar_proto)) {
            return Status::InvalidArgument("Calendar specification column is malformed.");
        }
        RETURN_IF_ERROR(validate_calendar(calendar_proto));
        if (calendar_id_column.has_value()) {
            return Status::InvalidArgument("Calendar ID column should not be set for weekday calendar.");
        }
        Calendar calendar(calendar_proto);
        milliseconds = calendar.remap_timestamp_ms(timestamp);
    }
    return convert_time_unit(time_unit, milliseconds);
}

StatusOr<ColumnPtr> CelonisTimeFunctions::remap_timestamps_calendar([[maybe_unused]] FunctionContext* context,
                                                                    const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 4);
    size_t n_rows = columns[0]->size();
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[1]);
    ColumnViewer calendar_id_column_viewer = ColumnViewer<TYPE_VARCHAR>(columns[3]);
    ColumnPtr calendar_column_ptr = ColumnHelper::unpack_and_duplicate_const_column(n_rows, columns[2]);
    ColumnPtr calendar_array_data = calendar_column_ptr;
    if (calendar_column_ptr->is_nullable()) {
        calendar_array_data = down_cast<const NullableColumn*>(calendar_column_ptr.get())->data_column();
    }
    const auto& calendar_element_column = down_cast<ArrayColumn*>(calendar_array_data.get())->elements();
    if (calendar_element_column.has_null()) {
        return Status::InvalidArgument("Calendar array should not have null elements.");
    }
    const auto& calendar_data_column = down_cast<const NullableColumn&>(calendar_element_column).data_column_ref();
    const auto& calendar_data = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(calendar_data_column).get_data();
    const auto& calendar_offsets = down_cast<const ArrayColumn*>(
            calendar_array_data.get())->offsets().get_data().data();

    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    for (size_t row = 0; row < n_rows; ++row) {
        if (timestamp_viewer.is_null(row) || time_unit_viewer.is_null(row) || calendar_column_ptr->is_null(row)) {
            result.append_null();
            continue;
        }
        std::string time_unit = time_unit_viewer.value(row).to_string();
        if (TIME_UNIT_TO_MS.find(time_unit) == TIME_UNIT_TO_MS.end()) {
            return Status::InvalidArgument("time unit must be one of DAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
        }
        auto timestamp = (TimestampValue) timestamp_viewer.value(row);
        size_t start = calendar_offsets[row];
        size_t end = calendar_offsets[row + 1];
        std::string calendar_json_string;
        std::optional<std::string> calendar_id_column = std::nullopt;
        if (!calendar_id_column_viewer.is_null(row)) {
            calendar_id_column = calendar_id_column_viewer.value(row).to_string();
        }
        for (size_t id = start; id < end; ++id) {
            calendar_json_string += calendar_data[id].to_string();
        }
        StatusOr<std::optional<int64_t>> status_or_time = remap_timestamp_calendar(timestamp, time_unit,
                                                                                   calendar_json_string,
                                                                                   calendar_id_column);
        if (status_or_time.ok()) {
            if (status_or_time.value().has_value()) {
                result.append(status_or_time.value().value());
            } else {
                result.append_null();
            }
        } else {
            return status_or_time.status();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

} // namespace starrocks
