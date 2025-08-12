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

struct PeriodicTimeRange {
    int64_t begin_ms;
    int64_t end_ms;
    int64_t period_ms;
};

struct TimeRange {
    int64_t begin_ms;
    int64_t end_ms;
};

static int64_t compute_time_range_overlap(const PeriodicTimeRange& periodic_time_range, const TimeRange& time_range) {
    int64_t begin = time_range.begin_ms;
    int64_t end = time_range.end_ms;
    int64_t period = periodic_time_range.period_ms;
    int64_t cur_begin = periodic_time_range.begin_ms;
    int64_t cur_end = periodic_time_range.end_ms;
    DCHECK(period != 0);
    int64_t rv = 0L;
    while (true) {
        if ((period > 0 && cur_begin >= end) || (period < 0 && cur_end <= begin)) {
            break;
        }
        int64_t left = std::max(cur_begin, begin);
        int64_t right = std::min(cur_end, end);
        rv += (right > left) ? (right - left) : 0L;
        cur_begin += period;
        cur_end += period;
    }
    return rv;
}

bool json_string_to_calendar(const std::string& calendar_json_string, celonis::accelerator::Calendar& calendar) {
    auto status = google::protobuf::util::JsonStringToMessage(calendar_json_string, &calendar);
    return status.ok();
}

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

static StatusOr<int64_t>
remap_timestamp_weekday_ms(const celonis::accelerator::WeekdayCalendarEntry& entry, int index,
                           const TimeRange& time_range,
                           bool before_epoch) {
    if (!entry.use_day() || entry.shift().begin() == entry.shift().end()) {
        return 0L;
    }
    int64_t period = NUM_DAYS_PER_WEEK * NUM_MILLISECONDS_PER_DAY;
    if (before_epoch) {
        period = -period;
    }
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
    std::vector<int> add_days = {4, 5, 6, 0, 1, 2, 3};
    begin += NUM_MILLISECONDS_PER_DAY * add_days.at(index);
    end += NUM_MILLISECONDS_PER_DAY * add_days.at(index);
    return compute_time_range_overlap(PeriodicTimeRange{begin, end, period}, time_range);
}

static StatusOr<int64_t> remap_timestamp_weekday_calendar_ms(const TimestampValue& timestamp,
                                                             const celonis::accelerator::WeekdayCalendar& weekday_calendar) {
    const TimestampValue epoch = TimestampValue::create(1970, 1, 1, 0, 0, 0);
    const bool before_epoch = timestamp < epoch;
    int64_t rv = 0L;
    const TimeRange time_range = get_time_range(timestamp);
    int64_t overlap = 0L;
    ASSIGN_OR_RETURN(overlap, remap_timestamp_weekday_ms(weekday_calendar.monday(), 0, time_range, before_epoch));
    rv += overlap;
    ASSIGN_OR_RETURN(overlap, remap_timestamp_weekday_ms(weekday_calendar.tuesday(), 1, time_range, before_epoch));
    rv += overlap;
    ASSIGN_OR_RETURN(overlap, remap_timestamp_weekday_ms(weekday_calendar.wednesday(), 2, time_range, before_epoch));
    rv += overlap;
    ASSIGN_OR_RETURN(overlap, remap_timestamp_weekday_ms(weekday_calendar.thursday(), 3, time_range, before_epoch));
    rv += overlap;
    ASSIGN_OR_RETURN(overlap, remap_timestamp_weekday_ms(weekday_calendar.friday(), 4, time_range, before_epoch));
    rv += overlap;
    ASSIGN_OR_RETURN(overlap, remap_timestamp_weekday_ms(weekday_calendar.saturday(), 5, time_range, before_epoch));
    rv += overlap;
    ASSIGN_OR_RETURN(overlap, remap_timestamp_weekday_ms(weekday_calendar.sunday(), 6, time_range, before_epoch));
    rv += overlap;
    return before_epoch ? -rv : rv;
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
    celonis::accelerator::Calendar calendar;
    if (calendar_json_string.empty()) {
        if (calendar_id_column.has_value()) {
            return Status::InvalidArgument(
                    "Calendar ID column should not be set when calendar specification is not set.");
        }
        milliseconds = remap_timestamp_ms(timestamp);
    } else {
        // parse calendar_json_string
        if (!json_string_to_calendar(calendar_json_string, calendar)) {
            return Status::InvalidArgument("Calendar specification column is malformed.");
        }
        if (!calendar.has_weekday_calendar()) {
            return Status::InvalidArgument("Non weekday calendar is not supported.");
        }
        if (calendar_id_column.has_value()) {
            return Status::InvalidArgument("Calendar ID column should not be set for weekday calendar.");
        }
        ASSIGN_OR_RETURN(milliseconds, remap_timestamp_weekday_calendar_ms(timestamp, calendar.weekday_calendar()));
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
