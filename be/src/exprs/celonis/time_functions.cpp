#include "exprs/celonis/time_functions.h"

#include "column/column_builder.h"
#include "column/column_helper.h"
#include "column/column_viewer.h"
#include "column/array_column.h"
#include "exprs/base64.h"
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

static const int64_t NUM_MILLISECONDS_PER_WEEK = NUM_MILLISECONDS_PER_DAY * NUM_DAYS_PER_WEEK;

static const int64_t NUM_MICROSECONDS_PER_MILLISECONDS = 1000L;

// SR places an upper limit of 1M of STRING. We use 900K which is less than 1M.
static const size_t MAX_STRING_SIZE = 900000;

static const int MONTH_TO_QUARTER[13] = {0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4};

static const TimestampValue EPOCH = TimestampValue::create(1970, 1, 1, 0, 0, 0);

static const TimestampValue MAX_YEAR = TimestampValue::create(10000, 1, 1, 0, 0, 0);

static const TimestampValue MIN_YEAR = TimestampValue::create(1400, 1, 1, 0, 0, 0);

static TimestampValue
add_timeunits_helper(const TimestampValue& timestamp, const std::string& time_unit, int64_t add_value) {
    std::vector<int> adds;
    const int64_t max_int = std::numeric_limits<int>::max();
    const int64_t min_int = std::numeric_limits<int>::min();
    while (add_value > max_int) {
        adds.push_back(max_int);
        add_value -= max_int;
    }
    while (add_value < min_int) {
        adds.push_back(min_int);
        add_value -= min_int;
    }
    if (add_value != 0) {
        adds.push_back(add_value);
    }
    TimestampValue rv = timestamp;
    for (auto add: adds) {
        if (time_unit == "DAYS" || time_unit == "WORKDAYS") {
            rv = rv.add<TimeUnit::DAY>(add);
        } else if (time_unit == "HOURS") {
            rv = rv.add<TimeUnit::HOUR>(add);
        } else if (time_unit == "MINUTES") {
            rv = rv.add<TimeUnit::MINUTE>(add);
        } else if (time_unit == "SECONDS") {
            rv = rv.add<TimeUnit::SECOND>(add);
        } else {
            rv = rv.add<TimeUnit::MILLISECOND>(add);
        }
    }
    return rv;
}

static int get_year(const TimestampValue& value) {
    int year, month, day, hour, minute, second, usec;
    value.to_timestamp(&year, &month, &day, &hour, &minute, &second, &usec);
    return year;
}

