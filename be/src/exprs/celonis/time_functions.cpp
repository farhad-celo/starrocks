#include "exprs/celonis/time_functions.h"

#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/array_column.h"
#include "column/hash_set.h"
#include "exprs/celonis/base64.h"
#include "exprs/builtin_functions.h"
#include "exprs/celonis/util.h"
#include "exprs/celonis/agg/util.h"
#include "google/protobuf/util/json_util.h"
#include "modules/query/calendars.pb.h"
#include "types/date_value.h"

#include <bitset>
#include <stack>

namespace starrocks {

namespace {

enum class CalendarFunction {
    GET_CALENDAR_ENTRY_START = 0,
    REMAP_TIMESTAMPS_CALENDAR = 1,
    TIMEUNITS_BETWEEN_CALENDAR = 2,
    IN_CALENDAR = 3,
    ADD_TIMEUNITS_CALENDAR = 4
};

static const phmap::flat_hash_map<std::string, int64_t, StdHash<std::string>> TIME_UNIT_TO_MS = {
        {"DAYS",         86400000L},
        {"WORKDAYS",     86400000L},
        {"HOURS",        3600000L},
        {"MINUTES",      60000L},
        {"SECONDS",      1000L},
        {"MILLISECONDS", 1L}
};

static const int64_t NUM_DAYS_PER_WEEK = 7L;

static const int64_t NUM_MILLISECONDS_PER_DAY = 86400000L;

static const int64_t NUM_MILLISECONDS_PER_WEEK = NUM_MILLISECONDS_PER_DAY * NUM_DAYS_PER_WEEK;

static const int64_t NUM_MICROSECONDS_PER_MILLISECONDS = 1000L;

// SR places an upper limit of 1M of STRING. We use 900K which is less than 1M.
static const size_t MAX_STRING_SIZE = 900000;

static const int MONTH_TO_QUARTER[13] = {0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4};

static const TimestampValue EPOCH = TimestampValue::create(1970, 1, 1, 0, 0, 0);

static const TimestampValue MAX_VALID_TIMESTAMP = TimestampValue::create(10000, 1, 1, 0, 0, 0);

static const TimestampValue MIN_VALID_TIMESTAMP = TimestampValue::create(1400, 1, 1, 0, 0, 0);

// Valid datetime is in [MIN_VALID_MILLIS, MAX_VALID_MILLIS).
static int64_t MAX_VALID_MILLIS = MAX_VALID_TIMESTAMP.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;

static int64_t MIN_VALID_MILLIS = MIN_VALID_TIMESTAMP.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;

bool is_timestamp_in_valid_range(const TimestampValue& timestamp) {
    return MIN_VALID_TIMESTAMP <= timestamp && timestamp < MAX_VALID_TIMESTAMP;
}

bool is_timestamp_in_valid_range(int64_t unix_millis) {
    return MIN_VALID_MILLIS <= unix_millis && unix_millis < MAX_VALID_MILLIS;
}

TimestampValue timestamp_from_unix_millis(int64_t unix_millis) {
    int64_t seconds = unix_millis / 1000;
    int64_t microseconds = (unix_millis % 1000) * 1000;
    TimestampValue timestamp;
    timestamp.from_unix_second(seconds, microseconds);
    return timestamp;
}

static int64_t remap_timestamp_ms(const TimestampValue& timestamp) {
    return timestamp.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;
}

static TimestampValue
add_timeunits_helper(const TimestampValue& timestamp, const std::string& time_unit, int64_t add_value) {
    auto start_millis = remap_timestamp_ms(timestamp);
    int64_t factor = 1;
    if (time_unit == "DAYS" || time_unit == "WORKDAYS") {
        factor = 86400000L;
    } else if (time_unit == "HOURS") {
        factor = 3600000L;
    } else if (time_unit == "MINUTES") {
        factor = 60000L;
    } else if (time_unit == "SECONDS") {
        factor = 1000L;
    }
    auto end_millis = start_millis + add_value * factor;
    return timestamp_from_unix_millis(end_millis);
}

static int get_year(const TimestampValue& value) {
    int year, month, day, hour, minute, second, usec;
    value.to_timestamp(&year, &month, &day, &hour, &minute, &second, &usec);
    return year;
}

static int get_year(int64_t millis) {
    TimestampValue t = timestamp_from_unix_millis(millis);
    return get_year(t);
}

static int get_week_number(int year, int month, int day) {
    return DateValue::create(year, month, day).get_week_of_year();
}

static int64_t floor_to_nearest_multiple(int64_t num, int64_t multiple) {
    DCHECK(multiple > 0);
    int64_t floored_num = (num / multiple) * multiple;
    if (num < 0 && num % multiple != 0) {
        floored_num -= multiple;
    }
    return floored_num;
}

static int64_t ceil_to_nearest_multiple(int64_t num, int64_t multiple) {
    DCHECK(multiple > 0);
    int64_t ceiled_num = (num / multiple) * multiple;
    if (num > 0 && num % multiple != 0) {
        ceiled_num += multiple;
    }
    return ceiled_num;
}

static int64_t millis_between(const TimestampValue& from_timestamp, const TimestampValue& to_timestamp) {
    return remap_timestamp_ms(to_timestamp) - remap_timestamp_ms(from_timestamp);
}

static void sort_time_ranges(std::vector<TimeRange>& time_ranges) {
    std::sort(time_ranges.begin(), time_ranges.end(),
              [](const TimeRange& a, const TimeRange& b) { return a.begin_ms < b.begin_ms; });
}

static void merge_non_weekly_time_ranges(std::vector<TimeRange>& time_ranges) {
    sort_time_ranges(time_ranges);
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

    Calendar(const celonis::accelerator::Calendar& calendar_proto, CalendarFunction func) {
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
        if (calendar_proto.has_intersect_calendar()) {
            handle_intersect_calendar(calendar_proto.intersect_calendar());
        }
        // sort the time ranges of each calendar_id
        process_time_ranges();
        set_cum_sum();
        set_no_weekly();
        // TODO(y.zhang): Add more optimization for other calendar functions.
        if (func != CalendarFunction::REMAP_TIMESTAMPS_CALENDAR) {
            set_scope();
            set_round_time_ranges();
            set_round_cum_sum();
        }
    }

    bool requires_calendar_id() const {
        auto iter = id_to_time_ranges_.find(std::nullopt);
        if (iter == id_to_time_ranges_.end()) {
            return !id_to_time_ranges_.empty();
        }
        return id_to_time_ranges_.size() > 1;
    }

    std::optional<int64_t>
    remap_timestamp_ms(const TimestampValue& timestamp, const std::optional<std::string>& calendar_id) const {
        int64 ms = timestamp.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;
        if (!is_timestamp_in_valid_range(ms)) {
            return std::nullopt;
        }
        int64_t left_ms = ms < 0 ? ms : 0L;
        int64_t right_ms = ms < 0 ? 0L : ms;
        std::optional<int64_t> overlap = compute_overlap(left_ms, right_ms, calendar_id);
        if (!overlap.has_value()) {
            return std::nullopt;
        }
        return ms < 0 ? -overlap.value() : overlap.value();
    }

    std::optional<int64_t>
    millis_between(const TimestampValue& from_timestamp, const TimestampValue& to_timestamp,
                   const std::optional<std::string>& calendar_id, bool round_to_day = false) const {
        if (is_out_scope(from_timestamp, calendar_id) || is_out_scope(to_timestamp, calendar_id)) {
            return std::nullopt;
        }
        int64 from_ms = from_timestamp.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;
        int64 to_ms = to_timestamp.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;
        int64_t left_ms = to_ms >= from_ms ? from_ms : to_ms;
        int64_t right_ms = to_ms >= from_ms ? to_ms : from_ms;
        std::optional<int64_t> overlap = compute_overlap(left_ms, right_ms, calendar_id, round_to_day);
        if (!overlap.has_value()) {
            return 0;
        }
        return (to_ms >= from_ms) ? overlap.value() : -overlap.value();
    }

    struct CompareTimeRange {
        bool left_to_right;

        CompareTimeRange(const bool& left_to_right = false) : left_to_right(left_to_right) {}

        bool operator()(const TimeRange& lhs, const TimeRange& rhs) const {
            if (left_to_right) {
                return lhs.begin_ms > rhs.begin_ms;
            }
            return lhs.end_ms < rhs.end_ms;
        }
    };

    std::optional<TimestampValue>
    add_timeunits(const TimestampValue& timestamp, const std::string& time_unit, int64_t add_value,
                  const std::optional<std::string>& calendar_id) const {
        if (is_out_scope(timestamp, calendar_id)) {
            return std::nullopt;
        }
        int64_t ms = timestamp.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;
        const bool is_workdays = (time_unit == "WORKDAYS");
        auto time_unit_iter = TIME_UNIT_TO_MS.find(time_unit);
        DCHECK(time_unit_iter != TIME_UNIT_TO_MS.end());
        const auto add_ms = add_value * time_unit_iter->second;
        auto time_range_iter = id_to_time_ranges_.find(calendar_id);
        if (time_range_iter == id_to_time_ranges_.end() || time_range_iter->second.empty()) {
            return (add_ms == 0) ? std::optional<TimestampValue>(timestamp) : std::nullopt;
        }
        auto no_weekly_it = id_to_no_weekly_.find(calendar_id);
        DCHECK(no_weekly_it != id_to_no_weekly_.end());
        const std::vector<TimeRange>& time_ranges = is_workdays ? id_to_round_time_ranges_.find(calendar_id)->second
                                                                : time_range_iter->second;
        if (no_weekly_it->second) {
            return quick_add_ms(timestamp, ms, add_ms, time_ranges, calendar_id, is_workdays);
        }
        const bool left_to_right = add_value >= 0;
        std::priority_queue<TimeRange, std::vector<TimeRange>, CompareTimeRange> pq(CompareTimeRange{left_to_right});
        for (const auto& time_range: time_ranges) {
            DCHECK(time_range.is_weekly);
            int64_t a = time_range.begin_ms;
            int64_t b = time_range.end_ms;
            // move [a, b) to the left of ms
            if (left_to_right && a - ms >= NUM_MILLISECONDS_PER_WEEK) {
                const int64_t n_periods = (a - ms + NUM_MILLISECONDS_PER_WEEK - 1) / NUM_MILLISECONDS_PER_WEEK;
                a -= n_periods * NUM_MILLISECONDS_PER_WEEK;
                b -= n_periods * NUM_MILLISECONDS_PER_WEEK;
            }
            // move [a, b) to the right of ms
            if (!left_to_right && ms - b >= NUM_MILLISECONDS_PER_WEEK) {
                const int64_t n_periods = (ms - b + NUM_MILLISECONDS_PER_WEEK - 1) / NUM_MILLISECONDS_PER_WEEK;
                a += n_periods * NUM_MILLISECONDS_PER_WEEK;
                b += n_periods * NUM_MILLISECONDS_PER_WEEK;
            }
            pq.emplace(a, b, true);
        }
        int64_t ms_left = std::abs(add_ms);
        // [begin_ms, end_ms)
        int64_t begin_ms = left_to_right ? ms : MIN_VALID_MILLIS;
        int64_t end_ms = left_to_right ? MAX_VALID_MILLIS : ms;
        std::optional<int64_t> rv = std::nullopt;
        int64_t period = left_to_right ? NUM_MILLISECONDS_PER_WEEK : -NUM_MILLISECONDS_PER_WEEK;
        while (!pq.empty()) {
            auto top = pq.top();
            pq.pop();
            int64_t left = std::max(top.begin_ms, begin_ms);
            int64_t right = std::min(top.end_ms, end_ms);
            if (right > left) {
                // note that [left, right) is half inclusive
                if ((left_to_right && ms_left >= right - left) || (!left_to_right && ms_left > right - left)) {
                    ms_left -= right - left;
                } else {
                    // ms_left < (right - left)
                    if (left_to_right) {
                        rv = left + ms_left;
                    } else {
                        rv = right - ms_left;
                    }
                    ms_left = 0;
                    break;
                }
            }
            int64_t new_begin_ms = top.begin_ms + period;
            int64_t new_end_ms = top.end_ms + period;
            if (!((left_to_right && new_begin_ms >= MAX_VALID_MILLIS) ||
                  (!left_to_right && new_end_ms < MIN_VALID_MILLIS))) {
                pq.emplace(new_begin_ms, new_end_ms, true);
            }
        }
        if (rv.has_value()) {
            auto res_timestamp = timestamp_from_unix_millis(rv.value());
            if (is_out_scope(res_timestamp, calendar_id)) {
                return std::nullopt;
            }
            // For WORKDAYS, keep the time (hour, minute, second) of the day unchanged.
            if (is_workdays) {
                set_time_from(timestamp, res_timestamp);
            }
            return res_timestamp;
        }
        return std::nullopt;
    }

    std::optional<bool>
    is_timestamp_in(const TimestampValue& timestamp,
                    const std::optional<std::string>& calendar_id) const {
        if (is_out_scope(timestamp, calendar_id)) {
            return std::nullopt;
        }
        const int64_t ms = timestamp.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;
        if (!is_timestamp_in_valid_range(ms)) {
            return std::nullopt;
        }
        auto time_ranges_it = id_to_time_ranges_.find(calendar_id);
        if (time_ranges_it != id_to_time_ranges_.end()) {
            auto no_weekly_it = id_to_no_weekly_.find(calendar_id);
            DCHECK(no_weekly_it != id_to_no_weekly_.end());
            if (no_weekly_it->second) {
                return quick_is_timestamp_in(ms, time_ranges_it->second);
            }
            for (const auto& cur_time_range: time_ranges_it->second) {
                if (cur_time_range.is_ms_in(ms)) {
                    return true;
                }
            }
        }
        return false;
    }

    std::optional<int64_t> get_entry_start(int index, const std::optional<std::string>& calendar_id) const {
        auto itr = id_to_time_ranges_.find(calendar_id);
        if (itr != id_to_time_ranges_.end()) {
            const auto& time_ranges = itr->second;
            if (index < 0 || index >= time_ranges.size()) {
                return std::nullopt;
            }
            return time_ranges[index].begin_ms;
        }
        return std::nullopt;
    }

private:
    using IdToTimeRangesMap = phmap::flat_hash_map<std::optional<std::string>, std::vector<TimeRange>, StdHash<std::optional<std::string>>>;
    using IdToWeekdayMap = phmap::flat_hash_map<std::optional<std::string>, phmap::flat_hash_map<int, celonis::accelerator::WeekdayCalendarEntry, StdHash<int>>, StdHash<std::optional<std::string>>>;

    void set_time_from(const TimestampValue& ref_timestamp, TimestampValue& timestamp) const {
        int new_year, new_month, new_day, new_hour, new_minute, new_second, new_usec;
        timestamp.to_timestamp(&new_year, &new_month, &new_day, &new_hour, &new_minute, &new_second, &new_usec);
        int old_year, old_month, old_day, old_hour, old_minute, old_second, old_usec;
        ref_timestamp.to_timestamp(&old_year, &old_month, &old_day, &old_hour, &old_minute, &old_second, &old_usec);
        timestamp.from_timestamp(new_year, new_month, new_day, old_hour, old_minute, old_second, old_usec);
    }

    void process_time_ranges() {
        for (auto& kv: id_to_time_ranges_) {
            auto& time_ranges = kv.second;
            sort_time_ranges(time_ranges);
        }
    }

    void set_cum_sum() {
        id_to_cum_sum_.reserve(id_to_time_ranges_.size());
        for (const auto& kv: id_to_time_ranges_) {
            const auto& time_ranges = kv.second;
            std::vector<int64_t> cum_sum = {0};
            cum_sum.reserve(time_ranges.size() + 1);
            for (const auto& time_range: time_ranges) {
                cum_sum.push_back(cum_sum.back() + time_range.end_ms - time_range.begin_ms);
            }
            id_to_cum_sum_.emplace(kv.first, std::move(cum_sum));
        }
    }

    void set_round_time_ranges() {
        id_to_round_time_ranges_.reserve(id_to_time_ranges_.size());
        for (const auto& kv: id_to_time_ranges_) {
            std::vector<TimeRange> round_time_ranges;
            round_time_ranges.reserve(kv.second.size());
            for (const auto& time_range: kv.second) {
                auto round_time_range = time_range;
                round_time_range.begin_ms = floor_to_nearest_multiple(round_time_range.begin_ms,
                                                                      NUM_MILLISECONDS_PER_DAY);
                round_time_range.end_ms = ceil_to_nearest_multiple(round_time_range.end_ms, NUM_MILLISECONDS_PER_DAY);
                round_time_ranges.push_back(round_time_range);
            }
            id_to_round_time_ranges_.emplace(kv.first, std::move(round_time_ranges));
        }
    }

    void set_round_cum_sum() {
        id_to_round_cum_sum_.reserve(id_to_round_time_ranges_.size());
        for (const auto& kv: id_to_round_time_ranges_) {
            const auto& round_time_ranges = kv.second;
            std::vector<int64_t> round_cum_sum = {0};
            round_cum_sum.reserve(round_time_ranges.size() + 1);
            for (const auto& round_time_range: round_time_ranges) {
                round_cum_sum.push_back(round_cum_sum.back() + round_time_range.end_ms - round_time_range.begin_ms);
            }
            id_to_round_cum_sum_.emplace(kv.first, std::move(round_cum_sum));
        }
    }

    void set_no_weekly() {
        id_to_no_weekly_.reserve(id_to_time_ranges_.size());
        for (const auto& kv: id_to_time_ranges_) {
            id_to_no_weekly_.insert({kv.first, std::all_of(kv.second.begin(), kv.second.end(),
                                                           [](const TimeRange& time_range) { return !time_range.is_weekly; })});
        }
    }

    void set_scope() {
        for (const auto& [id, time_ranges]: id_to_time_ranges_) {
            if (time_ranges.empty()) {
                id_to_scope_[id] = std::nullopt;
                continue;
            }
            int64 min_begin = time_ranges[0].begin_ms;
            int64 max_end = time_ranges[0].end_ms;
            bool has_weekly_range = false;
            for (const auto& time_range: time_ranges) {
                if (time_range.is_weekly) {
                    has_weekly_range = true;
                    break;
                }
                min_begin = std::min(min_begin, time_range.begin_ms);
                max_end = std::max(max_end, time_range.end_ms);
            }
            if (has_weekly_range) {
                id_to_scope_[id] = std::nullopt;
            } else {
                id_to_scope_[id] = Scope{get_year(min_begin), get_year(max_end)};
            }
        }
    }

    bool is_out_scope(const TimestampValue& timestamp, const std::optional<std::string>& calendar_id) const {
        auto iter = id_to_scope_.find(calendar_id);
        if (iter != id_to_scope_.end()) {
            const auto& scope = iter->second;
            if (scope.has_value()) {
                const int year = get_year(timestamp);
                if (year < scope->min_year || year > scope->max_year) {
                    return true;
                }
            }
        }
        return false;
    }

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
            std::vector<int> add_days = {4, 5, 6, 0, 1, 2, 3};
            begin += NUM_MILLISECONDS_PER_DAY * add_days.at(index);
            end += NUM_MILLISECONDS_PER_DAY * add_days.at(index);
            if (calendar_id.has_value()) {
                id_to_time_ranges_[calendar_id.value()].emplace_back(begin, end, true);
            } else {
                id_to_time_ranges_[std::nullopt].emplace_back(begin, end, true);
            }
        }
    }

