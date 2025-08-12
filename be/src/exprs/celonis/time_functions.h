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
};

} // namespace starrocks