static int get_year(int64_t millis) {
    TimestampValue t = add_timeunits_helper(EPOCH, "MILLISECONDS", millis);
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

static int64_t remap_timestamp_ms(const TimestampValue& timestamp) {
    return timestamp.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;
}

static int64_t millis_between(const TimestampValue& from_timestamp, const TimestampValue& to_timestamp) {
    return remap_timestamp_ms(to_timestamp) - remap_timestamp_ms(from_timestamp);
}

struct TimeRange {
    int64_t begin_ms;
    int64_t end_ms;
    bool is_weekly;

    TimeRange(int64_t begin_ms, int64_t end_ms) : begin_ms(begin_ms), end_ms(end_ms), is_weekly(false) {}

    TimeRange(int64_t begin_ms, int64_t end_ms, bool is_weekly) : begin_ms(begin_ms), end_ms(end_ms),
                                                                  is_weekly(is_weekly) {}

    TimeRange(const TimeRange& other) : begin_ms(other.begin_ms), end_ms(other.end_ms), is_weekly(other.is_weekly) {}

    TimeRange& operator=(const TimeRange& other) {
        if (this != &other) {
            begin_ms = other.begin_ms;
            end_ms = other.end_ms;
            is_weekly = other.is_weekly;
        }
        return *this;
    }

    virtual ~TimeRange() = default;

    // Computes the intersection with [left_ms, right_ms)
    std::vector<TimeRange> intersect(int64_t left_ms, int64_t right_ms) const {
        std::vector<TimeRange> rv;
        if (!is_weekly) {
            int64_t start = std::max(begin_ms, left_ms);
            int64_t end = std::min(end_ms, right_ms);
            if (end > start) {
                rv.emplace_back(start, end);
            }
            return rv;
        }
        const int64_t begin = left_ms;
        const int64_t end = right_ms;
        int64_t cur_begin = begin_ms;
        int64_t cur_end = end_ms;
        int64_t period = NUM_MILLISECONDS_PER_WEEK;
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
                rv.emplace_back(left, right);
            }
            cur_begin += period;
            cur_end += period;
        }
        return rv;
    }

    // Computes the overlap in milliseconds with [left_ms, right_ms)
    int64_t compute_overlap(int64_t left_ms, int64_t right_ms) const {
        if (!is_weekly) {
            int64_t start = std::max(begin_ms, left_ms);
            int64_t end = std::min(end_ms, right_ms);
            return (end > start) ? end - start : 0;
        }
        const int64_t begin = left_ms;
        const int64_t end = right_ms;
        int64_t cur_begin = begin_ms;
        int64_t cur_end = end_ms;
        int64_t period = NUM_MILLISECONDS_PER_WEEK;
        // move [cur_begin, cur_end) to the left of [begin, end)
        if (cur_end > begin) {
            int64_t n_periods = (cur_end - begin + period - 1) / period;
            cur_begin -= n_periods * period;
            cur_end -= n_periods * period;
        }
        int64_t rv = 0L;
        // move [cur_begin, cur_end) to right to pass [begin, end)
        while (true) {
            if (cur_begin >= end) {
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

    bool is_ms_in(int64_t ms) const {
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
};

static void merge_non_weekly_time_ranges(std::vector<TimeRange>& time_ranges) {
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
        if (calendar_proto.has_intersect_calendar()) {
            handle_intersect_calendar(calendar_proto.intersect_calendar());
        }
        set_scope();
    }

    bool requires_calendar_id() const {
        auto iter = id_to_time_ranges_.find(std::nullopt);
        if (iter == id_to_time_ranges_.end()) {
            return !id_to_time_ranges_.empty();
        }
        return id_to_time_ranges_.size() > 1;
    }

    int64_t
    remap_timestamp_ms(const TimestampValue& timestamp, const std::optional<std::string>& calendar_id) const {
        int64 ms = timestamp.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;
        int64_t left_ms = ms < 0 ? ms : 0L;
        int64_t right_ms = ms < 0 ? 0L : ms;
        int64_t overlap = compute_overlap(left_ms, right_ms, calendar_id);
        return ms < 0 ? -overlap : overlap;
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
        int64_t overlap = compute_overlap(left_ms, right_ms, calendar_id, round_to_day);
        return (to_ms >= from_ms) ? overlap : -overlap;
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
        const bool is_workdays = (time_unit == "WORKDAYS");
        const bool left_to_right = add_value >= 0;
        std::priority_queue<TimeRange, std::vector<TimeRange>, CompareTimeRange> pq(CompareTimeRange{left_to_right});
        std::vector<TimeRange> time_ranges;
        auto itr = id_to_time_ranges_.find(calendar_id);
        if (itr != id_to_time_ranges_.end()) {
            for (const auto& cur_time_range: itr->second) {
                if (is_workdays) {
                    auto time_range_copy = cur_time_range;
                    time_range_copy.begin_ms = floor_to_nearest_multiple(cur_time_range.begin_ms,
                                                                         NUM_MILLISECONDS_PER_DAY);
                    time_range_copy.end_ms = ceil_to_nearest_multiple(cur_time_range.end_ms, NUM_MILLISECONDS_PER_DAY);
                    time_ranges.push_back(time_range_copy);
                } else {
                    time_ranges.push_back(cur_time_range);
                }
            }
        }
        int64_t ms = timestamp.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;
        for (auto& time_range: time_ranges) {
            if (!time_range.is_weekly) {
                pq.push(time_range);
            } else {
                int64_t a = time_range.begin_ms;
                int64_t b = time_range.end_ms;
                // move [a, b) to the left of ms
                if (left_to_right && a - ms >= NUM_MILLISECONDS_PER_WEEK) {
                    int64_t nperiods = (a - ms + NUM_MILLISECONDS_PER_WEEK - 1) / NUM_MILLISECONDS_PER_WEEK;
                    a -= nperiods * NUM_MILLISECONDS_PER_WEEK;
                    b -= nperiods * NUM_MILLISECONDS_PER_WEEK;
                }
                // move [a, b) to the right of ms
                if (!left_to_right && ms - b >= NUM_MILLISECONDS_PER_WEEK) {
                    int64_t nperiods = (ms - b + NUM_MILLISECONDS_PER_WEEK - 1) / NUM_MILLISECONDS_PER_WEEK;
                    a += nperiods * NUM_MILLISECONDS_PER_WEEK;
                    b += nperiods * NUM_MILLISECONDS_PER_WEEK;
                }
                pq.emplace(a, b, true);
            }
        }
        auto iter = TIME_UNIT_TO_MS.find(time_unit);
        DCHECK(iter != TIME_UNIT_TO_MS.end());
        int64_t ms_left = std::abs(add_value * iter->second);
        int64_t max_ms = MAX_YEAR.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;
        int64_t min_ms = MIN_YEAR.diff_microsecond(EPOCH) / NUM_MICROSECONDS_PER_MILLISECONDS;
        // [begin_ms, end_ms)
        int64_t begin_ms = left_to_right ? ms : min_ms;
        int64_t end_ms = left_to_right ? max_ms : ms;
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
            if (top.is_weekly) {
                int64_t new_begin_ms = top.begin_ms + period;
                int64_t new_end_ms = top.end_ms + period;
                if (!((left_to_right && new_begin_ms >= max_ms) || (!left_to_right && new_end_ms < min_ms))) {
                    pq.emplace(new_begin_ms, new_end_ms, true);
                }
            }
        }
        if (rv.has_value()) {
            auto res_timestamp = add_timeunits_helper(EPOCH, "MILLISECONDS", rv.value());
            if (is_out_scope(res_timestamp, calendar_id)) {
                return std::nullopt;
            }
            if (!is_workdays) {
                return res_timestamp;
            }
            // For WORKDAYS, keep the time of the day unchanged.
            int new_year, new_month, new_day, new_hour, new_minute, new_second, new_usec;
            res_timestamp.to_timestamp(&new_year, &new_month, &new_day, &new_hour, &new_minute, &new_second, &new_usec);
            int old_year, old_month, old_day, old_hour, old_minute, old_second, old_usec;
            timestamp.to_timestamp(&old_year, &old_month, &old_day, &old_hour, &old_minute, &old_second, &old_usec);
            res_timestamp.from_timestamp(new_year, new_month, new_day, old_hour, old_minute, old_second, old_usec);
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
        auto itr = id_to_time_ranges_.find(calendar_id);
        if (itr != id_to_time_ranges_.end()) {
            for (const auto& cur_time_range: itr->second) {
                if (cur_time_range.is_ms_in(ms)) {
                    return true;
                }
            }
        }
        return false;
    }

private:
    using IdToTimeRangesMap = std::unordered_map<std::optional<std::string>, std::vector<TimeRange>>;
    using IdToWeekdayMap = std::unordered_map<std::optional<std::string>, std::unordered_map<int, celonis::accelerator::WeekdayCalendarEntry>>;

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
        IdToTimeRangesMap id_to_time_ranges;
        for (const auto& entry: factory_calendar.entries()) {
            if (!entry.has_start_date() || !entry.has_end_date() || entry.start_date() > entry.end_date()) {
                continue;
            }
            if (entry.has_calendar_id()) {
                id_to_time_ranges[entry.calendar_id()].emplace_back(entry.start_date(), entry.end_date());
            } else {
                id_to_time_ranges[std::nullopt].emplace_back(entry.start_date(), entry.end_date());
            }
        }
        for (auto& kv: id_to_time_ranges) {
            merge_non_weekly_time_ranges(kv.second);
        }
        return id_to_time_ranges;
    }

    void populate_id_to_time_ranges(const IdToTimeRangesMap& id_to_time_ranges) {
        for (const auto& kv: id_to_time_ranges) {
            auto& cur_time_ranges = id_to_time_ranges_[kv.first];
            for (const auto& time_range: kv.second) {
                cur_time_ranges.push_back(time_range);
            }
        }
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
                std::unordered_map<int, celonis::accelerator::WeekdayCalendarEntry> new_index_to_weekday;
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
        for (int i = 0; i < entry.is_workday_size(); ++i) {
            if (entry.is_workday(i)) {
                bit_set.set(i, true);
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
                time_ranges.emplace_back(begin, begin + NUM_MILLISECONDS_PER_DAY);
            }
        }
        return time_ranges;
    }

    IdToTimeRangesMap to_time_ranges(const celonis::accelerator::WorkdayCalendar& workday_calendar) {
        std::unordered_map<std::optional<std::string>, std::map<int, std::bitset<366>>> year_to_bitset_by_id;
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

    int64_t compute_overlap(int64_t left_ms, int64_t right_ms, const std::optional<std::string>& calendar_id,
                            bool round_to_day = false) const {
        int64_t rv = 0L;
        auto itr = id_to_time_ranges_.find(calendar_id);
        if (itr != id_to_time_ranges_.end()) {
            for (const auto& cur_time_range: itr->second) {
                if (!round_to_day) {
                    rv += cur_time_range.compute_overlap(left_ms, right_ms);
                } else {
                    auto time_range_copy = cur_time_range;
                    time_range_copy.begin_ms = floor_to_nearest_multiple(cur_time_range.begin_ms,
                                                                         NUM_MILLISECONDS_PER_DAY);
                    time_range_copy.end_ms = ceil_to_nearest_multiple(cur_time_range.end_ms, NUM_MILLISECONDS_PER_DAY);
                    rv += time_range_copy.compute_overlap(left_ms, right_ms);
                }
            }
        }
        return rv;
    }

    struct Scope {
        int min_year;
        int max_year;
    };

    IdToTimeRangesMap id_to_time_ranges_;
    std::unordered_map<std::optional<std::string>, std::optional<Scope>> id_to_scope_;
};

struct CalendarState {
    Calendar calendar;
    bool is_null;
    bool is_empty;
};

static bool
base64_encoded_string_to_calendar(const std::string &calendar_string, celonis::accelerator::Calendar &calendar) {
    int cipher_len = calendar_string.length();
    std::unique_ptr<char[]> p;
    p.reset(new char[cipher_len + 3]);

    int len = base64_decode2(calendar_string.data(), calendar_string.length(), p.get());
    std::string decoded_string(p.get(), len);
    bool success = calendar.ParseFromString(decoded_string);
    return success;
}

static bool json_string_to_calendar(const std::string& calendar_json_string, celonis::accelerator::Calendar& calendar) {
    auto status = google::protobuf::util::JsonStringToMessage(calendar_json_string, &calendar);
    return status.ok();
}

static bool string_to_calendar(const std::string& calendar_string, celonis::accelerator::Calendar& calendar) {
    if (calendar_string.find('{') != std::string::npos) {
        return json_string_to_calendar(calendar_string, calendar);
    } else {
        return base64_encoded_string_to_calendar(calendar_string, calendar);
    }
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

static Status
validate_workday_calendar(const celonis::accelerator::WorkdayCalendar& workday_calendar, bool reject_year_gap = false) {
    int has_calendar_id = -1; // not set
    std::unordered_map<std::optional<std::string>, std::vector<int64_t>> id_to_years;
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
                  const std::optional<Calendar>& calendar,
                  std::optional<std::string>& calendar_id) {
    const bool round_to_day = time_unit == "WORKDAYS";
    TimestampValue from_timestamp = from_timestamp_raw;
    TimestampValue to_timestamp = to_timestamp_raw;
    if (round_to_day) {
        from_timestamp.trunc_to_day();
        to_timestamp.trunc_to_day();
    }
    std::optional<int64_t> milliseconds;
    if (!calendar.has_value()) {
        if (calendar_id.has_value()) {
            return Status::InvalidArgument(
                    "Calendar ID column should not be set when calendar specification is not set.");
        }
        milliseconds = millis_between(from_timestamp, to_timestamp);
    } else {
        if (calendar->requires_calendar_id() && !calendar_id.has_value()) {
            return Status::InvalidArgument("Calendar ID column not provided.");
        }
        if (!calendar->requires_calendar_id()) {
            calendar_id = std::nullopt;
        }
        milliseconds = calendar->millis_between(from_timestamp, to_timestamp, calendar_id, round_to_day);
    }
    if (milliseconds.has_value()) {
        return convert_time_unit_float(time_unit, milliseconds.value());
    }
    return std::nullopt;
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

static StatusOr<std::optional<int64_t>>
remap_timestamp_calendar(const TimestampValue& input_timestamp, const std::string& time_unit,
                         const std::string& calendar_string,
                         std::optional<std::string>& calendar_id) {
    TimestampValue timestamp = input_timestamp;
    int64_t milliseconds = 0L;
    if (calendar_string.empty()) {
        if (calendar_id.has_value()) {
            return Status::InvalidArgument(
                    "Calendar ID column should not be set when calendar specification is not set.");
        }
        milliseconds = remap_timestamp_ms(timestamp);
    } else {
        ASSIGN_OR_RETURN(const celonis::accelerator::Calendar calendar_proto,
                         validate_and_to_proto(calendar_string));
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

// used in prepare methods
StatusOr<std::string> get_calendar_string(const std::vector<Datum>& array) {
    std::string calendar_str;
    for (const auto& element: array) {
        if (element.is_null()) {
            return Status::InvalidArgument("[prepare] Calendar array can not contain null values.");
        } else {
            calendar_str += element.get_slice().to_string();
        }
    }
    return calendar_str;
}

Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope, int num_cols, int calendar_index) {
    // context->is_constant_column(index) must not be used to determine if the argument is Array Literal because as of
    // 2024-02-26 it returns false for Array Literal while get_constant_column(index) returns non nullptr.
    if (scope != FunctionContext::FRAGMENT_LOCAL || context->get_num_args() != num_cols ||
        context->get_arg_type(calendar_index)->type != TYPE_ARRAY ||
        context->get_constant_column(calendar_index) == nullptr) {
        return Status::OK();
    }
    const auto calendar_column = context->get_constant_column(calendar_index);
    if (calendar_column->size() == 0) {
        return Status::OK();
    }

    if (calendar_column->is_null(0)) {
        auto* calendar_state = new CalendarState{Calendar(), true, false};
        context->set_function_state(scope, calendar_state);
        return Status::OK();
    }
    ASSIGN_OR_RETURN(const std::string calendar_string, get_calendar_string(calendar_column->get(0).get_array()));
    if (calendar_string.empty()) {
        auto* calendar_state = new CalendarState{Calendar(), false, true};
        context->set_function_state(scope, calendar_state);
        return Status::OK();
    }
    ASSIGN_OR_RETURN(const celonis::accelerator::Calendar calendar_proto,
                     validate_and_to_proto(calendar_string, true));
    auto* calendar_state = new CalendarState{Calendar(calendar_proto), false, false};
    context->set_function_state(scope, calendar_state);
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

Status validate_time_unit(const std::string& time_unit) {
    if (TIME_UNIT_TO_MS.find(time_unit) == TIME_UNIT_TO_MS.end()) {
        return Status::InvalidArgument("time unit must be one of WORKDAYS/DAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
    }
    return Status::OK();
}

StatusOr<ColumnPtr> func(FunctionContext* context, const starrocks::Columns& columns,
                         StatusOr<ColumnPtr> (* func_const)(FunctionContext*, const starrocks::Columns&,
                                                            const CalendarState*),
                         StatusOr<ColumnPtr> (* func_general)(FunctionContext*, const starrocks::Columns&)) {
    if (context == nullptr) {
        return func_general(context, columns);
    }
    auto* calendar_state = reinterpret_cast<CalendarState*>(context->get_function_state(
            FunctionContext::FRAGMENT_LOCAL));
    if (calendar_state == nullptr) {
        return func_general(context, columns);
    }
    return func_const(context, columns, calendar_state);
}

} // namespace

StatusOr<ColumnPtr>
CelonisTimeFunctions::millis_timestamp([[maybe_unused]] FunctionContext* context, const Columns& columns) {
    DCHECK_EQ(columns.size(), 1);
    const size_t n_rows = columns[0]->size();
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row)) {
            result.append_null();
            continue;
        }
        result.append(remap_timestamp_ms(timestamp_viewer.value(row)));
    }
    return result.build(ColumnHelper::is_all_const(columns));
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
        const std::string time_unit = time_unit_viewer.value(row).to_string();
        RETURN_IF_ERROR(validate_time_unit(time_unit));
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

StatusOr<ColumnPtr> remap_timestamps_calendar_general([[maybe_unused]] FunctionContext* context,
                                                      const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 4);
    size_t n_rows = columns[0]->size();
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[1]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[3]);
    UnnestedArrayData calendar_array_data = prepare_array_input(
            ColumnHelper::unpack_and_duplicate_const_column(columns[2]->size(), columns[2]).get());
    if (calendar_array_data.null_elements != nullptr) {
        return Status::InvalidArgument("Calendar array should not have null elements.");
    }
    DCHECK(calendar_array_data.elements->is_binary());
    DCHECK_EQ(calendar_array_data.offsets->size(), n_rows + 1);
    const auto& calendars = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *calendar_array_data.elements).get_data().data();
    const auto& calendar_offsets = calendar_array_data.offsets->get_data().data();

    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    for (size_t row = 0; row < n_rows; ++row) {
        if (timestamp_viewer.is_null(row) || time_unit_viewer.is_null(row) || columns[2]->is_null(row)) {
            result.append_null();
            continue;
        }
        const std::string time_unit = time_unit_viewer.value(row).to_string();
        RETURN_IF_ERROR(validate_time_unit(time_unit));
        const auto timestamp = timestamp_viewer.value(row);
        size_t start = calendar_offsets[row];
        size_t end = calendar_offsets[row + 1];
        std::string calendar_string;
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        for (size_t id = start; id < end; ++id) {
            calendar_string += calendars[id].to_string();
        }
        ASSIGN_OR_RETURN(const std::optional<int64_t> time,
                         remap_timestamp_calendar(timestamp, time_unit, calendar_string, calendar_id));
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
    return func(context, columns, remap_timestamps_calendar_const, remap_timestamps_calendar_general);
}


StatusOr<ColumnPtr> CelonisTimeFunctions::date_between([[maybe_unused]] FunctionContext* context,
                                                       const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 3);
    const size_t n_rows = columns[0]->size();
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer begin_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[1]);
    ColumnViewer end_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[2]);
    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    for (auto row = 0; row < n_rows; ++row) {
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
    return result.build(ColumnHelper::is_all_const(columns));
}