    IdToTimeRangesMap to_time_ranges(const celonis::accelerator::FactoryCalendar& factory_calendar) {
        // Group entry indices by calendar_id
        phmap::flat_hash_map<std::optional<std::string>, std::vector<size_t>> calendar_to_indexes;
        for (size_t i = 0; i < factory_calendar.entries_size(); ++i) {
            const auto& entry = factory_calendar.entries(i);
            if (entry.has_start_date() && entry.has_end_date() && entry.start_date() <= entry.end_date()) {
                const auto key = entry.has_calendar_id() ?
                                 std::make_optional(entry.calendar_id()) : std::nullopt;
                calendar_to_indexes[key].push_back(i);
            }
        }

        // Build result by processing grouped entries
        IdToTimeRangesMap id_to_time_ranges;
        id_to_time_ranges.reserve(calendar_to_indexes.size());

        for (auto& [calendar_id, indexes] : calendar_to_indexes) {
            auto& time_ranges = id_to_time_ranges[calendar_id];
            time_ranges.reserve(indexes.size());

            // Process all entries for this calendar_id together
            for (size_t idx : indexes) {
                const auto& entry = factory_calendar.entries(idx);
                time_ranges.emplace_back(entry.start_date(), entry.end_date(), false);
            }

            // Merge time ranges if needed
            if (time_ranges.size() > 1) {
                merge_non_weekly_time_ranges(time_ranges);
            }
        }

        return id_to_time_ranges;
    }

