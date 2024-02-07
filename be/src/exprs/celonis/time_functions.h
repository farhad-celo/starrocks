#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisTimeFunctions {
public:
    DEFINE_VECTORIZED_FN(timestamp_millis);

    /**
     * @param: [timestamp, time_unit, calendar_specification, calendar_id_column]
     * @paramType: [DATETIME, VARCHAR, ARRAY_VARCHAR, VARCHAR]
     * @return: BIGINT
     * Implements PQL REMAP_TIMESTAMPS https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11245429/REMAP+TIMESTAMPS
     */
    DEFINE_VECTORIZED_FN(remap_timestamps_calendar);

    /**
     * @param: [from_timestamp, to_timestamp, time_unit, calendar_specification, calendar_id_column]
     * @paramType: [DATETIME, DATETIME, VARCHAR, ARRAY_VARCHAR, VARCHAR]
     * @return: DOUBLE
     */
    DEFINE_VECTORIZED_FN(timeunits_between_calendar);

    /**
     * @param: [timestamp, calendar_specification, calendar_id_column]
     * @paramType: [DATETIME, ARRAY_VARCHAR, VARCHAR]
     * @return: BIGINT
     * Implements PQL IN_CALENDAR https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11248959/IN+CALENDAR
     */
    DEFINE_VECTORIZED_FN(in_calendar);

    /**
     * @param: [calendar1, calendar2]
     * @paramType: [ARRAY_VARCHAR, ARRAY_VARCHAR]
     * @return: ARRAY_VARCHAR
     */
    DEFINE_VECTORIZED_FN(make_intersect_calendar);

    static Status in_calendar_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status in_calendar_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);


    static Status remap_timestamps_calendar_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status remap_timestamps_calendar_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);
};

} // namespace starrocks
