#include "exprs/celonis/time_functions.h"

#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/array_column.h"
#include "exprs/celonis/util.h"
#include "google/protobuf/util/json_util.h"
#include "modules/query/calendars.pb.h"

#include <bitset>

namespace starrocks {

namespace {
static const int64_t NANOS_PER_MILLIS = 1000000;

static const std::unordered_map<std::string, int64_t> TIME_UNIT_TO_MS = {
        {"DAYS",         86400000L},
        {"WORKDAYS",     86400000L},
        {"HOURS",        3600000L},
        {"MINUTES",      60000L},
        {"SECONDS",      1000L},
        {"MILLISECONDS", 1L}
};

static const int64_t NUM_DAYS_PER_WEEK = 7L;

static const int64_t NUM_MILLISECONDS_PER_DAY = 86400000L;

static const int64_t NUM_MICROSECONDS_PER_MILLISECONDS = 1000L;

// SR places an upper limit of 1M of STRING. We use 900K which is less than 1M.
static const size_t MAX_STRING_SIZE = 900000;
}

static void truncate_timestamp(const std::string& time_unit, TimestampValue& timestamp) {
    if (time_unit == "DAYS" || time_unit == "WORKDAYS") {
        timestamp.trunc_to_day();
    } else if (time_unit == "HOURS") {
        timestamp.trunc_to_hour();
    } else if (time_unit == "MINUTES") {
        timestamp.trunc_to_minute();
    } else if (time_unit == "SECONDS") {
        timestamp.trunc_to_second();
    }
}

static int64_t floor_to_nearest_multiple(int64_t num, int64_t multiple) {
    if (multiple == 0) {
        return num;
    }
    int64_t floored_num = (num / multiple) * multiple;
    if (num < 0 && num % multiple != 0) {
        floored_num -= multiple;
    }
    return floored_num;
}

static int64_t ceil_to_nearest_multiple(int64_t num, int64_t multiple) {
    if (multiple == 0) {
        return num;
    }
    int64_t ceiled_num = (num / multiple) * multiple;
    if (num > 0 && num % multiple != 0) {
        ceiled_num += multiple;
    }
    return ceiled_num;
}

static void round_weekday_calendar_entry(celonis::accelerator::WeekdayCalendarEntry* entry, int64_t multiple) {
    if (entry->use_day() && entry->shift().begin() <= entry->shift().end()) {
        entry->mutable_shift()->set_begin(floor_to_nearest_multiple(entry->shift().begin(), multiple));
        entry->mutable_shift()->set_end(ceil_to_nearest_multiple(entry->shift().end(), multiple));
    }
}

static void round_weekday_calendar(celonis::accelerator::WeekdayCalendar& weekday_calendar, int64_t multiple) {
    round_weekday_calendar_entry(weekday_calendar.mutable_monday(), multiple);
    round_weekday_calendar_entry(weekday_calendar.mutable_tuesday(), multiple);
    round_weekday_calendar_entry(weekday_calendar.mutable_wednesday(), multiple);
    round_weekday_calendar_entry(weekday_calendar.mutable_thursday(), multiple);
    round_weekday_calendar_entry(weekday_calendar.mutable_friday(), multiple);
    round_weekday_calendar_entry(weekday_calendar.mutable_saturday(), multiple);
    round_weekday_calendar_entry(weekday_calendar.mutable_sunday(), multiple);
}

static void round_calendar(celonis::accelerator::Calendar& calendar_proto, int64_t multiple) {
    if (calendar_proto.has_weekday_calendar()) {
        round_weekday_calendar(*calendar_proto.mutable_weekday_calendar(), multiple);
    } else if (calendar_proto.has_multi_weekday_calendar()) {
        auto* multi_weekday_calendar = calendar_proto.mutable_multi_weekday_calendar();
        for (auto& weekday_calendar: *multi_weekday_calendar->mutable_calendars()) {
            round_weekday_calendar(weekday_calendar, multiple);
        }
    } else if (calendar_proto.has_factory_calendar()) {
        auto* factory_calendar = calendar_proto.mutable_factory_calendar();
        for (auto& entry: *factory_calendar->mutable_entries()) {
            entry.set_start_date(floor_to_nearest_multiple(entry.start_date(), multiple));
            entry.set_end_date(ceil_to_nearest_multiple(entry.end_date(), multiple));
        }
    } else if (calendar_proto.has_intersect_calendar()) {
        auto* intersect_calendar = calendar_proto.mutable_intersect_calendar();
        round_calendar(*intersect_calendar->mutable_calendar1(), multiple);
        round_calendar(*intersect_calendar->mutable_calendar2(), multiple);
    }
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

    TimeRange(const TimeRange& other) : begin_ms(other.begin_ms), end_ms(other.end_ms) {}

    TimeRange& operator=(const TimeRange& other) {
        if (this != &other) {
            begin_ms = other.begin_ms;
            end_ms = other.end_ms;
        }
        return *this;
    }

    virtual ~TimeRange() = default;

    // Computes the overlap in milliseconds with another TimeRange object.
    virtual int64_t compute_overlap(const TimeRange& other) const {
        int64_t start = std::max(begin_ms, other.begin_ms);
        int64_t end = std::min(end_ms, other.end_ms);
        return (end > start) ? end - start : 0;
    }

    virtual bool is_ms_in(int64_t ms) const {
        return ms >= begin_ms && ms <= end_ms;
    }
};

struct PeriodicTimeRange : public TimeRange {
    int64_t period_ms;