    void populate_id_to_time_ranges(const IdToTimeRangesMap& id_to_time_ranges) {
        id_to_time_ranges_ = id_to_time_ranges;
    }

    void handle_factory_calendar(const celonis::accelerator::FactoryCalendar& factory_calendar) {
        populate_id_to_time_ranges(to_time_ranges(factory_calendar));
    }

    // m2 does not contain any weekly TimeRanges
    IdToTimeRangesMap intersect_id_to_time_ranges(const IdToTimeRangesMap& m1, const IdToTimeRangesMap& m2) {
        IdToTimeRangesMap m;
        std::vector<std::optional<std::string>> m2_keys;
        for (const auto& p: m2) {
            m2_keys.push_back(p.first);
        }
        for (const auto& [id1, time_ranges_1]: m1) {
            std::vector<std::optional<std::string>> ids = {id1, std::nullopt}; // assuming id1 is not nullopt
            if (!id1.has_value()) {
                ids = m2_keys;
            }
            for (const auto& id: ids) {
                auto iter = m2.find(id);
                if (iter == m2.end()) {
                    continue;
                }
                std::vector<TimeRange> new_time_ranges;
                const auto& time_ranges_2 = iter->second;
                for (const auto& time_range_1: time_ranges_1) {
                    for (const auto& time_range_2: time_ranges_2) {
                        DCHECK(!time_range_2.is_weekly);
                        int64_t left_ms = time_range_2.begin_ms;
                        int64_t right_ms = time_range_2.end_ms;
                        const auto inter_time_ranges = time_range_1.intersect(left_ms, right_ms);
                        new_time_ranges.insert(new_time_ranges.end(), inter_time_ranges.begin(),
                                               inter_time_ranges.end());
                    }
                }
                m[id1.has_value() ? id1 : id] = new_time_ranges;
            }
        }
        return m;
    }

    IdToWeekdayMap intersect_id_to_weekday(const IdToWeekdayMap& m1, const IdToWeekdayMap& m2) {
        IdToWeekdayMap m;
        std::vector<std::optional<std::string>> m2_keys;
        for (const auto& p: m2) {
            m2_keys.push_back(p.first);
        }
        for (const auto& [id1, index_to_weekday_1]: m1) {
            std::vector<std::optional<std::string>> ids = {id1, std::nullopt}; // assuming id1 is not nullopt
            if (!id1.has_value()) {
                ids = m2_keys;
            }
            for (const auto& id: ids) {
                auto iter = m2.find(id);
                if (iter == m2.end()) {
                    continue;
                }
                const auto& index_to_weekday_2 = iter->second;
                // both m1 and m2 have id
                phmap::flat_hash_map<int, celonis::accelerator::WeekdayCalendarEntry, StdHash<int>> new_index_to_weekday;
                for (const auto& [index, weekday_1]: index_to_weekday_1) {
                    auto it = index_to_weekday_2.find(index);
                    if (it == index_to_weekday_2.end()) {
                        continue;
                    }
                    const auto& weekday_2 = it->second;
                    // intersect weekday_1 and weekday_2
                    if (!weekday_1.use_day() || !weekday_2.use_day()) {
                        continue;
                    }
                    celonis::accelerator::WeekdayCalendarEntry weekday;
                    int64_t s1 = weekday_1.shift().begin();
                    int64_t e1 = weekday_1.shift().end();
                    int64_t s2 = weekday_2.shift().begin();
                    int64_t e2 = weekday_2.shift().end();
                    int64_t s = std::max(s1, s2);
                    int64_t e = std::min(e1, e2);
                    if (e > s) {
                        weekday.set_use_day(true);
                        weekday.mutable_shift()->set_begin(s);
                        weekday.mutable_shift()->set_end(e);
                        new_index_to_weekday[index] = weekday;
                    }
                }
                if (!new_index_to_weekday.empty()) {
                    m[id1.has_value() ? id1 : id] = new_index_to_weekday;
                }
            }
        }
        return m;
    }

    void handle_weekday_calendar(const celonis::accelerator::WeekdayCalendar& weekday_calendar,
                                 IdToWeekdayMap& id_to_weekday) {
        std::optional<std::string> id = std::nullopt;
        if (weekday_calendar.has_calendar_id()) {
            id = weekday_calendar.calendar_id();
        }
        if (weekday_calendar.has_monday() && weekday_calendar.monday().use_day()) {
            id_to_weekday[id][0] = weekday_calendar.monday();
        }
        if (weekday_calendar.has_tuesday() && weekday_calendar.tuesday().use_day()) {
            id_to_weekday[id][1] = weekday_calendar.tuesday();
        }
        if (weekday_calendar.has_wednesday() && weekday_calendar.wednesday().use_day()) {
            id_to_weekday[id][2] = weekday_calendar.wednesday();
        }
        if (weekday_calendar.has_thursday() && weekday_calendar.thursday().use_day()) {
            id_to_weekday[id][3] = weekday_calendar.thursday();
        }
        if (weekday_calendar.has_friday() && weekday_calendar.friday().use_day()) {
            id_to_weekday[id][4] = weekday_calendar.friday();
        }
        if (weekday_calendar.has_saturday() && weekday_calendar.saturday().use_day()) {
            id_to_weekday[id][5] = weekday_calendar.saturday();
        }
        if (weekday_calendar.has_sunday() && weekday_calendar.sunday().use_day()) {
            id_to_weekday[id][6] = weekday_calendar.sunday();
        }
    }

    IdToTimeRangesMap to_time_ranges(const IdToWeekdayMap& id_to_weekday) {
        IdToTimeRangesMap id_to_time_ranges;
        std::vector<int> add_days = {4, 5, 6, 0, 1, 2, 3};
        for (const auto& [id, index_to_weekday]: id_to_weekday) {
            for (const auto& [index, weekday]: index_to_weekday) {
                int64_t begin = weekday.shift().begin();
                int64_t end = weekday.shift().end();
                if (weekday.use_day() && end > begin) {
                    begin += NUM_MILLISECONDS_PER_DAY * add_days.at(index);
                    end += NUM_MILLISECONDS_PER_DAY * add_days.at(index);
                    id_to_time_ranges[id].emplace_back(begin, end, true);
                }
            }
        }
        return id_to_time_ranges;
    }

    void handle_intersect_calendar(const celonis::accelerator::IntersectCalendar& intersect_calendar) {
        // collect all the calendars
        std::stack<celonis::accelerator::Calendar> calendars;
        if (intersect_calendar.has_calendar1()) {
            calendars.push(intersect_calendar.calendar1());
        }
        if (intersect_calendar.has_calendar2()) {
            calendars.push(intersect_calendar.calendar2());
        }
        std::optional<IdToTimeRangesMap> id_to_time_ranges = std::nullopt;
        std::optional<IdToWeekdayMap> id_to_weekday;
        while (!calendars.empty()) {
            const auto calendar = calendars.top();
            calendars.pop();
            std::optional<IdToTimeRangesMap> cur_id_to_time_ranges;
            std::optional<IdToWeekdayMap> cur_id_to_weekday;
            if (calendar.has_factory_calendar()) {
                cur_id_to_time_ranges = to_time_ranges(calendar.factory_calendar());
            } else if (calendar.has_workday_calendar()) {
                cur_id_to_time_ranges = to_time_ranges(calendar.workday_calendar());
            } else if (calendar.has_weekday_calendar()) {
                cur_id_to_weekday = IdToWeekdayMap();
                handle_weekday_calendar(calendar.weekday_calendar(), cur_id_to_weekday.value());
            } else if (calendar.has_multi_weekday_calendar()) {
                cur_id_to_weekday = IdToWeekdayMap();
                for (const auto& weekday_calendar: calendar.multi_weekday_calendar().calendars()) {
                    handle_weekday_calendar(weekday_calendar, cur_id_to_weekday.value());
                }
            } else if (calendar.has_intersect_calendar()) {
                if (calendar.intersect_calendar().has_calendar1()) {
                    calendars.push(calendar.intersect_calendar().calendar1());
                }
                if (calendar.intersect_calendar().has_calendar2()) {
                    calendars.push(calendar.intersect_calendar().calendar2());
                }
            }
            if (cur_id_to_time_ranges.has_value()) {
                if (id_to_time_ranges.has_value()) {
                    id_to_time_ranges = intersect_id_to_time_ranges(id_to_time_ranges.value(),
                                                                    cur_id_to_time_ranges.value());
                } else {
                    id_to_time_ranges = cur_id_to_time_ranges.value();
                }
            }
            if (cur_id_to_weekday) {
                if (id_to_weekday.has_value()) {
                    id_to_weekday = intersect_id_to_weekday(id_to_weekday.value(), cur_id_to_weekday.value());
                } else {
                    id_to_weekday = cur_id_to_weekday.value();
                }
            }
        }
        if (id_to_weekday.has_value() && id_to_time_ranges.has_value()) {
            populate_id_to_time_ranges(
                    intersect_id_to_time_ranges(to_time_ranges(id_to_weekday.value()), id_to_time_ranges.value()));
        } else if (id_to_weekday.has_value()) {
            populate_id_to_time_ranges(to_time_ranges(id_to_weekday.value()));
        } else if (id_to_time_ranges.has_value()) {
            populate_id_to_time_ranges(id_to_time_ranges.value());
        }
    }

    std::bitset<366> to_bitset(const celonis::accelerator::WorkdayCalendarEntry& entry) {
        std::bitset<366> bit_set;
        if (entry.has_workday_mask()) {
            const std::string& mask = entry.workday_mask();
            size_t mask_len_bytes = mask.length();
            for (int i = 0; i < 366; ++i) {
                int byte_index = i / 8;
                int bit_index = i % 8;
                if (static_cast<size_t>(byte_index) < mask_len_bytes) {
                    const unsigned char byte_value = static_cast<unsigned char>(mask[byte_index]);
                    const unsigned char bit_mask_value = (1 << bit_index);
                    if ((byte_value & bit_mask_value) != 0) {
                        bit_set.set(i, true);
                    }
                }
            }
        } else {
            for (int i = 0; i < entry.is_workday_size() && i < 366; ++i) {
                if (entry.is_workday(i)) {
                    bit_set.set(i, true);
                }
            }
        }
        return bit_set;
    }

