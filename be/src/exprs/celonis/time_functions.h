#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

struct TimeRange {
    int64_t begin_ms{};
    int64_t end_ms{};
    bool is_weekly{};

    /** Computes the intersection with [left_ms, right_ms) */
    [[nodiscard]] std::vector<TimeRange> intersect(int64_t left_ms, int64_t right_ms) const;

    /** Computes the overlap in milliseconds with [left_ms, right_ms) */
    [[nodiscard]] int64_t compute_overlap(int64_t left_ms, int64_t right_ms) const;

    [[nodiscard]] bool is_ms_in(int64_t ms) const;
};

class CelonisTimeFunctions {
public:
    /**
     * @param: [timestamp]
     * @paramType: [DATETIME]
     * @return: BIGINT
     * Returns milliseconds since epoch of the timestamp
     */
    DEFINE_VECTORIZED_FN(millis_timestamp);

    DEFINE_VECTORIZED_FN(timestamp_millis);

    DEFINE_VECTORIZED_FN(timestamp_to_millis_precision);

    /**
     * @param: [entry_index, calendar_specification, calendar_id_column]
     * @paramType: [INT, ARRAY_VARCHAR, VARCHAR]
     * @return: BIGINT
     * Calendar sorts (based on begin time) and merges the time ranges. This function returns the begin mills of the
     * entry_index-th time range. NULL is returned when calendar_id does not exist or entry_index is invalid.
     * This function returns Status::NotSupported when non-const calendar column is given.
     */
    DEFINE_VECTORIZED_FN(get_calendar_entry_start);

    /**
     * @param: [timestamp, time_unit, calendar_specification, calendar_id_column]
     * @paramType: [DATETIME, VARCHAR, ARRAY_VARCHAR, VARCHAR]
     * @return: BIGINT
     * Implements PQL REMAP_TIMESTAMPS: https://docs.celonis.com/en/remap_timestamps.html
     */
    DEFINE_VECTORIZED_FN(remap_timestamps_calendar);

    /**
     * @param: [from_timestamp, to_timestamp, time_unit, calendar_specification, calendar_id_column]
     * @paramType: [DATETIME, DATETIME, VARCHAR, ARRAY_VARCHAR, VARCHAR]
     * @return: DOUBLE
     */
    DEFINE_VECTORIZED_FN(timeunits_between_calendar);

    /**
     * @param: [first_date, second_date, third_date]
     * @paramType: [DATETIME, DATETIME, DATETIME]
     * @return: BIGINT
     * This function returns 1 if first_date is in range [second_date, third_date), and 0 otherwise.
     * Implements PQL DATE_BETWEEN: https://docs.celonis.com/en/date_between.html
     */
    DEFINE_VECTORIZED_FN(date_between);

    /**
     * @param: [timestamp, calendar_specification, calendar_id_column]
     * @paramType: [DATETIME, ARRAY_VARCHAR, VARCHAR]
     * @return: BIGINT
     * Implements PQL IN_CALENDAR: https://docs.celonis.com/en/in_calendar.html
     */
    DEFINE_VECTORIZED_FN(in_calendar);

    /**
     * @param: [calendar1, calendar2]
     * @paramType: [ARRAY_VARCHAR, ARRAY_VARCHAR]
     * @return: ARRAY_VARCHAR
     */
    DEFINE_VECTORIZED_FN(make_intersect_calendar);

    /**
     * @param: [timestamp, add_value, time_unit, calendar_specification, calendar_id_column]
     * @paramType: [DATETIME, BIGINT, VARCHAR, ARRAY_VARCHAR, VARCHAR]
     * @return: DATETIME
     */
    DEFINE_VECTORIZED_FN(add_timeunits_calendar);

    /**
     * @param: [timestamp, years, quarters, months, weeks, days]
     * @paramType: [DATETIME, ARRAY_BIGINT, ARRAY_BIGINT, ARRAY_BIGINT, ARRAY_BIGINT, ARRAY_BIGINT]
     * @return: BIGINT
     * Implements PQL DATE_MATCH: https://docs.celonis.com/en/date_match.html
     */
    DEFINE_VECTORIZED_FN(date_match);

    static Status get_calendar_entry_start_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status get_calendar_entry_start_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status in_calendar_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status in_calendar_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status remap_timestamps_calendar_prepare(FunctionContext* context,
                                                    FunctionContext::FunctionStateScope scope);

    static Status remap_timestamps_calendar_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status timeunits_between_calendar_prepare(FunctionContext* context,
                                                     FunctionContext::FunctionStateScope scope);

    static Status timeunits_between_calendar_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status add_timeunits_calendar_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status add_timeunits_calendar_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status date_match_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status date_match_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status make_intersect_calendar_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status make_intersect_calendar_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(date_match_constant_filters);
    DEFINE_VECTORIZED_FN(date_match_non_constant_filters);
    DEFINE_VECTORIZED_FN(make_intersect_calendar_const);
    DEFINE_VECTORIZED_FN(make_intersect_calendar_general);
};

} // namespace starrocks