    PeriodicTimeRange(int64_t begin_ms, int64_t end_ms, int64_t period_ms) : TimeRange(begin_ms, end_ms),
                                                                             period_ms(period_ms) {}

    PeriodicTimeRange(const PeriodicTimeRange& other)
            : TimeRange(other), period_ms(other.period_ms) {}

    PeriodicTimeRange& operator=(const PeriodicTimeRange& other) {
        if (this != &other) {
            TimeRange::operator=(other); // Call base class assignment operator
            period_ms = other.period_ms;
        }
        return *this;
    }

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

    bool is_ms_in(int64_t ms) const override {
        int64_t diff_mod = (ms - begin_ms) % period_ms;
        if (diff_mod < 0) {
            diff_mod += period_ms;
        }
        int64_t adjusted_ms = begin_ms + diff_mod;
        return begin_ms <= adjusted_ms && adjusted_ms <= end_ms;
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

static void merge_time_ranges(std::vector<TimeRange>& time_ranges) {
    std::sort(time_ranges.begin(), time_ranges.end(),
              [](const TimeRange& a, const TimeRange& b) { return a.begin_ms < b.begin_ms; });
    std::vector<TimeRange> merged;
    for (const auto& time_range: time_ranges) {
        if (merged.empty() || time_range.begin_ms > merged.back().end_ms) {
            merged.push_back(time_range);
        } else {
            merged.back().end_ms = std::max(time_range.end_ms, merged.back().end_ms);
        }
    }
    time_ranges = merged;
}

class Calendar {
public:
    Calendar() = default;

    Calendar(const celonis::accelerator::Calendar& calendar_proto) {
        if (calendar_proto.has_multi_weekday_calendar()) {
            handle_multi_weekday_calendar(calendar_proto.multi_weekday_calendar());
        }
        if (calendar_proto.has_weekday_calendar()) {
            handle_weekday_calendar(calendar_proto.weekday_calendar());
        }
        if (calendar_proto.has_factory_calendar()) {
            handle_factory_calendar(calendar_proto.factory_calendar());
        }
        if (calendar_proto.has_workday_calendar()) {
            handle_workday_calendar(calendar_proto.workday_calendar());
        }
    }

    bool requires_calendar_id() const {
        return !id_to_time_ranges_.empty();
    }

    int64_t
    remap_timestamp_ms(const TimestampValue& timestamp, const std::optional<std::string>& calendar_id) const {
        const TimestampValue epoch = TimestampValue::create(1970, 1, 1, 0, 0, 0);
        const bool before_epoch = timestamp < epoch;
        const TimeRange time_range = get_time_range(timestamp);
        int64_t rv = 0L;
        if (!calendar_id.has_value()) {
            for (const auto& cur_time_range: time_ranges_) {
                rv += cur_time_range->compute_overlap(time_range);
            }
        } else {
            auto itr = id_to_time_ranges_.find(calendar_id.value());
            if (itr != id_to_time_ranges_.end()) {
                for (const auto& cur_time_range: itr->second) {
                    rv += cur_time_range->compute_overlap(time_range);
                }
            }
        }
        return before_epoch ? -rv : rv;
    }

    bool
    is_timestamp_in_calendar(const TimestampValue& timestamp,
                             const std::optional<std::string>& calendar_id) const {
        const TimestampValue epoch = TimestampValue::create(1970, 1, 1, 0, 0, 0);
        const int64_t ms = timestamp.diff_microsecond(epoch) / NUM_MICROSECONDS_PER_MILLISECONDS;

        if (!calendar_id.has_value()) {
            for (const auto& cur_time_range: time_ranges_) {
                if (cur_time_range->is_ms_in(ms)) {
                    return true;
                }
            }
        } else {
            auto itr = id_to_time_ranges_.find(calendar_id.value());
            if (itr != id_to_time_ranges_.end()) {
                for (const auto& cur_time_range: itr->second) {
                    if (cur_time_range->is_ms_in(ms)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

private:
    void handle_multi_weekday_calendar(const celonis::accelerator::MultiWeekdayCalendar& multi_weekday_calendar) {
        for (const auto& calendar: multi_weekday_calendar.calendars()) {
            handle_weekday_calendar(calendar);
        }
    }

    void handle_weekday_calendar(const celonis::accelerator::WeekdayCalendar& weekday_calendar) {
        std::optional<std::string> calendar_id = std::nullopt;
        if (weekday_calendar.has_calendar_id()) {
            calendar_id = weekday_calendar.calendar_id();
        }
        handle_weekday_calendar_entry(weekday_calendar.monday(), 0, calendar_id);
        handle_weekday_calendar_entry(weekday_calendar.tuesday(), 1, calendar_id);
        handle_weekday_calendar_entry(weekday_calendar.wednesday(), 2, calendar_id);
        handle_weekday_calendar_entry(weekday_calendar.thursday(), 3, calendar_id);
        handle_weekday_calendar_entry(weekday_calendar.friday(), 4, calendar_id);
        handle_weekday_calendar_entry(weekday_calendar.saturday(), 5, calendar_id);
        handle_weekday_calendar_entry(weekday_calendar.sunday(), 6, calendar_id);
    }

    void handle_weekday_calendar_entry(const celonis::accelerator::WeekdayCalendarEntry& entry, int index,
                                       std::optional<std::string> calendar_id) {
        int64_t begin = entry.shift().begin();
        int64_t end = entry.shift().end();
        if (entry.use_day() && begin < end) {
            int64_t period = NUM_DAYS_PER_WEEK * NUM_MILLISECONDS_PER_DAY;
            std::vector<int> add_days = {4, 5, 6, 0, 1, 2, 3};
            begin += NUM_MILLISECONDS_PER_DAY * add_days.at(index);
            end += NUM_MILLISECONDS_PER_DAY * add_days.at(index);
            if (calendar_id.has_value()) {
                id_to_time_ranges_[calendar_id.value()].push_back(
                        std::make_shared<PeriodicTimeRange>(begin, end, period));
            } else {
                time_ranges_.push_back(std::make_shared<PeriodicTimeRange>(begin, end, period));
            }
        }
    }

    void handle_factory_calendar(const celonis::accelerator::FactoryCalendar& factory_calendar) {
        std::vector<TimeRange> time_ranges;
        std::unordered_map<std::string, std::vector<TimeRange>> id_to_time_ranges;
        for (const auto& entry: factory_calendar.entries()) {
            if (!entry.has_start_date() || !entry.has_end_date() || entry.start_date() > entry.end_date()) {
                continue;
            }
            if (entry.has_calendar_id()) {
                id_to_time_ranges[entry.calendar_id()].emplace_back(entry.start_date(), entry.end_date());
            } else {
                time_ranges.emplace_back(entry.start_date(), entry.end_date());
            }
        }
        merge_time_ranges(time_ranges);
        for (auto& kv: id_to_time_ranges) {
            merge_time_ranges(kv.second);
        }
        for (const auto& time_range: time_ranges) {
            time_ranges_.push_back(std::make_shared<TimeRange>(time_range.begin_ms, time_range.end_ms));
        }
        for (const auto& kv: id_to_time_ranges) {
            auto& cur_time_ranges = id_to_time_ranges_[kv.first];
            for (const auto& time_range: kv.second) {
                cur_time_ranges.push_back(std::make_shared<TimeRange>(time_range.begin_ms, time_range.end_ms));
            }
        }
    }

    std::bitset<366> to_bitset(const celonis::accelerator::WorkdayCalendarEntry& entry) {
        std::bitset<366> bit_set;
        for (int i = 0; i < entry.is_workday_size(); ++i) {
            if (entry.is_workday(i)) {
                bit_set.set(i, true);
            }
        }
        return bit_set;
    }

    std::vector<TimeRange> to_time_ranges(int64_t year, const std::bitset<366>& bit_set) {
        const TimestampValue epoch = TimestampValue::create(1970, 1, 1, 0, 0, 0);
        const TimestampValue timestamp = TimestampValue::create(year, 1, 1, 0, 0, 0);
        const int64_t year_begin_ms = timestamp.diff_microsecond(epoch) / NUM_MICROSECONDS_PER_MILLISECONDS;
        std::vector<TimeRange> time_ranges;
        for (int i = 0; i < bit_set.size(); ++i) {
            if (bit_set.test(i)) {
                int64_t begin = year_begin_ms + NUM_MILLISECONDS_PER_DAY * i;
                time_ranges.emplace_back(begin, begin + NUM_MILLISECONDS_PER_DAY);
            }
        }
        return time_ranges;
    }

    void handle_workday_calendar(const celonis::accelerator::WorkdayCalendar& workday_calendar) {
        std::map<int, std::bitset<366>> year_to_bitset;
        std::unordered_map<std::string, std::map<int, std::bitset<366>>> year_to_bitset_by_id;
        for (const auto& entry: workday_calendar.entries()) {
            auto year = entry.year();
            if (entry.has_calendar_id()) {
                year_to_bitset_by_id[entry.calendar_id()][year] |= to_bitset(entry);
            } else {
                year_to_bitset[year] |= to_bitset(entry);
            }
        }
        std::vector<TimeRange> time_ranges;
        std::unordered_map<std::string, std::vector<TimeRange>> id_to_time_ranges;
        for (const auto& kv: year_to_bitset) {
            for (const auto& time_range: to_time_ranges(kv.first, kv.second)) {
                time_ranges.push_back(time_range);
            }
        }
        for (const auto& kv: year_to_bitset_by_id) {
            std::string id = kv.first;
            auto& cur_time_ranges = id_to_time_ranges[id];
            for (const auto& bitset_by_year: kv.second) {
                for (const auto& time_range: to_time_ranges(bitset_by_year.first, bitset_by_year.second)) {
                    cur_time_ranges.push_back(time_range);
                }
            }
        }
        merge_time_ranges(time_ranges);
        for (auto& kv: id_to_time_ranges) {
            merge_time_ranges(kv.second);
        }
        for (const auto& time_range: time_ranges) {
            time_ranges_.push_back(std::make_shared<TimeRange>(time_range.begin_ms, time_range.end_ms));
        }
        for (const auto& kv: id_to_time_ranges) {
            auto& cur_time_ranges = id_to_time_ranges_[kv.first];
            for (const auto& time_range: kv.second) {
                cur_time_ranges.push_back(std::make_shared<TimeRange>(time_range.begin_ms, time_range.end_ms));
            }
        }
    }

    std::vector<std::shared_ptr<TimeRange>> time_ranges_;
    std::unordered_map<std::string, std::vector<std::shared_ptr<TimeRange>>> id_to_time_ranges_;
};

struct CalendarState {
    Calendar calendar;
    bool is_null;
    bool is_empty;
};

static bool json_string_to_calendar(const std::string& calendar_json_string, celonis::accelerator::Calendar& calendar) {
    auto status = google::protobuf::util::JsonStringToMessage(calendar_json_string, &calendar);
    return status.ok();
}

static Status validate_weekday_calendar_entry(const celonis::accelerator::WeekdayCalendarEntry& entry) {
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

static Status validate_weekday_calendar(const celonis::accelerator::WeekdayCalendar& weekday_calendar) {
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.monday()));
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.tuesday()));
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.wednesday()));
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.thursday()));
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.friday()));
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.saturday()));
    RETURN_IF_ERROR(validate_weekday_calendar_entry(weekday_calendar.sunday()));
    return Status::OK();
}