static StatusOr<std::optional<bool>>
timestamp_in_calendar(const TimestampValue& timestamp,
                      const std::string& calendar_string,
                      const std::optional<std::string>& calendar_id) {
    if (calendar_string.empty()) {
        return std::nullopt;
    } else {
        ASSIGN_OR_RETURN(const celonis::accelerator::Calendar calendar_proto,
                         validate_and_to_proto(calendar_string));
        Calendar calendar(calendar_proto);
        return calendar.is_timestamp_in(timestamp, calendar_id);
    }
}

StatusOr<ColumnPtr> in_calendar_general([[maybe_unused]] FunctionContext* context, const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 3);
    size_t n_rows = columns[0]->size();
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);

    UnnestedArrayData calendar_array_data = prepare_array_input(
            ColumnHelper::unpack_and_duplicate_const_column(columns[1]->size(), columns[1]).get());
    if (calendar_array_data.null_elements != nullptr) {
        return Status::InvalidArgument("Calendar array can not contain null values.");
    }
    DCHECK(calendar_array_data.elements->is_binary());
    DCHECK_EQ(calendar_array_data.offsets->size(), n_rows + 1);
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
        std::string calendar_string;
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        for (size_t id = start; id < end; ++id) {
            calendar_string += calendars[id].to_string();
        }
        ASSIGN_OR_RETURN(std::optional<bool> is_in,
                         timestamp_in_calendar(timestamp, calendar_string, calendar_id));
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
        const std::optional<bool> is_in = calendar.is_timestamp_in(timestamp, calendar_id);
        if (is_in.has_value()) {
            result.append(is_in.value() ? 1L : 0L);
        } else {
            result.append_null();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> CelonisTimeFunctions::in_calendar(FunctionContext* context,
                                                      const starrocks::Columns& columns) {
    return func(context, columns, in_calendar_const, in_calendar_general);
}

Status CelonisTimeFunctions::in_calendar_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    RETURN_IF_ERROR(prepare(context, scope, 3, 1));
    return Status::OK();
}