    std::vector<TimeRange> to_time_ranges(int64_t year, const std::bitset<366>& bit_set) {
        const TimestampValue timestamp = TimestampValue::create(year, 1, 1, 0, 0, 0);
        const int64_t year_begin_ms = timestamp.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;
        std::vector<TimeRange> time_ranges;
        for (int i = 0; i < bit_set.size(); ++i) {
            if (bit_set.test(i)) {
                int64_t begin = year_begin_ms + NUM_MILLISECONDS_PER_DAY * i;
                time_ranges.emplace_back(begin, begin + NUM_MILLISECONDS_PER_DAY, false);
            }
        }
        return time_ranges;
    }

    IdToTimeRangesMap to_time_ranges(const celonis::accelerator::WorkdayCalendar& workday_calendar) {
        phmap::flat_hash_map<std::optional<std::string>, std::map<int, std::bitset<366>>, StdHash<std::optional<std::string>>> year_to_bitset_by_id;
        for (const auto& entry: workday_calendar.entries()) {
            auto year = entry.year();
            if (entry.has_calendar_id()) {
                year_to_bitset_by_id[entry.calendar_id()][year] |= to_bitset(entry);
            } else {
                year_to_bitset_by_id[std::nullopt][year] |= to_bitset(entry);
            }
        }
        IdToTimeRangesMap id_to_time_ranges;
        for (const auto& [id, bitset_by_year]: year_to_bitset_by_id) {
            auto& cur_time_ranges = id_to_time_ranges[id];
            for (const auto& [year, bit_set]: bitset_by_year) {
                for (const auto& time_range: to_time_ranges(year, bit_set)) {
                    cur_time_ranges.push_back(time_range);
                }
            }
        }
        for (auto& kv: id_to_time_ranges) {
            merge_non_weekly_time_ranges(kv.second);
        }
        return id_to_time_ranges;
    }

    void handle_workday_calendar(const celonis::accelerator::WorkdayCalendar& workday_calendar) {
        populate_id_to_time_ranges(to_time_ranges(workday_calendar));
    }

    // finds the most right time range index that its end_ms <= ms.
    int find_most_right_index(int64_t ms, const std::vector<TimeRange>& time_ranges) const {
        DCHECK(!time_ranges.empty());
        if (time_ranges.front().end_ms > ms) {
            return -1;
        }
        // find the last time_range whose end_ms <= ms
        int lo = 0;
        // time_ranges is not empty
        int hi = time_ranges.size() - 1;
        while (lo < hi) {
            int mid = hi - (hi - lo) / 2;
            if (time_ranges[mid].end_ms > ms) {
                hi = mid - 1;
            } else {
                lo = mid;
            }
        }
        return lo;
    }

    // computes the duration of [min_begin_ms, ms] in the time_ranges.
    int64_t
    compute_duration(int64_t ms, const std::vector<TimeRange>& time_ranges, const std::vector<int64_t>& cum_sum) const {
        if (time_ranges.empty() || ms <= time_ranges.front().begin_ms) {
            return 0L;
        }
        int64_t rv = 0L;
        int index = find_most_right_index(ms, time_ranges);
        rv += cum_sum[index + 1];
        // need to check next time_range if exists
        if (index + 1 < time_ranges.size()) {
            if (ms > time_ranges[index + 1].begin_ms) {
                rv += std::min(ms, time_ranges[index + 1].end_ms) - time_ranges[index + 1].begin_ms;
            }
        }
        return rv;
    }

    // This function assumes time_ranges is sorted and contains only non-weekly time_ranges.
    int64_t quick_compute_overlap(int64_t left_ms, int64_t right_ms, const std::vector<TimeRange>& time_ranges,
                                  const std::vector<int64_t>& cum_sum) const {
        if (time_ranges.empty() || left_ms >= time_ranges.back().end_ms || right_ms <= time_ranges.front().begin_ms) {
            return 0L;
        }
        return compute_duration(right_ms, time_ranges, cum_sum) - compute_duration(left_ms, time_ranges, cum_sum);
    }

    std::optional<int64_t>
    compute_overlap(int64_t left_ms, int64_t right_ms, const std::optional<std::string>& calendar_id,
                    bool round_to_day = false) const {
        auto time_ranges_it = id_to_time_ranges_.find(calendar_id);
        if (time_ranges_it != id_to_time_ranges_.end()) {
            const auto& time_ranges = round_to_day ? id_to_round_time_ranges_.find(calendar_id)->second
                                                   : time_ranges_it->second;
            auto no_weekly_it = id_to_no_weekly_.find(calendar_id);
            DCHECK(no_weekly_it != id_to_no_weekly_.end());
            if (no_weekly_it->second) {
                auto cum_sum_it = round_to_day ? id_to_round_cum_sum_.find(calendar_id) : id_to_cum_sum_.find(
                        calendar_id);
                return quick_compute_overlap(left_ms, right_ms, time_ranges, cum_sum_it->second);
            } else {
                int64_t rv = 0L;
                for (const auto& cur_time_range: time_ranges) {
                    rv += cur_time_range.compute_overlap(left_ms, right_ms);
                }
                return rv;
            }
        }
        if (calendar_id.has_value()) {
            // The requested calendar_id does not exist.
            return std::nullopt;
        }
        return 0L;
    }

    bool quick_is_timestamp_in(int64_t ms, const std::vector<TimeRange>& time_ranges) const {
        if (time_ranges.empty()) {
            return false;
        }
        int index = find_most_right_index(ms, time_ranges);
        // we only need to check (index + 1)-th time_range if it exists
        if (index + 1 < time_ranges.size()) {
            return time_ranges.at(index + 1).is_ms_in(ms);
        }
        return false;
    }

    std::optional<TimestampValue>
    quick_add_ms(const TimestampValue& timestamp, int64_t ms, int64_t add_ms, const std::vector<TimeRange>& time_ranges,
                 const std::optional<std::string>& calendar_id, bool is_workdays) const {
        if (time_ranges.empty()) {
            return std::nullopt;
        }
        auto cum_sum_iter = is_workdays ? id_to_round_cum_sum_.find(calendar_id) : id_to_cum_sum_.find(calendar_id);
        DCHECK((is_workdays && cum_sum_iter != id_to_round_cum_sum_.end()) ||
               (!is_workdays && cum_sum_iter != id_to_cum_sum_.end()));
        const auto& cum_sum = cum_sum_iter->second;
        if (add_ms >= 0) {
            ms = std::max(ms, time_ranges.front().begin_ms);
        } else {
            ms = std::min(ms, time_ranges.back().end_ms);
        }
        const int64_t start = compute_duration(ms, time_ranges, cum_sum);
        const int64_t target = start + add_ms;
        if (target < 0 || target >= cum_sum.back()) {
            return std::nullopt;
        }
        // Compute the time (i.e., target_ms) which corresponds to target.
        // Find the first value in cum_sum which is greater than target.
        auto it = std::upper_bound(cum_sum.begin(), cum_sum.end(), target);
        // target is at index-th time_range
        auto index = std::distance(cum_sum.begin(), it) - 1;
        DCHECK(index >= 0 && index < time_ranges.size());
        const auto begin_ms = time_ranges.at(index).begin_ms;
        const auto target_ms = begin_ms + target - cum_sum.at(index);
        if (!is_timestamp_in_valid_range(target_ms)) {
            return std::nullopt;
        }
        auto res_timestamp = timestamp_from_unix_millis(target_ms);
        if (is_out_scope(res_timestamp, calendar_id)) {
            return std::nullopt;
        }
        if (is_workdays) {
            set_time_from(timestamp, res_timestamp);
        }
        return res_timestamp;
    }

    struct Scope {
        int min_year;
        int max_year;
    };

    IdToTimeRangesMap id_to_time_ranges_;
    // suppose the time_ranges is [(-4, -2), (1, 2), (3, 6), (7, 11)], the cum_sum is [0, 2, 3, 6, 10].
    phmap::flat_hash_map<std::optional<std::string>, std::vector<int64_t>, StdHash<std::optional<std::string>>> id_to_cum_sum_;
    IdToTimeRangesMap id_to_round_time_ranges_;
    phmap::flat_hash_map<std::optional<std::string>, std::vector<int64_t>, StdHash<std::optional<std::string>>> id_to_round_cum_sum_;
    // no_weekly is set to true if all the time range in time_ranges is not weekly.
    phmap::flat_hash_map<std::optional<std::string>, bool, StdHash<std::optional<std::string>>> id_to_no_weekly_;
    phmap::flat_hash_map<std::optional<std::string>, std::optional<Scope>, StdHash<std::optional<std::string>>> id_to_scope_;
};

StatusOr<std::string> get_calendar_string(const ColumnPtr& calendar_column, int row) {
    std::string calendar_str;
    auto array = calendar_column->get(row).get_array();
    size_t size = 0;
    for (const auto& element: array) {
        if (element.is_null()) {
            return Status::InvalidArgument("Calendar array can not contain null values.");
        } else {
            size += element.get_slice().size;
        }
    }
    calendar_str.reserve(size);
    for (const auto& element: array) {
        calendar_str.append(element.get_slice().data, element.get_slice().size);
    }
    return calendar_str;
}

static bool
base64_encoded_string_to_calendar(const std::string& calendar_string, celonis::accelerator::Calendar& calendar) {
    std::unique_ptr<char[]> decoded_buffer(new char[calendar_string.length()]);

    int decoded_len = base64_decode3(calendar_string.data(), calendar_string.length(), decoded_buffer.get());
    // Check if the decoding was successful before attempting to parse.
    if (decoded_len < 0) {
        return false;
    }
    bool success = calendar.ParseFromArray(decoded_buffer.get(), decoded_len);
    return success;
}

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
    HashSet<std::string> calendar_ids;
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