static Status
validate_multi_weekday_calendar(const celonis::accelerator::MultiWeekdayCalendar& multi_weekday_calendar) {
    std::unordered_set<std::string> calendar_ids;
    int has_calendar_id = -1;
    for (const auto& weekday_calendar: multi_weekday_calendar.calendars()) {
        if (has_calendar_id == -1) {
            has_calendar_id = weekday_calendar.has_calendar_id();
        } else {
            if (has_calendar_id != weekday_calendar.has_calendar_id()) {
                return Status::InvalidArgument(
                        "In MultiWeekdayCalendar, ensure that the calendar_id is either set or not set in all calendars.");
            }
        }
        auto result = calendar_ids.insert(weekday_calendar.calendar_id());
        if (!result.second) {
            return Status::InvalidArgument(
                    "In MultiWeekdayCalendar, two calendars must not share the same calendar_id.");
        }
        RETURN_IF_ERROR(validate_weekday_calendar(weekday_calendar));
    }
    return Status::OK();
}

static Status validate_factory_calendar(const celonis::accelerator::FactoryCalendar& factory_calendar) {
    int has_calendar_id = -1; // not set
    for (const auto& entry: factory_calendar.entries()) {
        if (has_calendar_id == -1) {
            has_calendar_id = entry.has_calendar_id();
        } else {
            if (has_calendar_id != entry.has_calendar_id()) {
                return Status::InvalidArgument(
                        "In FactoryCalendar, ensure that the calendar_id is either set or not set in all entries.");
            }
        }
    }
    return Status::OK();
}

