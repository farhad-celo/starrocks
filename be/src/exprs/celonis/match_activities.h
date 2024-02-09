#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisMatchActivitiesFunctions {
public:

    /**
     * @param: [activity_list, STARTING, NODE, ENDING, EXCLUDING, EXCLUDING_ALL, NODES_ANY]
     * @paramType: [ARRAY_VARCHAR, ARRAY_VARCHAR, ARRAY_VARCHAR, ARRAY_VARCHAR, ARRAY_VARCHAR, ARRAY_VARCHAR, ARRAY_VARCHAR]
     * @return: BIGINT
     * Implements PQL MATCH_ACTIVITIES: https://celonis-confluence.atlassian.net/wiki/spaces/PQLdevelopment/pages/11245607/MATCH+ACTIVITIES
     */
    DEFINE_VECTORIZED_FN(celonis_match_activities);
};

} // namespace starrocks
