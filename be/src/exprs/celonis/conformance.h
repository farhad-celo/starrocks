#pragma once

#include "exprs/function_context.h"
#include "exprs/function_helper.h"

namespace starrocks {

class CelonisConformance {
public:
    /**
     * @param: [activity array, json_petri_net_spec]
     * @paramType: [ARRAY_VARCHAR, VARCHAR]
     * @return: [ARRAY_BIGINT]
     * Supports CONFORMANCE PQL in https://docs.celonis.com/en/conformance.html
     *
     * json_petri_net_spec is a json version of PetriNetDescription message in
     * https://github.com/celonis/cpm-query-engine/blob/main/query-engine/src/main/protos/operators.proto
     * See be/test/exprs/celonis/conformance_test.cpp for an example.
     *
     * While the spec allows counts larger than 1 in initial_marking and final_marking, the implementation considers all
     * counts as 1 following Saola implementation.
     */
    DEFINE_VECTORIZED_FN(conformance);
    /* Deprecated for removal: Use CelonisReadableConformance::readable_conformance instead */
    DEFINE_VECTORIZED_FN(readable_conformance);
    static Status conformance_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status conformance_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);
};

class CelonisReadableConformance {
public:
    /**
     * @param: [conformance violation values, activity array]
     * @paramType: [ARRAY_BIGINT, ARRAY_VARCHAR]
     * @return: [ARRAY_VARCHAR]
     * Supports READABLE PQL IN https://docs.celonis.com/en/conformance.html
     */
    DEFINE_VECTORIZED_FN(readable_conformance);
};

} // namespace starrocks