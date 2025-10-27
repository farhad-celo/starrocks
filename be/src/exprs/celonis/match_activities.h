#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisMatchActivitiesFunctions {
public:
    /**
     * @param: [activity_list, STARTING, NODE, ENDING, EXCLUDING, EXCLUDING_ALL, NODES_ANY]
     * @paramType: [ARRAY_VARCHAR, ARRAY_VARCHAR, ARRAY_VARCHAR, ARRAY_VARCHAR, ARRAY_VARCHAR, ARRAY_VARCHAR, ARRAY_VARCHAR]
     * @return: BIGINT
     * Implements PQL MATCH_ACTIVITIES: https://docs.celonis.com/en/match_activities.html
     */
    DEFINE_VECTORIZED_FN(celonis_match_activities);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(celonis_match_activities_constant_config);

    DEFINE_VECTORIZED_FN(celonis_match_activities_non_constant_config);
};

} // namespace starrocks