Status CelonisTimeFunctions::in_calendar_close(FunctionContext* context, FunctionContext::FunctionStateScope scope) {
    return close(context, scope);
}

Status CelonisTimeFunctions::remap_timestamps_calendar_prepare(FunctionContext* context,
                                                               FunctionContext::FunctionStateScope scope) {
    RETURN_IF_ERROR(prepare(context, scope, 4, 2));
    return Status::OK();
}

Status CelonisTimeFunctions::remap_timestamps_calendar_close(FunctionContext* context,
                                                             FunctionContext::FunctionStateScope scope) {
    return close(context, scope);
}

static StatusOr<celonis::accelerator::Calendar>
get_calendar(const Slice* const calendars, const unsigned int* const offsets, int row) {
    size_t start = offsets[row];
    size_t end = offsets[row + 1];
    std::string calendar_string;
    for (size_t i = start; i < end; ++i) {
        calendar_string += calendars[i].to_string();
    }
    celonis::accelerator::Calendar calendar_proto;
    if (!string_to_calendar(calendar_string, calendar_proto)) {
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
    const auto& calendars1 = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *calendar1_array_data.elements).get_data().data();
    const auto& calendar1_offsets = calendar1_array_data.offsets->get_data().data();

    UnnestedArrayData calendar2_array_data = prepare_array_input(columns[1].get());
    if (calendar2_array_data.null_elements != nullptr) {
        return Status::InvalidArgument("calendar2 array must not contain null values.");
    }
    DCHECK(calendar2_array_data.elements->is_binary());
    const auto& calendars2 = down_cast<const RunTimeColumnType<TYPE_VARCHAR>&>(
            *calendar2_array_data.elements).get_data().data();
    const auto& calendar2_offsets = calendar2_array_data.offsets->get_data().data();

    ColumnPtr output_column = columns[0]->clone_empty();
    output_column = NullableColumn::wrap_if_necessary(output_column);
    for (size_t row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row)) {
            output_column->append_nulls(1);
            continue;
        }
        StatusOr<celonis::accelerator::Calendar> status_or_calendar1 = get_calendar(calendars1, calendar1_offsets,
                                                                                    row);
        StatusOr<celonis::accelerator::Calendar> status_or_calendar2 = get_calendar(calendars2, calendar2_offsets,
                                                                                    row);
        if (!status_or_calendar1.ok() || !status_or_calendar2.ok()) {
            output_column->append_nulls(1);
            continue;
        }
        celonis::accelerator::Calendar calendar_proto;
        calendar_proto.mutable_intersect_calendar()->mutable_calendar1()->CopyFrom(status_or_calendar1.value());
        calendar_proto.mutable_intersect_calendar()->mutable_calendar2()->CopyFrom(status_or_calendar2.value());
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

        output_column->append_datum(array);
    }
    return output_column;
}