static Status
validate_workday_calendar(const celonis::accelerator::WorkdayCalendar& workday_calendar, bool reject_year_gap = false) {
    int has_calendar_id = -1; // not set
    phmap::flat_hash_map<std::optional<std::string>, std::vector<int64_t>, StdHash<std::optional<std::string>>> id_to_years;
    for (const auto& entry: workday_calendar.entries()) {
        if (!entry.has_year()) {
            return Status::InvalidArgument("year is not set in a workday calendar entry.");
        }
        auto required_n_days = get_days_in_year(entry.year());
        if (!entry.has_workday_mask() && required_n_days != entry.is_workday_size()) {
            return Status::InvalidArgument(
                    fmt::format("{} should have {} days, however the workday calendar contains {} is_workday.",
                                entry.year(), required_n_days, entry.is_workday_size()));
        }
        if (entry.has_workday_mask() && entry.workday_mask().length() != 46) {
            return Status::InvalidArgument(
                    fmt::format("The length of workday_mask of workday calendar should be 46 however it is {}",
                                entry.workday_mask().length()));
        }
        if (has_calendar_id == -1) {
            has_calendar_id = entry.has_calendar_id();
        } else {
            if (has_calendar_id != entry.has_calendar_id()) {
                return Status::InvalidArgument(
                        "In WorkdayCalendar, ensure that the calendar_id is either set or not set in all entries.");
            }
        }
        std::optional<std::string> id = std::nullopt;
        if (entry.has_calendar_id()) {
            id = entry.calendar_id();
        }
        id_to_years[id].push_back(entry.year());
    }
    if (reject_year_gap) {
        for (auto& [id, years]: id_to_years) {
            std::sort(years.begin(), years.end());
            for (size_t i = 1; i < years.size(); ++i) {
                if (years[i] - years[i - 1] > 1) {
                    return Status::InvalidArgument("Year gaps are found in the workday calendar configuration.");
                }
            }
        }
    }
    return Status::OK();
}

static bool string_to_calendar(const std::string& calendar_string, celonis::accelerator::Calendar& calendar) {
    if (calendar_string.find('{') != std::string::npos) {
        return json_string_to_calendar(calendar_string, calendar);
    } else {
        return base64_encoded_string_to_calendar(calendar_string, calendar);
    }
}

static Status validate_calendar(const celonis::accelerator::Calendar& calendar,
                                bool reject_year_gap_in_workday_calendar = false) {
    if (calendar.has_weekday_calendar()) {
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
        RETURN_IF_ERROR(validate_workday_calendar(calendar.workday_calendar(), reject_year_gap_in_workday_calendar));
    }
    if (calendar.has_intersect_calendar()) {
        const celonis::accelerator::IntersectCalendar& intersect_calendar = calendar.intersect_calendar();
        if (!intersect_calendar.has_calendar1() || !intersect_calendar.has_calendar2()) {
            return Status::InvalidArgument("Intersect calendar must set both calendar1 and calendar2.");
        }
        if (intersect_calendar.has_calendar1()) {
            RETURN_IF_ERROR(validate_calendar(intersect_calendar.calendar1(), reject_year_gap_in_workday_calendar));
        }
        if (intersect_calendar.has_calendar2()) {
            RETURN_IF_ERROR(validate_calendar(intersect_calendar.calendar2(), reject_year_gap_in_workday_calendar));
        }
    }
    return Status::OK();
}

StatusOr<celonis::accelerator::Calendar>
validate_and_to_proto(const std::string& calendar_string, bool in_prepare = false,
                      bool reject_year_gap_in_workday_calendar = false) {
    celonis::accelerator::Calendar calendar_proto;
    // parse calendar_string
    if (!string_to_calendar(calendar_string, calendar_proto)) {
        const std::string msg =
                (in_prepare ? "[prepare] " : "") + std::string("Calendar specification column is malformed.");
        return Status::InvalidArgument(msg.c_str());
    }
    RETURN_IF_ERROR(validate_calendar(calendar_proto, reject_year_gap_in_workday_calendar));
    return calendar_proto;
}

Status validate_time_unit(const std::string& time_unit) {
    if (TIME_UNIT_TO_MS.find(time_unit) == TIME_UNIT_TO_MS.end()) {
        return Status::InvalidArgument("time unit must be one of WORKDAYS/DAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
    }
    return Status::OK();
}

struct CalendarState {
    static StatusOr<CalendarState> create_calendar_state(const ColumnPtr& calendar_column, int row, bool in_prepare,
                                                         bool reject_year_gap_in_workday_calendar,
                                                         const std::optional<std::string>& time_unit,
                                                         CalendarFunction func) {
        if (time_unit.has_value()) {
            RETURN_IF_ERROR(validate_time_unit(time_unit.value()));
        }
        if (calendar_column->is_null(row)) {
            return CalendarState{Calendar(), true, false, time_unit};
        }
        ASSIGN_OR_RETURN(const std::string calendar_string, get_calendar_string(calendar_column, row));
        if (calendar_string.empty()) {
            return CalendarState{Calendar(), false, true, time_unit};
        }
        ASSIGN_OR_RETURN(const celonis::accelerator::Calendar calendar_proto,
                         validate_and_to_proto(calendar_string, in_prepare, reject_year_gap_in_workday_calendar));
        return CalendarState{Calendar(calendar_proto, func), false, false, time_unit};
    }

    Calendar calendar;
    bool is_null;
    bool is_empty;
    std::optional<std::string> time_unit;
};

static StatusOr<int64_t> convert_time_unit(const std::string& time_unit, int64_t milliseconds) {
    auto iter = TIME_UNIT_TO_MS.find(time_unit);
    if (iter != TIME_UNIT_TO_MS.end()) {
        return milliseconds / iter->second;
    } else {
        return Status::InvalidArgument("Unknown time_unit: " + time_unit);
    }
}

static StatusOr<double> convert_time_unit_float(const std::string& time_unit, int64_t milliseconds) {
    auto iter = TIME_UNIT_TO_MS.find(time_unit);
    if (iter != TIME_UNIT_TO_MS.end()) {
        return static_cast<double>(milliseconds) / iter->second;
    } else {
        return Status::InvalidArgument("Unknown time_unit: " + time_unit);
    }
}

static StatusOr<std::optional<double>>
timeunits_between(const TimestampValue& from_timestamp_raw, const TimestampValue& to_timestamp_raw,
                  const std::string& time_unit,
                  const CalendarState& calendar_state,
                  std::optional<std::string>& calendar_id) {
    if (!is_timestamp_in_valid_range(from_timestamp_raw) || !is_timestamp_in_valid_range(to_timestamp_raw)) {
        return std::nullopt;
    }
    TimestampValue from_timestamp = from_timestamp_raw;
    TimestampValue to_timestamp = to_timestamp_raw;
    const bool round_to_day = time_unit == "WORKDAYS";
    if (round_to_day) {
        from_timestamp.trunc_to_day();
        to_timestamp.trunc_to_day();
    }
    std::optional<int64_t> milliseconds;
    if (calendar_state.is_empty) {
        if (calendar_id.has_value()) {
            return Status::InvalidArgument(
                    "Calendar ID column should not be set when calendar specification is not set.");
        }
        milliseconds = millis_between(from_timestamp, to_timestamp);
    } else {
        if (calendar_state.calendar.requires_calendar_id() && !calendar_id.has_value()) {
            milliseconds = std::nullopt;
        } else {
            if (!calendar_state.calendar.requires_calendar_id()) {
                // If calendar does not require calendar_id, ignore calendar_id by setting it to std::nullopt.
                // This is consistent with Saola's behavior.
                calendar_id = std::nullopt;
            }
            milliseconds = calendar_state.calendar.millis_between(from_timestamp, to_timestamp, calendar_id, round_to_day);
        }
    }
    if (milliseconds.has_value()) {
        return convert_time_unit_float(time_unit, milliseconds.value());
    }
    return std::nullopt;
}

static StatusOr<std::optional<int64_t>>
remap_timestamp_calendar(const TimestampValue& input_timestamp, const std::string& time_unit,
                         const CalendarState& calendar_state,
                         std::optional<std::string>& calendar_id) {
    TimestampValue timestamp = input_timestamp;
    int64_t milliseconds = 0L;
    if (calendar_state.is_empty) {
        if (calendar_id.has_value()) {
            return Status::InvalidArgument(
                    "Calendar ID column should not be set when calendar specification is not set.");
        }
        milliseconds = remap_timestamp_ms(timestamp);
    } else {
        if (calendar_state.calendar.requires_calendar_id() && !calendar_id.has_value()) {
            return std::nullopt;
        }
        if (!calendar_state.calendar.requires_calendar_id()) {
            calendar_id = std::nullopt;
        }
        std::optional<int64_t> rv = calendar_state.calendar.remap_timestamp_ms(timestamp, calendar_id);
        if (!rv.has_value()) {
            return std::nullopt;
        }
        milliseconds = rv.value();
    }
    return convert_time_unit(time_unit, milliseconds);
}

Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope, int num_cols, int calendar_index,
               std::optional<int> time_unit_index, CalendarFunction func) {
    // context->is_constant_column(index) must not be used to determine if the argument is Array Literal because as of
    // 2024-02-26 it returns false for Array Literal while get_constant_column(index) returns non nullptr.
    if (scope != FunctionContext::FRAGMENT_LOCAL || context->get_num_args() != num_cols ||
        context->get_arg_type(calendar_index)->type != TYPE_ARRAY ||
        context->get_constant_column(calendar_index) == nullptr ||
        (time_unit_index.has_value() && context->get_constant_column(time_unit_index.value()) == nullptr)) {
        return Status::OK();
    }
    const auto calendar_column = context->get_constant_column(calendar_index);
    if (calendar_column->size() == 0) {
        return Status::OK();
    }
    std::optional<std::string> time_unit;
    if (time_unit_index.has_value()) {
        const auto time_unit_column = context->get_constant_column(time_unit_index.value());
        if (time_unit_column->size() == 0 || time_unit_column->is_null(0)) {
            return Status::OK();
        }
        time_unit = time_unit_column->get(0).get_slice().to_string();
    }
    ASSIGN_OR_RETURN(CalendarState calendar_state,
                     CalendarState::create_calendar_state(calendar_column, 0, true, false, time_unit, func));
    auto* calendar_state_ptr = new CalendarState(std::move(calendar_state));
    context->set_function_state(scope, calendar_state_ptr);
    return Status::OK();
}

Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        auto* calendar_state = reinterpret_cast<CalendarState*>(context->get_function_state(
                FunctionContext::FRAGMENT_LOCAL));
        if (calendar_state != nullptr) {
            delete calendar_state;
        }
    }
    return Status::OK();
}

StatusOr<ColumnPtr> func(FunctionContext* context, const starrocks::Columns& columns,
                         StatusOr<ColumnPtr> (* func_const)(FunctionContext*, const starrocks::Columns&,
                                                            const CalendarState*),
                         StatusOr<ColumnPtr> (* func_general)(FunctionContext*, const starrocks::Columns&)) {
    auto* calendar_state = reinterpret_cast<CalendarState*>(context->get_function_state(
            FunctionContext::FRAGMENT_LOCAL));
    if (calendar_state == nullptr) {
        return func_general(context, columns);
    }
    return func_const(context, columns, calendar_state);
}

} // namespace