static int get_days_in_year(int64_t year) {
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 366 : 365;
}

static Status validate_workday_calendar(const celonis::accelerator::WorkdayCalendar& workday_calendar) {
    int has_calendar_id = -1; // not set
    for (const auto& entry: workday_calendar.entries()) {
        if (!entry.has_year()) {
            return Status::InvalidArgument("year is not set in a workday calendar entry.");
        }
        auto required_n_days = get_days_in_year(entry.year());
        if (required_n_days != entry.is_workday_size()) {
            return Status::InvalidArgument(
                    fmt::format("{} should have {} days, however the workday calendar contains {} is_workday.",
                                entry.year(), required_n_days, entry.is_workday_size()));
        }
        if (has_calendar_id == -1) {
            has_calendar_id = entry.has_calendar_id();
        } else {
            if (has_calendar_id != entry.has_calendar_id()) {
                return Status::InvalidArgument(
                        "In WorkdayCalendar, ensure that the calendar_id is either set or not set in all entries.");
            }
        }
    }
    return Status::OK();
}

static Status validate_calendar(const celonis::accelerator::Calendar& calendar,
                                const std::optional<std::string>& calendar_id = std::nullopt) {
    if (calendar.has_weekday_calendar()) {
        if (calendar_id.has_value()) {
            return Status::InvalidArgument("Calendar ID column should not be set for weekday calendar.");
        }
        if (calendar.weekday_calendar().has_calendar_id()) {
            return Status::InvalidArgument("calendar_id should not be set in WeekdayCalendar.");
        }
        RETURN_IF_ERROR(validate_weekday_calendar(calendar.weekday_calendar()));
    }
    if (calendar.has_multi_weekday_calendar()) {
        RETURN_IF_ERROR(validate_multi_weekday_calendar(calendar.multi_weekday_calendar()));
    }
    if (calendar.has_factory_calendar()) {
        RETURN_IF_ERROR(validate_factory_calendar(calendar.factory_calendar()));
    }
    if (calendar.has_workday_calendar()) {
        RETURN_IF_ERROR(validate_workday_calendar(calendar.workday_calendar()));
    }
    if (calendar.has_intersect_calendar()) {
        const celonis::accelerator::IntersectCalendar& intersect_calendar = calendar.intersect_calendar();
        if (!intersect_calendar.has_calendar1() && !intersect_calendar.has_calendar2()) {
            return Status::InvalidArgument("Neither calendar1 nor calendar2 is set in intersect_calendar.");
        }
        if (intersect_calendar.has_calendar1()) {
            RETURN_IF_ERROR(validate_calendar(intersect_calendar.calendar1(), calendar_id));
        }
        if (intersect_calendar.has_calendar2()) {
            RETURN_IF_ERROR(validate_calendar(intersect_calendar.calendar2(), calendar_id));
        }
    }
    if (calendar.has_intersect_calendar()) {
        return Status::InvalidArgument("Intersect calendar is not supported.");
    }
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
remap_timestamp_calendar(const TimestampValue& input_timestamp, const std::string& time_unit,
                         const std::string& calendar_json_string,
                         std::optional<std::string>& calendar_id, bool round_time = false) {
    TimestampValue timestamp = input_timestamp;
    truncate_timestamp(time_unit, timestamp);
    int64_t milliseconds = 0L;
    celonis::accelerator::Calendar calendar_proto;
    if (calendar_json_string.empty()) {
        if (calendar_id.has_value()) {
            return Status::InvalidArgument(
                    "Calendar ID column should not be set when calendar specification is not set.");
        }
        milliseconds = remap_timestamp_ms(timestamp);
    } else {
        // parse calendar_json_string
        if (!json_string_to_calendar(calendar_json_string, calendar_proto)) {
            return Status::InvalidArgument("Calendar specification column is malformed.");
        }
        RETURN_IF_ERROR(validate_calendar(calendar_proto, calendar_id));
        if (round_time) {
            int64_t multiplier = TIME_UNIT_TO_MS.at(time_unit);
            round_calendar(calendar_proto, multiplier);
        }
        Calendar calendar(calendar_proto);
        if (calendar.requires_calendar_id() && !calendar_id.has_value()) {
            return Status::InvalidArgument("Calendar ID column not provided.");
        }
        if (!calendar.requires_calendar_id()) {
            calendar_id = std::nullopt;
        }
        milliseconds = calendar.remap_timestamp_ms(timestamp, calendar_id);
    }
    return convert_time_unit(time_unit, milliseconds);
}

StatusOr<ColumnPtr> remap_timestamps_calendar_const([[maybe_unused]] FunctionContext* context,
                                                    const starrocks::Columns& columns,
                                                    const CalendarState* calendar_state) {
    DCHECK_EQ(columns.size(), 4);
    size_t n_rows = columns[0]->size();
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[1]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[3]);
    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    const Calendar& calendar = calendar_state->calendar;
    for (size_t row = 0; row < n_rows; ++row) {
        if (calendar_state->is_null || timestamp_viewer.is_null(row) || time_unit_viewer.is_null(row)) {
            result.append_null();
            continue;
        }
        std::string time_unit = time_unit_viewer.value(row).to_string();
        if (TIME_UNIT_TO_MS.find(time_unit) == TIME_UNIT_TO_MS.end()) {
            return Status::InvalidArgument("time unit must be one of DAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
        }
        auto timestamp = timestamp_viewer.value(row);
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        int64_t milliseconds;
        if (calendar_state->is_empty) {
            milliseconds = remap_timestamp_ms(timestamp);
        } else {
            if (calendar.requires_calendar_id() && !calendar_id.has_value()) {
                return Status::InvalidArgument("Calendar ID column not provided.");
            }
            if (!calendar.requires_calendar_id()) {
                calendar_id = std::nullopt;
            }
            milliseconds = calendar.remap_timestamp_ms(timestamp, calendar_id);
        }
        ASSIGN_OR_RETURN(const int64_t value, convert_time_unit(time_unit, milliseconds));
        result.append(value);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> CelonisTimeFunctions::timeunits_between_calendar([[maybe_unused]] FunctionContext* context,
                                                                     const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 5);
    size_t n_rows = columns[0]->size();
    ColumnViewer from_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer to_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[1]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[4]);
    UnnestedArrayData calendar_array_data = prepare_array_input(columns[3].get());
    if (calendar_array_data.null_elements != nullptr) {
        return Status::InvalidArgument("Calendar array should not have null elements.");
    }
    DCHECK(calendar_array_data.elements->is_binary());
    const auto& calendars = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *calendar_array_data.elements).get_data().data();
    const auto& calendar_offsets = calendar_array_data.offsets->get_data().data();

    ColumnBuilder<TYPE_DOUBLE> result(n_rows);
    for (size_t row = 0; row < n_rows; ++row) {
        if (from_timestamp_viewer.is_null(row) || to_timestamp_viewer.is_null(row) || time_unit_viewer.is_null(row) ||
            columns[3]->is_null(row)) {
            result.append_null();
            continue;
        }
        std::string time_unit = time_unit_viewer.value(row).to_string();
        if (TIME_UNIT_TO_MS.find(time_unit) == TIME_UNIT_TO_MS.end()) {
            return Status::InvalidArgument(
                    "time unit must be one of DAYS/WORKDAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
        }
        auto from_timestamp = from_timestamp_viewer.value(row);
        auto to_timestamp = to_timestamp_viewer.value(row);
        size_t start = calendar_offsets[row];
        size_t end = calendar_offsets[row + 1];
        std::string calendar_json_string;
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        for (size_t id = start; id < end; ++id) {
            calendar_json_string += calendars[id].to_string();
        }
        ASSIGN_OR_RETURN(const std::optional<int64_t> from_time,
                         remap_timestamp_calendar(from_timestamp, time_unit, calendar_json_string, calendar_id, true));
        ASSIGN_OR_RETURN(const std::optional<int64_t> to_time,
                         remap_timestamp_calendar(to_timestamp, time_unit, calendar_json_string, calendar_id, true));
        if (from_time.has_value() && to_time.has_value()) {
            double diff = to_time.value() - from_time.value();
            result.append(diff);
        } else {
            result.append_null();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> remap_timestamps_calendar_general([[maybe_unused]] FunctionContext* context,
                                                      const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 4);
    size_t n_rows = columns[0]->size();
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[1]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[3]);
    UnnestedArrayData calendar_array_data = prepare_array_input(columns[2].get());
    if (calendar_array_data.null_elements != nullptr) {
        return Status::InvalidArgument("Calendar array should not have null elements.");
    }
    DCHECK(calendar_array_data.elements->is_binary());
    const auto& calendars = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *calendar_array_data.elements).get_data().data();
    const auto& calendar_offsets = calendar_array_data.offsets->get_data().data();

    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    for (size_t row = 0; row < n_rows; ++row) {
        if (timestamp_viewer.is_null(row) || time_unit_viewer.is_null(row) || columns[2]->is_null(row)) {
            result.append_null();
            continue;
        }
        std::string time_unit = time_unit_viewer.value(row).to_string();
        if (TIME_UNIT_TO_MS.find(time_unit) == TIME_UNIT_TO_MS.end()) {
            return Status::InvalidArgument("time unit must be one of DAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
        }
        auto timestamp = timestamp_viewer.value(row);
        size_t start = calendar_offsets[row];
        size_t end = calendar_offsets[row + 1];
        std::string calendar_json_string;
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        for (size_t id = start; id < end; ++id) {
            calendar_json_string += calendars[id].to_string();
        }
        ASSIGN_OR_RETURN(const std::optional<int64_t> time,
                         remap_timestamp_calendar(timestamp, time_unit, calendar_json_string, calendar_id));
        if (time.has_value()) {
            result.append(time.value());
        } else {
            result.append_null();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> CelonisTimeFunctions::remap_timestamps_calendar([[maybe_unused]] FunctionContext* context,
                                                                    const starrocks::Columns& columns) {
    if (context == nullptr) {
        return remap_timestamps_calendar_general(context, columns);
    }
    auto* calendar_state = reinterpret_cast<CalendarState*>(context->get_function_state(
            FunctionContext::FRAGMENT_LOCAL));
    if (calendar_state == nullptr) {
        return remap_timestamps_calendar_general(context, columns);
    }
    return remap_timestamps_calendar_const(context, columns, calendar_state);
}

static StatusOr<std::optional<bool>>
timestamp_in_calendar(const TimestampValue& timestamp,
                      const std::string& calendar_json_string,
                      const std::optional<std::string>& calendar_id) {
    celonis::accelerator::Calendar calendar_proto;
    if (calendar_json_string.empty()) {
        return std::nullopt;
    } else {
        // parse calendar_json_string
        if (!json_string_to_calendar(calendar_json_string, calendar_proto)) {
            return Status::InvalidArgument("Calendar specification column is malformed.");
        }
        RETURN_IF_ERROR(validate_calendar(calendar_proto, calendar_id));
        Calendar calendar(calendar_proto);
        return calendar.is_timestamp_in_calendar(timestamp, calendar_id);
    }
}

StatusOr<ColumnPtr> in_calendar_general([[maybe_unused]] FunctionContext* context, const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 3);
    size_t n_rows = columns[0]->size();
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);

    UnnestedArrayData calendar_array_data = prepare_array_input(columns[1].get());
    if (calendar_array_data.null_elements != nullptr) {
        return Status::InvalidArgument("Calendar array can not contain null values.");
    }
    DCHECK(calendar_array_data.elements->is_binary());
    const auto& calendars = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *calendar_array_data.elements).get_data().data();
    const auto& calendar_offsets = calendar_array_data.offsets->get_data().data();

    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    for (size_t row = 0; row < n_rows; ++row) {
        if (timestamp_viewer.is_null(row) || columns[1]->is_null(row)) {
            result.append_null();
            continue;
        }
        auto timestamp = timestamp_viewer.value(row);
        size_t start = calendar_offsets[row];
        size_t end = calendar_offsets[row + 1];
        std::string calendar_json_string;
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        for (size_t id = start; id < end; ++id) {
            calendar_json_string += calendars[id].to_string();
        }
        ASSIGN_OR_RETURN(std::optional<bool> is_in,
                         timestamp_in_calendar(timestamp, calendar_json_string, calendar_id));
        if (is_in.has_value()) {
            result.append(is_in.value() ? 1L : 0L);
        } else {
            result.append_null();
        }

    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> in_calendar_const([[maybe_unused]] FunctionContext* context, const starrocks::Columns& columns,
                                      const CalendarState* calendar_state) {
    DCHECK_EQ(columns.size(), 3);
    size_t n_rows = columns[0]->size();
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);

    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    const Calendar& calendar = calendar_state->calendar;
    for (size_t row = 0; row < n_rows; ++row) {
        if (calendar_state->is_null || calendar_state->is_empty || timestamp_viewer.is_null(row)) {
            result.append_null();
            continue;
        }
        auto timestamp = timestamp_viewer.value(row);
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        const bool is_in = calendar.is_timestamp_in_calendar(timestamp, calendar_id);
        result.append(is_in ? 1L : 0L);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> CelonisTimeFunctions::in_calendar([[maybe_unused]] FunctionContext* context,
                                                      const starrocks::Columns& columns) {
    if (context == nullptr) {
        return in_calendar_general(context, columns);
    }
    auto* calendar_state = reinterpret_cast<CalendarState*>(context->get_function_state(
            FunctionContext::FRAGMENT_LOCAL));
    if (calendar_state == nullptr) {
        return in_calendar_general(context, columns);
    }
    return in_calendar_const(context, columns, calendar_state);
}

Status CelonisTimeFunctions::in_calendar_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    // context->is_constant_column(index) must not be used to determine if the argument is Array Literal because as of
    // 2024-01-31 it returns false for Array Literal while get_constant_column(index) returns non nullptr.
    if (scope != FunctionContext::FRAGMENT_LOCAL || context->get_num_args() != 3 ||
        context->get_arg_type(1)->type != TYPE_ARRAY || context->get_constant_column(1) == nullptr) {
        return Status::OK();
    }
    const auto calendar_column = context->get_constant_column(1);
    if (calendar_column->size() == 0) {
        return Status::OK();
    }

    if (calendar_column->is_null(0)) {
        auto* calendar_state = new CalendarState{Calendar(), true, false};
        context->set_function_state(scope, calendar_state);
        return Status::OK();
    }

    std::string calendar_json_string;
    auto array = calendar_column->get(0).get_array();
    for (const auto& element: array) {
        if (element.is_null()) {
            return Status::InvalidArgument("[prepare] Calendar array can not contain null values.");
        } else {
            calendar_json_string += element.get_slice().to_string();
        }
    }
    if (calendar_json_string.empty()) {
        auto* calendar_state = new CalendarState{Calendar(), false, true};
        context->set_function_state(scope, calendar_state);
        return Status::OK();
    }
    celonis::accelerator::Calendar calendar_proto;
    // parse calendar_json_string
    if (!json_string_to_calendar(calendar_json_string, calendar_proto)) {
        return Status::InvalidArgument("[prepare] Calendar specification column is malformed.");
    }
    RETURN_IF_ERROR(validate_calendar(calendar_proto));
    auto* calendar_state = new CalendarState{Calendar(calendar_proto), false, false};
    context->set_function_state(scope, calendar_state);
    return Status::OK();
}

Status CelonisTimeFunctions::in_calendar_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        auto* calendar_state = reinterpret_cast<CalendarState*>(context->get_function_state(
                FunctionContext::FRAGMENT_LOCAL));
        if (calendar_state != nullptr) {
            delete calendar_state;
        }
    }
    return Status::OK();
}

Status CelonisTimeFunctions::remap_timestamps_calendar_prepare(FunctionContext* context,
                                                               FunctionContext::FunctionStateScope scope) {
    // context->is_constant_column(index) must not be used to determine if the argument is Array Literal because as of
    // 2024-01-31 it returns false for Array Literal while get_constant_column(index) returns non nullptr.
    if (scope != FunctionContext::FRAGMENT_LOCAL || context->get_num_args() != 4 ||
        context->get_arg_type(2)->type != TYPE_ARRAY || context->get_constant_column(2) == nullptr) {
        return Status::OK();
    }
    const auto calendar_column = context->get_constant_column(2);
    if (calendar_column->size() == 0) {
        return Status::OK();
    }

    if (calendar_column->is_null(0)) {
        auto* calendar_state = new CalendarState{Calendar(), true, false};
        context->set_function_state(scope, calendar_state);
        return Status::OK();
    }

    std::string calendar_json_string;
    auto array = calendar_column->get(0).get_array();
    for (const auto& element: array) {
        if (element.is_null()) {
            return Status::InvalidArgument("[prepare] Calendar array can not contain null values.");
        } else {
            calendar_json_string += element.get_slice().to_string();
        }
    }
    if (calendar_json_string.empty()) {
        auto* calendar_state = new CalendarState{Calendar(), false, true};
        context->set_function_state(scope, calendar_state);
        return Status::OK();
    }
    celonis::accelerator::Calendar calendar_proto;
    // parse calendar_json_string
    if (!json_string_to_calendar(calendar_json_string, calendar_proto)) {
        return Status::InvalidArgument("[prepare] Calendar specification column is malformed.");
    }
    RETURN_IF_ERROR(validate_calendar(calendar_proto));
    auto* calendar_state = new CalendarState{Calendar(calendar_proto), false, false};
    context->set_function_state(scope, calendar_state);
    return Status::OK();
}

Status CelonisTimeFunctions::remap_timestamps_calendar_close(FunctionContext* context,
                                                             FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        auto* calendar_state = reinterpret_cast<CalendarState*>(context->get_function_state(
                FunctionContext::FRAGMENT_LOCAL));
        if (calendar_state != nullptr) {
            delete calendar_state;
        }
    }
    return Status::OK();
}

static StatusOr<celonis::accelerator::Calendar>
get_calendar(const Slice* const calendars, const unsigned int* const offsets, int row) {
    size_t start = offsets[row];
    size_t end = offsets[row + 1];
    std::string calendar_json_string;
    for (size_t i = start; i < end; ++i) {
        calendar_json_string += calendars[i].to_string();
    }
    celonis::accelerator::Calendar calendar_proto;
    if (!json_string_to_calendar(calendar_json_string, calendar_proto)) {
        return Status::InvalidArgument("Calendar json string is malformed.");
    }
    RETURN_IF_ERROR(validate_calendar(calendar_proto));
    return calendar_proto;
}

StatusOr<ColumnPtr> CelonisTimeFunctions::make_intersect_calendar(starrocks::FunctionContext* context,
                                                                  const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    size_t n_rows = columns[0]->size();

    UnnestedArrayData calendar1_array_data = prepare_array_input(columns[0].get());
    if (calendar1_array_data.null_elements != nullptr) {
        return Status::InvalidArgument("calendar1 array must not contain null values.");
    }
    DCHECK(calendar1_array_data.elements->is_binary());
    const auto& calendars1 = down_cast<const RunTimeColumnType <TYPE_VARCHAR>&>(
            *calendar1_array_data.elements).get_data().data();
    const auto& calendar1_offsets = calendar1_array_data.offsets->get_data().data();

    UnnestedArrayData calendar2_array_data = prepare_array_input(columns[1].get());
    if (calendar2_array_data.null_elements != nullptr) {
        return Status::InvalidArgument("calendar2 array must not contain null values.");
    }
    DCHECK(calendar2_array_data.elements->is_binary());
    const auto& calendars2 = down_cast<const RunTimeColumnType <TYPE_VARCHAR>&>(
            *calendar2_array_data.elements).get_data().data();
    const auto& calendar2_offsets = calendar2_array_data.offsets->get_data().data();

    ColumnPtr output_column = columns[0]->clone_empty();
    output_column = NullableColumn::wrap_if_necessary(output_column);
    for (size_t row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row)) {
            output_column->append_nulls(1);
            continue;
        }
        StatusOr <celonis::accelerator::Calendar> status_or_calendar1 = get_calendar(calendars1, calendar1_offsets,
                                                                                     row);
        StatusOr <celonis::accelerator::Calendar> status_or_calendar2 = get_calendar(calendars2, calendar2_offsets,
                                                                                     row);
        if (!status_or_calendar1.ok() || !status_or_calendar2.ok()) {
            output_column->append_nulls(1);
            continue;
        }
        celonis::accelerator::Calendar calendar_proto;
        calendar_proto.mutable_intersect_calendar()->mutable_calendar1()->CopyFrom(status_or_calendar1.value());
        calendar_proto.mutable_intersect_calendar()->mutable_calendar2()->CopyFrom(status_or_calendar2.value());
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

        output_column->append_datum(array);
    }
    return output_column;
}