static StatusOr<std::optional<TimestampValue>>
add_timeunits(const TimestampValue& timestamp, const std::string& time_unit, int64_t add_value,
              const std::string& calendar_string,
              std::optional<std::string>& calendar_id) {
    if (calendar_string.empty()) {
        if (calendar_id.has_value()) {
            return Status::InvalidArgument(
                    "Calendar ID column should not be set when calendar specification is not set.");
        }
        return add_timeunits_helper(timestamp, time_unit, add_value);
    } else {
        ASSIGN_OR_RETURN(const celonis::accelerator::Calendar calendar_proto,
                         validate_and_to_proto(calendar_string));
        Calendar calendar(calendar_proto);
        return calendar.add_timeunits(timestamp, time_unit, add_value, calendar_id);
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

    void populate_filters(std::unordered_set<int64_t>& filters, size_t row, ColumnPtr column) {
        DCHECK(row < column->size());
        UnnestedArrayData array_data = prepare_array_input(column.get());
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

    std::unordered_set<int64_t> years_;
    std::unordered_set<int64_t> quarters_;
    std::unordered_set<int64_t> months_;
    std::unordered_set<int64_t> weeks_;
    std::unordered_set<int64_t> days_;
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

StatusOr<ColumnPtr> CelonisTimeFunctions::date_match_non_constant_filters([[maybe_unused]] FunctionContext *context,
                                                                          const starrocks::Columns &columns) {
    const size_t n_rows = columns[0]->size();
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    for (auto row = 0; row < n_rows; ++row) {
        if (columns[0]->is_null(row) || columns[1]->is_null(row) || columns[2]->is_null(row) ||
            columns[3]->is_null(row) || columns[4]->is_null(row) || columns[5]->is_null(row)) {
            result.append_null();
            continue;
        }
        DateFilters date_filters(row, columns[1], columns[2], columns[3], columns[4], columns[5]);
        auto timestamp = timestamp_viewer.value(row);
        result.append(date_filters.matches(timestamp) ? 1L : 0L);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> CelonisTimeFunctions::date_match_constant_filters([[maybe_unused]] FunctionContext *context,
                                                                      const starrocks::Columns &columns) {
    const size_t n_rows = columns[0]->size();
    const auto* state = reinterpret_cast<const DateMatchStateFragmentLocal*>(
            context->get_function_state(FunctionContext::FRAGMENT_LOCAL));
    ColumnBuilder<TYPE_BIGINT> result(n_rows);
    if (state->has_null_filter) {
        result.append_nulls(n_rows);
        return result.build(true);
    }
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    for (auto row = 0; row < n_rows; ++row) {
        if (timestamp_viewer.is_null(row)) {
            result.append_null();
            continue;
        }
        auto timestamp = timestamp_viewer.value(row);
        result.append(state->date_filters.matches(timestamp) ? 1L : 0L);
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

Status CelonisTimeFunctions::date_match_prepare(FunctionContext *context, FunctionContext::FunctionStateScope scope) {
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
    RETURN_IF_ERROR(prepare(context, scope, 5, 3));
    return Status::OK();
}

Status CelonisTimeFunctions::timeunits_between_calendar_close(FunctionContext* context,
                                                              FunctionContext::FunctionStateScope scope) {
    return close(context, scope);
}

StatusOr<ColumnPtr> timeunits_between_calendar_general([[maybe_unused]] FunctionContext* context,
                                                       const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 5);
    const size_t n_rows = columns[0]->size();
    ColumnViewer from_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer to_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[1]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[4]);
    UnnestedArrayData calendar_array_data = prepare_array_input(
            ColumnHelper::unpack_and_duplicate_const_column(columns[3]->size(), columns[3]).get());
    if (calendar_array_data.null_elements != nullptr) {
        return Status::InvalidArgument("Calendar array should not have null elements.");
    }
    DCHECK(calendar_array_data.elements->is_binary());
    DCHECK_EQ(calendar_array_data.offsets->size(), n_rows + 1);
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
        const std::string time_unit = time_unit_viewer.value(row).to_string();
        if (TIME_UNIT_TO_MS.find(time_unit) == TIME_UNIT_TO_MS.end()) {
            return Status::InvalidArgument(
                    "time unit must be one of DAYS/WORKDAYS/HOURS/MINUTES/SECONDS/MILLISECONDS.");
        }
        auto from_timestamp = from_timestamp_viewer.value(row);
        auto to_timestamp = to_timestamp_viewer.value(row);
        size_t start = calendar_offsets[row];
        size_t end = calendar_offsets[row + 1];
        std::string calendar_string;
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        for (size_t id = start; id < end; ++id) {
            calendar_string += calendars[id].to_string();
        }
        std::optional<Calendar> calendar = std::nullopt;
        if (!calendar_string.empty()) {
            ASSIGN_OR_RETURN(const celonis::accelerator::Calendar calendar_proto,
                             validate_and_to_proto(calendar_string, false, true));
            calendar = Calendar(calendar_proto);
        }
        ASSIGN_OR_RETURN(const std::optional<double> diff,
                         timeunits_between(from_timestamp, to_timestamp, time_unit, calendar, calendar_id));
        if (diff.has_value()) {
            result.append(diff.value());
        } else {
            result.append_null();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}


StatusOr<ColumnPtr> timeunits_between_calendar_const([[maybe_unused]] FunctionContext* context,
                                                     const starrocks::Columns& columns,
                                                     const CalendarState* calendar_state) {
    DCHECK_EQ(columns.size(), 5);
    const size_t n_rows = columns[0]->size();
    ColumnViewer from_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer to_timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[1]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[4]);
    ColumnBuilder<TYPE_DOUBLE> result(n_rows);
    const Calendar& calendar = calendar_state->calendar;
    for (size_t row = 0; row < n_rows; ++row) {
        if (from_timestamp_viewer.is_null(row) || to_timestamp_viewer.is_null(row) || time_unit_viewer.is_null(row) ||
            calendar_state->is_null) {
            result.append_null();
            continue;
        }
        const std::string time_unit = time_unit_viewer.value(row).to_string();
        RETURN_IF_ERROR(validate_time_unit(time_unit));
        const auto from_timestamp = from_timestamp_viewer.value(row);
        const auto to_timestamp = to_timestamp_viewer.value(row);
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        std::optional<double> diff;
        if (calendar_state->is_empty) {
            ASSIGN_OR_RETURN(diff,
                             timeunits_between(from_timestamp, to_timestamp, time_unit, std::nullopt, calendar_id));
        } else {
            ASSIGN_OR_RETURN(diff, timeunits_between(from_timestamp, to_timestamp, time_unit, calendar, calendar_id));
        }
        if (diff.has_value()) {
            result.append(diff.value());
        } else {
            result.append_null();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> CelonisTimeFunctions::timeunits_between_calendar(FunctionContext* context,
                                                                     const starrocks::Columns& columns) {
    return func(context, columns, timeunits_between_calendar_const, timeunits_between_calendar_general);
}

Status CelonisTimeFunctions::add_timeunits_calendar_prepare(FunctionContext* context,
                                                            FunctionContext::FunctionStateScope scope) {
    RETURN_IF_ERROR(prepare(context, scope, 5, 3));
    return Status::OK();
}

Status CelonisTimeFunctions::add_timeunits_calendar_close(FunctionContext* context,
                                                          FunctionContext::FunctionStateScope scope) {
    return close(context, scope);
}

static StatusOr<ColumnPtr> add_timeunits_calendar_general([[maybe_unused]] FunctionContext* context,
                                                          const starrocks::Columns& columns) {
    DCHECK_EQ(columns.size(), 5);
    size_t n_rows = columns[0]->size();
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer add_value_viewer = ColumnViewer<TYPE_BIGINT>(columns[1]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[4]);
    UnnestedArrayData calendar_array_data = prepare_array_input(
            ColumnHelper::unpack_and_duplicate_const_column(columns[3]->size(), columns[3]).get());
    if (calendar_array_data.null_elements != nullptr) {
        return Status::InvalidArgument("Calendar array should not have null elements.");
    }
    DCHECK(calendar_array_data.elements->is_binary());
    DCHECK_EQ(calendar_array_data.offsets->size(), n_rows + 1);
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
        RETURN_IF_ERROR(validate_time_unit(time_unit));
        const auto timestamp = timestamp_viewer.value(row);
        auto add_value = add_value_viewer.value(row);
        size_t start = calendar_offsets[row];
        size_t end = calendar_offsets[row + 1];
        std::string calendar_string;
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        for (size_t id = start; id < end; ++id) {
            calendar_string += calendars[id].to_string();
        }
        ASSIGN_OR_RETURN(const std::optional<TimestampValue> new_timestamp,
                         add_timeunits(timestamp, time_unit, add_value, calendar_string, calendar_id));
        if (new_timestamp.has_value()) {
            result.append(new_timestamp.value());
        } else {
            result.append_null();
        }
    }
    return result.build(ColumnHelper::is_all_const(columns));
}

static StatusOr<ColumnPtr> add_timeunits_calendar_const([[maybe_unused]] FunctionContext* context,
                                                        const starrocks::Columns& columns,
                                                        const CalendarState* calendar_state) {
    DCHECK_EQ(columns.size(), 5);
    size_t n_rows = columns[0]->size();
    ColumnViewer timestamp_viewer = ColumnViewer<TYPE_DATETIME>(columns[0]);
    ColumnViewer add_value_viewer = ColumnViewer<TYPE_BIGINT>(columns[1]);
    ColumnViewer time_unit_viewer = ColumnViewer<TYPE_VARCHAR>(columns[2]);
    ColumnViewer calendar_id_viewer = ColumnViewer<TYPE_VARCHAR>(columns[4]);
    ColumnBuilder<TYPE_DATETIME> result(n_rows);
    const Calendar& calendar = calendar_state->calendar;
    for (size_t row = 0; row < n_rows; ++row) {
        if (timestamp_viewer.is_null(row) || add_value_viewer.is_null(row) || time_unit_viewer.is_null(row) ||
            calendar_state->is_null) {
            result.append_null();
            continue;
        }
        std::string time_unit = time_unit_viewer.value(row).to_string();
        RETURN_IF_ERROR(validate_time_unit(time_unit));
        const auto timestamp = timestamp_viewer.value(row);
        auto add_value = add_value_viewer.value(row);
        std::optional<std::string> calendar_id = std::nullopt;
        if (!calendar_id_viewer.is_null(row)) {
            calendar_id = calendar_id_viewer.value(row).to_string();
        }
        if (calendar_state->is_empty) {
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
    return result.build(ColumnHelper::is_all_const(columns));
}

StatusOr<ColumnPtr> CelonisTimeFunctions::add_timeunits_calendar([[maybe_unused]] FunctionContext* context,
                                                                 const starrocks::Columns& columns) {
    return func(context, columns, add_timeunits_calendar_const, add_timeunits_calendar_general);
}

} // namespace starrocks