std::vector<TimeRange> TimeRange::intersect(int64_t left_ms, int64_t right_ms) const {
    std::vector<TimeRange> rv;
    if (!is_weekly) {
        int64_t start = std::max(begin_ms, left_ms);
        int64_t end = std::min(end_ms, right_ms);
        if (end > start) {
            rv.emplace_back(start, end, false);
        }
        return rv;
    }
    const int64_t begin = left_ms;
    const int64_t end = right_ms;
    int64_t cur_begin = begin_ms;
    int64_t cur_end = end_ms;
    int64_t period = NUM_MILLISECONDS_PER_WEEK;
    if (cur_end <= begin) {
        int64_t n_periods = (begin - cur_end + period - 1) / period;
        cur_begin += n_periods * period;
        cur_end += n_periods * period;
    }
    // move [cur_begin, cur_end) to the left of [begin, end)
    if (cur_end > begin) {
        int64_t n_periods = (cur_end - begin + period - 1) / period;
        cur_begin -= n_periods * period;
        cur_end -= n_periods * period;
    }
    // move [cur_begin, cur_end) to right to pass [begin, end)
    while (true) {
        if (cur_begin >= end) {
            break;
        }
        int64_t left = std::max(cur_begin, begin);
        int64_t right = std::min(cur_end, end);
        if (right > left) {
            rv.emplace_back(left, right, false);
        }
        cur_begin += period;
        cur_end += period;
    }
    return rv;
}

int64_t TimeRange::compute_overlap(int64_t left_ms, int64_t right_ms) const {
    if (left_ms >= right_ms || this->begin_ms >= this->end_ms) {
        return 0;
    }

    constexpr auto do_compute_overlap = [](int64_t begin_ms, int64_t end_ms, int64_t left_ms, int64_t right_ms) {
        int64_t start = std::max(begin_ms, left_ms);
        int64_t end = std::min(end_ms, right_ms);
        return (end > start) ? end - start : 0;
    };

    if (!is_weekly) {
        return do_compute_overlap(this->begin_ms, this->end_ms, left_ms, right_ms);
    }
    const int64_t period = NUM_MILLISECONDS_PER_WEEK;
    const int64_t begin_normalized = this->begin_ms % period;
    int64_t end_normalized = this->end_ms % period;
    // note that it is impossible that begin_ms < period < end_ms.
    if (end_normalized == 0) {
        end_normalized = period;
    }
    int64_t left_normalized = left_ms % period;
    int64_t left_week = left_ms / period;
    if (left_normalized < 0) {
        left_normalized += period;
        left_week -= 1;
    }
    int64_t right_normalized = right_ms % period;
    int64_t right_week = right_ms / period;
    if (right_normalized < 0) {
        right_normalized += period;
        right_week -= 1;
    }

    DCHECK_LE(left_week, right_week);

    if (left_week == right_week) {
        // If [left_ms, right_ms) is within the same week, just compute the overlap once
        return do_compute_overlap(begin_normalized, end_normalized, left_normalized, right_normalized);
    }

    const int64_t n_weeks_between = right_week - left_week - 1;
    DCHECK_GE(n_weeks_between, 0);
    const int64_t weeks_between_overlap = n_weeks_between * (this->end_ms - this->begin_ms);
    const int64_t first_week_overlap = do_compute_overlap(begin_normalized, end_normalized, left_normalized, period);
    const int64_t last_week_overlap = do_compute_overlap(begin_normalized, end_normalized, 0, right_normalized);
    return weeks_between_overlap + first_week_overlap + last_week_overlap;
}

bool TimeRange::is_ms_in(int64_t ms) const {
    if (!is_weekly) {
        return ms >= begin_ms && ms < end_ms;
    }
    int64_t diff_mod = (ms - begin_ms) % NUM_MILLISECONDS_PER_WEEK;
    if (diff_mod < 0) {
        diff_mod += NUM_MILLISECONDS_PER_WEEK;
    }
    int64_t adjusted_ms = begin_ms + diff_mod;
    return begin_ms <= adjusted_ms && adjusted_ms < end_ms;
}

StatusOr<ColumnPtr>
CelonisTimeFunctions::millis_timestamp([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    for (auto row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        result.append(remap_timestamp_ms(timestamp_viewer.value(row)));
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr>
CelonisTimeFunctions::timestamp_millis([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    ColumnViewer<TYPE_BIGINT> data_column(columns[0]);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_DATETIME> result(num_rows);
    for (int row = 0; row < num_rows; ++row) {
        if (data_column.is_null(row)) {
            result.append_null();
            continue;
        }
        auto unix_millis = data_column.value(row);
        TimestampValue timestamp = timestamp_from_unix_millis(unix_millis);
        result.append(timestamp);
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr> remap_timestamps_calendar_const([[maybe_unused]] FunctionContext* context,
                                                    const starrocks::Columns& columns,
                                                    const CalendarState* calendar_state) {
    DCHECK_EQ(columns.size(), 4);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[3]);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    const Calendar& calendar = calendar_state->calendar;
    const std::optional<std::string>& time_unit = calendar_state->time_unit;
    int64_t time_unit_to_ms = 1;
    if (time_unit.has_value()) {
        auto iter = TIME_UNIT_TO_MS.find(time_unit.value());
        DCHECK(iter != TIME_UNIT_TO_MS.end());
        time_unit_to_ms = iter->second;
    }
    for (size_t row = 0; row < num_rows; ++row) {
        if (calendar_state->is_null || timestamp_viewer.is_null(row) || !time_unit.has_value()) {
            result.append_null();
            continue;
        }
        const auto timestamp = timestamp_viewer.value(row);
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        int64_t milliseconds;
        if (calendar_state->is_empty) {
            milliseconds = remap_timestamp_ms(timestamp);
        } else {
            if (calendar.requires_calendar_id() && !calendar_id.has_value()) {
                result.append_null();
                continue;
            }
            if (!calendar.requires_calendar_id()) {
                calendar_id = std::nullopt;
            }
            std::optional<int64_t> rv = calendar.remap_timestamp_ms(timestamp, calendar_id);
            if (!rv.has_value()) {
                result.append_null();
                continue;
            }
            milliseconds = rv.value();
        }
        result.append(milliseconds / time_unit_to_ms);
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr> remap_timestamps_calendar_general([[maybe_unused]] FunctionContext* context,
                                                      const starrocks::Columns& columns) {
    LOG(INFO) << "Non-const version of remap_timestamps_calendar is called.\n";
    DCHECK_EQ(columns.size(), 4);
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[2] });
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[1]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[3]);
    ColumnPtr calendar_array_column = ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[2]);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    std::optional<CalendarState> calendar_state = std::nullopt;
    for (size_t row = 0; row < num_rows; ++row) {
        if (timestamp_viewer.is_null(row) || time_unit_viewer.is_null(row) || columns[2]->is_null(row)) {
            result.append_null();
            continue;
        }
        const std::string time_unit = time_unit_viewer.value(row).to_string();
        const auto timestamp = timestamp_viewer.value(row);
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        if (!config::treat_calendar_column_as_constant_in_calendar_functions || !calendar_state.has_value()) {
            ASSIGN_OR_RETURN(calendar_state,
                             CalendarState::create_calendar_state(calendar_array_column, row, false, false,
                                                                  time_unit,
                                                                  CalendarFunction::REMAP_TIMESTAMPS_CALENDAR));
        }
        ASSIGN_OR_RETURN(const std::optional<int64_t> time,
                         remap_timestamp_calendar(timestamp, time_unit, calendar_state.value(), calendar_id));
        if (time.has_value()) {
            result.append(time.value());
        } else {
            result.append_null();
        }
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr> CelonisTimeFunctions::remap_timestamps_calendar([[maybe_unused]] FunctionContext* context,
                                                                    const starrocks::Columns& columns) {
    return func(context, columns, remap_timestamps_calendar_const, remap_timestamps_calendar_general);
}


StatusOr<ColumnPtr> CelonisTimeFunctions::date_between([[maybe_unused]] FunctionContext* context,
                                                       const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 3);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer begin_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[1]);
    ColumnViewer end_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[2]);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    for (auto row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row) || columns[2]->is_null(row)) {
            result.append_null();
            continue;
        }
        auto timestamp = timestamp_viewer.value(row);
        auto begin_timestamp = begin_timestamp_viewer.value(row);
        auto end_timestamp = end_timestamp_viewer.value(row);
        if (timestamp >= begin_timestamp && timestamp < end_timestamp) {
            result.append(1L);
        } else {
            result.append(0L);
        }
    }
    return result.build(all_const);
}

static StatusOr<std::optional<bool>>
timestamp_in_calendar(const TimestampValue& timestamp,
                      const CalendarState& calendar_state,
                      const std::optional<std::string>& calendar_id) {
    if (calendar_state.is_empty) {
        return std::nullopt;
    } else {
        return calendar_state.calendar.is_timestamp_in(timestamp, calendar_id);
    }
}

StatusOr<ColumnPtr>
get_calendar_entry_start_general([[maybe_unused]] FunctionContext* context, const starrocks::Columns& columns) {
    LOG(INFO) << "Non-const version of get_calendar_entry_start is called.\n";
    DCHECK_EQ(columns.size(), 3);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    if (num_rows == 0) {
        return result.build(all_const);
    }
    return Status::NotSupported("Non-const calendar is not supported in get_calendar_entry_start.");
}