static StatusOr<TimestampValue>
add_timeunits(const TimestampValue& timestamp, const std::string& time_unit, int64_t add_value,
              const std::string& calendar_json_string,
              std::optional<std::string>& calendar_id) {
    if (calendar_json_string.empty()) {
        if (calendar_id.has_value()) {
            return Status::InvalidArgument(
                    "Calendar ID column should not be set when calendar specification is not set.");
        }
        if (time_unit == "DAYS" || time_unit == "WORKDAYS") {
            return timestamp.add<TimeUnit::DAY>(add_value);
        } else if (time_unit == "HOURS") {
            return timestamp.add<TimeUnit::HOUR>(add_value);
        } else if (time_unit == "MINUTES") {
            return timestamp.add<TimeUnit::MINUTE>(add_value);
        } else if (time_unit == "SECONDS") {
            return timestamp.add<TimeUnit::SECOND>(add_value);
        } else {
            return timestamp.add<TimeUnit::MILLISECOND>(add_value);
        }
    } else {
        return Status::InvalidArgument("add_timeunits_calendar with Calendar is not supported yet.");
    }
}

StatusOr<ColumnPtr> CelonisTimeFunctions::add_timeunits_calendar([[maybe_unused]] FunctionContext* context,
                                                                 const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 5);
    size_t n_rows = columns[0]->size();
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer add_value_viewer = ColumnViewer<TYPE_BIGINT>(columns[1]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[4]);
    UnnestedArrayData calendar_array_data = prepare_array_input(columns[3].get());
    if (calendar_array_data.null_elements != nullptr) {
        return Status::InvalidArgument("Calendar array should not have null elements.");
    }
    DCHECK(calendar_array_data.elements->is_binary());
    const auto& calendars = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *calendar_array_data.elements).get_data().data();
    const auto& calendar_offsets = calendar_array_data.offsets->get_data().data();

    ColumnBuilder<TYPE_DATETIME> result(n_rows);
    for (size_t row = 0; row < n_rows; ++row) {
        if (timestamp_viewer.is_null(row) || add_value_viewer.is_null(row) || time_unit_viewer.is_null(row) ||
            columns[3]->is_null(row)) {
            result.append_null();
            continue;
        }
        std::string time_unit = time_unit_viewer.value(row).to_string();
        if (TIME_UNIT_TO_MS.find(time_unit) == TIME_UNIT_TO_MS.end()) {
            return Status::InvalidArgument("time unit must be one of DAYS/WORKDAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
        }
        auto timestamp = timestamp_viewer.value(row);
        auto add_value = add_value_viewer.value(row);
        size_t start = calendar_offsets[row];
        size_t end = calendar_offsets[row + 1];
        std::string calendar_json_string;
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        for (size_t id = start; id < end; ++id) {
            calendar_json_string += calendars[id].to_string();
        }
        ASSIGN_OR_RETURN(const std::optional<TimestampValue> new_timestamp,
                         add_timeunits(timestamp, time_unit, add_value, calendar_json_string, calendar_id));
        if (new_timestamp.has_value()) {
            result.append(new_timestamp.value());
        } else {
            result.append_null();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

} // namespace starrocks