StatusOr<ColumnPtr>
get_calendar_entry_start_const([[maybe_unused]] FunctionContext* context, const starrocks::Columns& columns,
                               const CalendarState* calendar_state) {
    DCHECK_EQ(columns.size(), 3);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer index_viewer = ColumnViewer<TYPE_INT>(columns[0]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);

    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    const Calendar& calendar = calendar_state->calendar;
    for (size_t row = 0; row < num_rows; ++row) {
        if (calendar_state->is_null || calendar_state->is_empty || index_viewer.is_null(row)) {
            result.append_null();
            continue;
        }
        auto index = index_viewer.value(row);
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        const std::optional<int64_t> begin_ms = calendar.get_entry_start(index, calendar_id);
        if (begin_ms.has_value()) {
            result.append(begin_ms.value());
        } else {
            result.append_null();
        }
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr> CelonisTimeFunctions::get_calendar_entry_start(FunctionContext* context,
                                                                   const starrocks::Columns& columns) {
    return func(context, columns, get_calendar_entry_start_const, get_calendar_entry_start_general);
}

Status CelonisTimeFunctions::get_calendar_entry_start_prepare(FunctionContext* context,
                                                              FunctionContext::FunctionStateScope scope) {
    RETURN_IF_ERROR(prepare(context, scope, 3, 1, std::nullopt, CalendarFunction::GET_CALENDAR_ENTRY_START));
    return Status::OK();
}

Status CelonisTimeFunctions::get_calendar_entry_start_close(FunctionContext* context,
                                                            FunctionContext::FunctionStateScope scope) {
    return close(context, scope);
}

StatusOr<ColumnPtr> in_calendar_general([[maybe_unused]] FunctionContext* context, const starrocks::Columns& columns) {
    LOG(INFO) << "Non-const version of in_calendar is called.\n";
    DCHECK_EQ(columns.size(), 3);
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[1] });
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    ColumnPtr calendar_array_column = ColumnHelper::unpack_and_duplicate_const_column(columns[1]->size(), columns[1]);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    std::optional<CalendarState> calendar_state = std::nullopt;
    for (size_t row = 0; row < num_rows; ++row) {
        if (timestamp_viewer.is_null(row) || columns[1]->is_null(row)) {
            result.append_null();
            continue;
        }
        auto timestamp = timestamp_viewer.value(row);
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        if (!config::treat_calendar_column_as_constant_in_calendar_functions || !calendar_state.has_value()) {
            ASSIGN_OR_RETURN(calendar_state,
                             CalendarState::create_calendar_state(calendar_array_column, row, false, false,
                                                                  std::nullopt, CalendarFunction::IN_CALENDAR));
        }
        ASSIGN_OR_RETURN(std::optional<bool> is_in,
                         timestamp_in_calendar(timestamp, calendar_state.value(), calendar_id));
        if (is_in.has_value()) {
            result.append(is_in.value() ? 1L : 0L);
        } else {
            result.append_null();
        }

    }
    return result.build(all_const);
}

StatusOr<ColumnPtr> in_calendar_const([[maybe_unused]] FunctionContext* context, const starrocks::Columns& columns,
                                      const CalendarState* calendar_state) {
    DCHECK_EQ(columns.size(), 3);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);

    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    const Calendar& calendar = calendar_state->calendar;
    for (size_t row = 0; row < num_rows; ++row) {
        if (calendar_state->is_null || calendar_state->is_empty || timestamp_viewer.is_null(row)) {
            result.append_null();
            continue;
        }
        auto timestamp = timestamp_viewer.value(row);
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        const std::optional<bool> is_in = calendar.is_timestamp_in(timestamp, calendar_id);
        if (is_in.has_value()) {
            result.append(is_in.value() ? 1L : 0L);
        } else {
            result.append_null();
        }
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr> CelonisTimeFunctions::in_calendar(FunctionContext* context,
                                                      const starrocks::Columns& columns) {
    return func(context, columns, in_calendar_const, in_calendar_general);
}

Status CelonisTimeFunctions::in_calendar_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    RETURN_IF_ERROR(prepare(context, scope, 3, 1, std::nullopt, CalendarFunction::IN_CALENDAR));
    return Status::OK();
}

Status CelonisTimeFunctions::in_calendar_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    return close(context, scope);
}

Status CelonisTimeFunctions::remap_timestamps_calendar_prepare(FunctionContext* context,
                                                               FunctionContext::FunctionStateScope scope) {
    RETURN_IF_ERROR(prepare(context, scope, 4, 2, 1, CalendarFunction::REMAP_TIMESTAMPS_CALENDAR));
    return Status::OK();
}

Status CelonisTimeFunctions::remap_timestamps_calendar_close(FunctionContext* context,
                                                             FunctionContext::FunctionStateScope scope) {
    return close(context, scope);
}

static StatusOr<celonis::accelerator::Calendar>
get_calendar(const ColumnPtr& calendar_column, int row) {
    ASSIGN_OR_RETURN(const std::string calendar_string, get_calendar_string(calendar_column, row));
    return validate_and_to_proto(calendar_string, false);
}

StatusOr<ColumnPtr> CelonisTimeFunctions::make_intersect_calendar(starrocks::FunctionContext* context,
                                                                  const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 2);
    RETURN_IF_COLUMNS_ONLY_NULL(columns);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);

    int offset = 0;
    UInt32Column::Ptr array_offsets = UInt32Column::create();
    array_offsets->reserve(num_rows + 1);

    BinaryColumn::Ptr array_binary_column = BinaryColumn::create();
    auto null_column = NullColumn::create();

    for (size_t row = 0; row < num_rows; ++row) {
        array_offsets->append(offset);
        if (columns[0]->is_null(row) || columns[1]->is_null(row)) {
            null_column->append(1);
            continue;
        }
        StatusOr<celonis::accelerator::Calendar> status_or_calendar1 = get_calendar(columns[0], row);
        StatusOr<celonis::accelerator::Calendar> status_or_calendar2 = get_calendar(columns[1], row);
        if (!status_or_calendar1.ok() || !status_or_calendar2.ok()) {
            null_column->append(1);
            continue;
        }
        celonis::accelerator::Calendar calendar_proto;
        calendar_proto.mutable_intersect_calendar()->mutable_calendar1()->CopyFrom(status_or_calendar1.value());
        calendar_proto.mutable_intersect_calendar()->mutable_calendar2()->CopyFrom(status_or_calendar2.value());
        std::optional<std::string> calendar_string = to_base64_encoded_string(calendar_proto);
        if (!calendar_string.has_value()) {
            null_column->append(1);
            continue;
        }
        null_column->append(0);
        std::vector<std::string> calendar_pieces;
        calendar_pieces.reserve((calendar_string->size() + MAX_STRING_SIZE - 1) / MAX_STRING_SIZE);
        for (size_t i = 0; i < calendar_string->size(); i += MAX_STRING_SIZE) {
            calendar_pieces.emplace_back(calendar_string->substr(i, MAX_STRING_SIZE));
        }
        for (const auto& calendar_piece: calendar_pieces) {
            array_binary_column->append(Slice(calendar_piece.c_str()));
        }
        offset += calendar_pieces.size();
    }
    array_offsets->append(offset);
    if (all_const) {
        return ConstColumn::create(NullableColumn::create(
                ArrayColumn::create(NullableColumn::create(array_binary_column, NullColumn::create(offset, 0)),
                                    array_offsets), null_column), num_rows);
    }
    return NullableColumn::create(
            ArrayColumn::create(NullableColumn::create(array_binary_column, NullColumn::create(offset, 0)),
                                array_offsets), null_column);
}

static StatusOr<std::optional<TimestampValue>>
add_timeunits(const TimestampValue& timestamp, const std::string& time_unit, int64_t add_value,
              const CalendarState& calendar_state,
              std::optional<std::string>& calendar_id) {
    if (calendar_state.is_empty) {
        if (calendar_id.has_value()) {
            return Status::InvalidArgument(
                    "Calendar ID column should not be set when calendar specification is not set.");
        }
        return add_timeunits_helper(timestamp, time_unit, add_value);
    } else {
        return calendar_state.calendar.add_timeunits(timestamp, time_unit, add_value, calendar_id);
    }
}

class DateFilters {
public:
    DateFilters() {}

    DateFilters(size_t row, const ColumnPtr& years_column, const ColumnPtr& quarters_column,
                const ColumnPtr& months_column,
                const ColumnPtr& weeks_column, const ColumnPtr& days_column) {
        populate_filters(years_, row, years_column);
        populate_filters(quarters_, row, quarters_column);
        populate_filters(months_, row, months_column);
        populate_filters(weeks_, row, weeks_column);
        populate_filters(days_, row, days_column);
    }

    bool matches(const TimestampValue& timestamp) const {
        int year, month, day, hour, minute, second, usec;
        timestamp.to_timestamp(&year, &month, &day, &hour, &minute, &second, &usec);
        if (!years_.empty() && !years_.count(year)) {
            return false;
        }
        if (!quarters_.empty()) {
            const int quarter = MONTH_TO_QUARTER[month];
            if (!quarters_.count(quarter)) {
                return false;
            }
        }
        if (!months_.empty() && !months_.count(month)) {
            return false;
        }
        if (!weeks_.empty()) {
            const int week = get_week_number(year, month, day);
            if (!weeks_.count(week)) {
                return false;
            }
        }
        if (!days_.empty() && !days_.count(day)) {
            return false;
        }
        return true;
    }

private:

    void populate_filters(HashSet<int64_t>& filters, size_t row, ColumnPtr column) {
        DCHECK(row < column->size());
        ColumnPtr array_column = ColumnHelper::unpack_and_duplicate_const_column(column->size(), column);
        UnnestedArrayData array_data = prepare_array_input(array_column.get());
        const auto& elements = down_cast<const RunTimeColumnType<TYPE_BIGINT>&>(*array_data.elements).get_data().data();
        const auto& offsets = array_data.offsets->get_data().data();
        const size_t start = offsets[row];
        const size_t end = offsets[row + 1];
        for (auto i = start; i < end; ++i) {
            if (array_data.null_elements != nullptr && (*array_data.null_elements)[i] != 0) {
                continue;
            }
            filters.insert(elements[i]);
        }
    }

    HashSet<int64_t> years_;
    HashSet<int64_t> quarters_;
    HashSet<int64_t> months_;
    HashSet<int64_t> weeks_;
    HashSet<int64_t> days_;
};

struct DateMatchStateFragmentLocal {
    DateFilters date_filters;
    ScalarFunction function;
    bool has_null_filter = false;
};

StatusOr<ColumnPtr> CelonisTimeFunctions::date_match([[maybe_unused]] FunctionContext* context,
                                                     const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 6);
    const auto* state = reinterpret_cast<const DateMatchStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    return state->function(context, columns);
}

StatusOr<ColumnPtr> CelonisTimeFunctions::date_match_non_constant_filters([[maybe_unused]] FunctionContext* context,
                                                                          const starrocks::Columns& columns) {
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    for (auto row = 0; row < num_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row) || columns[2]->is_null(row) ||
            columns[3]->is_null(row) || columns[4]->is_null(row) || columns[5]->is_null(row)) {
            result.append_null();
            continue;
        }
        DateFilters date_filters(row, columns[1], columns[2], columns[3], columns[4], columns[5]);
        auto timestamp = timestamp_viewer.value(row);
        result.append(date_filters.matches(timestamp) ? 1L : 0L);
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr> CelonisTimeFunctions::date_match_constant_filters([[maybe_unused]] FunctionContext* context,
                                                                      const starrocks::Columns& columns) {
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    const auto* state = reinterpret_cast<const DateMatchStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    ColumnBuilder<TYPE_BIGINT> result(num_rows);
    if (state->has_null_filter) {
        result.append_nulls(num_rows);
        return result.build(true);
    }
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    for (auto row = 0; row < num_rows; ++row) {
        if (timestamp_viewer.is_null(row)) {
            result.append_null();
            continue;
        }
        auto timestamp = timestamp_viewer.value(row);
        result.append(state->date_filters.matches(timestamp) ? 1L : 0L);
    }
    return result.build(all_const);
}

Status CelonisTimeFunctions::date_match_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope != FunctionContext::FRAGMENT_LOCAL) {
        return Status::OK();
    }
    auto state = new DateMatchStateFragmentLocal();
    context->set_function_state(scope, state);

    auto years_column = context->get_constant_column(1);
    auto quarters_column = context->get_constant_column(2);
    auto months_column = context->get_constant_column(3);
    auto weeks_column = context->get_constant_column(4);
    auto days_column = context->get_constant_column(5);
    if (years_column == nullptr || quarters_column == nullptr || months_column == nullptr || weeks_column == nullptr ||
        days_column == nullptr) {
        state->function = date_match_non_constant_filters;
        return Status::OK();
    }
    state->function = date_match_constant_filters;
    if (years_column->empty() || quarters_column->empty() || months_column->empty() || weeks_column->empty() ||
        days_column->empty()) {
        return Status::OK();
    }
    if (years_column->is_null(0) || quarters_column->is_null(0) || months_column->is_null(0) ||
        weeks_column->is_null(0) || days_column->is_null(0)) {
        state->has_null_filter = true;
        return Status::OK();
    }
    state->date_filters = DateFilters(0, years_column, quarters_column, months_column, weeks_column, days_column);
    return Status::OK();
}

Status CelonisTimeFunctions::date_match_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    if (scope == FunctionContext::FRAGMENT_LOCAL) {
        const auto* state = reinterpret_cast<const DateMatchStateFragmentLocal*>(
                context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
        delete state;
    }
    return Status::OK();
}

Status CelonisTimeFunctions::timeunits_between_calendar_prepare(FunctionContext* context,
                                                                FunctionContext::FunctionStateScope scope) {
    RETURN_IF_ERROR(prepare(context, scope, 5, 3, 2, CalendarFunction::TIMEUNITS_BETWEEN_CALENDAR));
    return Status::OK();
}

Status CelonisTimeFunctions::timeunits_between_calendar_close(FunctionContext* context,
                                                              FunctionContext::FunctionStateScope scope) {
    return close(context, scope);
}

StatusOr<ColumnPtr> timeunits_between_calendar_general([[maybe_unused]] FunctionContext* context,
                                                       const starrocks::Columns& columns) {
    LOG(INFO) << "Non-const version of timeunits_between_calendar is called.\n";
    DCHECK_EQ(columns.size(), 5);
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[3] });
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer from_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer to_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[1]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[4]);
    ColumnPtr calendar_array_column = ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[3]);
    ColumnBuilder<TYPE_DOUBLE> result(num_rows);
    std::optional<CalendarState> calendar_state = std::nullopt;
    for (size_t row = 0; row < num_rows; ++row) {
        if (from_timestamp_viewer.is_null(row) || to_timestamp_viewer.is_null(row) || time_unit_viewer.is_null(row) ||
            columns[3]->is_null(row)) {
            result.append_null();
            continue;
        }
        const std::string time_unit = time_unit_viewer.value(row).to_string();
        auto from_timestamp = from_timestamp_viewer.value(row);
        auto to_timestamp = to_timestamp_viewer.value(row);
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        if (!config::treat_calendar_column_as_constant_in_calendar_functions || !calendar_state.has_value()) {
            ASSIGN_OR_RETURN(calendar_state,
                             CalendarState::create_calendar_state(calendar_array_column, row, false, true,
                                                                  time_unit,
                                                                  CalendarFunction::TIMEUNITS_BETWEEN_CALENDAR));
        }
        ASSIGN_OR_RETURN(const std::optional<double> diff,
                         timeunits_between(from_timestamp, to_timestamp, time_unit, calendar_state.value(),
                                           calendar_id));
        if (diff.has_value()) {
            result.append(diff.value());
        } else {
            result.append_null();
        }
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr> timeunits_between_calendar_const([[maybe_unused]] FunctionContext* context,
                                                     const starrocks::Columns& columns,
                                                     const CalendarState* calendar_state) {
    DCHECK_EQ(columns.size(), 5);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer from_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer to_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[1]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[4]);
    const std::string& time_unit = calendar_state->time_unit.value();
    ColumnBuilder<TYPE_DOUBLE> result(num_rows);
    for (size_t row = 0; row < num_rows; ++row) {
        if (from_timestamp_viewer.is_null(row) || to_timestamp_viewer.is_null(row) || time_unit_viewer.is_null(row) ||
            calendar_state->is_null) {
            result.append_null();
            continue;
        }
        const auto from_timestamp = from_timestamp_viewer.value(row);
        const auto to_timestamp = to_timestamp_viewer.value(row);
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        ASSIGN_OR_RETURN(std::optional<double> diff,
                         timeunits_between(from_timestamp, to_timestamp, time_unit, *calendar_state, calendar_id))
        if (diff.has_value()) {
            result.append(diff.value());
        } else {
            result.append_null();
        }
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr> CelonisTimeFunctions::timeunits_between_calendar(FunctionContext* context,
                                                                     const starrocks::Columns& columns) {
    return func(context, columns, timeunits_between_calendar_const, timeunits_between_calendar_general);
}

Status CelonisTimeFunctions::add_timeunits_calendar_prepare(FunctionContext* context,
                                                            FunctionContext::FunctionStateScope scope) {
    RETURN_IF_ERROR(prepare(context, scope, 5, 3, 2, CalendarFunction::ADD_TIMEUNITS_CALENDAR));
    return Status::OK();
}

Status CelonisTimeFunctions::add_timeunits_calendar_close(FunctionContext* context,
                                                          FunctionContext::FunctionStateScope scope) {
    return close(context, scope);
}

static StatusOr<ColumnPtr> add_timeunits_calendar_general([[maybe_unused]] FunctionContext* context,
                                                          const starrocks::Columns& columns) {
    LOG(INFO) << "Non-const version of add_timeunits_calendar is called.\n";
    DCHECK_EQ(columns.size(), 5);
    RETURN_IF_COLUMNS_ONLY_NULL({ columns[3] });
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer add_value_viewer = ColumnViewer<TYPE_BIGINT>(columns[1]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[4]);
    ColumnPtr calendar_array_column = ColumnHelper::unpack_and_duplicate_const_column(num_rows, columns[3]);
    ColumnBuilder<TYPE_DATETIME> result(num_rows);
    std::optional<CalendarState> calendar_state = std::nullopt;
    for (size_t row = 0; row < num_rows; ++row) {
        if (timestamp_viewer.is_null(row) || add_value_viewer.is_null(row) || time_unit_viewer.is_null(row) ||
            columns[3]->is_null(row)) {
            result.append_null();
            continue;
        }
        const std::string time_unit = time_unit_viewer.value(row).to_string();
        const auto timestamp = timestamp_viewer.value(row);
        if (!is_timestamp_in_valid_range(timestamp)) {
            result.append_null();
            continue;
        }
        auto add_value = add_value_viewer.value(row);
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        if (!config::treat_calendar_column_as_constant_in_calendar_functions || !calendar_state.has_value()) {
            ASSIGN_OR_RETURN(calendar_state,
                             CalendarState::create_calendar_state(calendar_array_column, row, false, false,
                                                                  time_unit, CalendarFunction::ADD_TIMEUNITS_CALENDAR));
        }
        ASSIGN_OR_RETURN(const std::optional<TimestampValue> new_timestamp,
                         add_timeunits(timestamp, time_unit, add_value, calendar_state.value(), calendar_id));
        if (new_timestamp.has_value()) {
            result.append(new_timestamp.value());
        } else {
            result.append_null();
        }
    }
    return result.build(all_const);
}

static StatusOr<ColumnPtr> add_timeunits_calendar_const([[maybe_unused]] FunctionContext* context,
                                                        const starrocks::Columns& columns,
                                                        const CalendarState* calendar_state) {
    DCHECK_EQ(columns.size(), 5);
    auto [all_const, num_rows] = ColumnHelper::num_packed_rows(columns);
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer add_value_viewer = ColumnViewer<TYPE_BIGINT>(columns[1]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[4]);
    ColumnBuilder<TYPE_DATETIME> result(num_rows);
    const Calendar& calendar = calendar_state->calendar;
    const std::string& time_unit = calendar_state->time_unit.value();
    const bool is_calendar_empty = calendar_state->is_empty;
    for (size_t row = 0; row < num_rows; ++row) {
        if (timestamp_viewer.is_null(row) || add_value_viewer.is_null(row) || time_unit_viewer.is_null(row) ||
            calendar_state->is_null) {
            result.append_null();
            continue;
        }
        const auto timestamp = timestamp_viewer.value(row);
        if (!is_timestamp_in_valid_range(timestamp)) {
            result.append_null();
            continue;
        }
        auto add_value = add_value_viewer.value(row);
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        if (is_calendar_empty) {
            if (calendar_id.has_value()) {
                return Status::InvalidArgument(
                        "Calendar ID column should not be set when calendar specification is not set.");
            }
            result.append(add_timeunits_helper(timestamp, time_unit, add_value));
            continue;
        }
        const std::optional<TimestampValue> new_timestamp = calendar.add_timeunits(timestamp, time_unit, add_value,
                                                                                   calendar_id);
        if (new_timestamp.has_value()) {
            result.append(new_timestamp.value());
        } else {
            result.append_null();
        }
    }
    return result.build(all_const);
}

StatusOr<ColumnPtr> CelonisTimeFunctions::add_timeunits_calendar([[maybe_unused]] FunctionContext* context,
                                                                 const starrocks::Columns& columns) {
    return func(context, columns, add_timeunits_calendar_const, add_timeunits_calendar_general);
}

} // namespace starrocks
