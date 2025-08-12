#pragma once

#include "exprs/celonis/util.h"
#include "exprs/function_helper.h"

namespace starrocks {

class CelonisSourceTargetFunctions {
public:
    // Edge configurations for SOURCE/TARGET functions.
    // TODO(j.kim): expand the list.
    enum EdgeConfig {DEFAULT, ANY_TO_ANY};

    /**
     * @param: [input_array, edge_configuration, (group_array)]
     * @paramType columns: [ARRAY of INT | DATETIME | BIGINT | VARCHAR, VARCHAR, (ARRAY of BIGINT)]
     * @return: input_array type
     * Supports PQL SOURCE and TARGET https://confluence.celonis.com/display/PQLdevelopment/SOURCE+-+TARGET
     * Only "any->any" edge_configuration is supported.
     */
    DEFINE_VECTORIZED_FN(celonis_array_sources);
    DEFINE_VECTORIZED_FN(celonis_array_targets);

    static Status celonis_array_sources_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status celonis_array_targets_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status celonis_array_sources_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status celonis_array_targets_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    // Constructs a column with results of CELONIS_ARRAY_SOURCES and CELONIS_ARRAY_TARGETS.
    template<bool is_source, bool has_group, bool has_null_element, bool has_null_group_element>
    static ColumnPtr _celonis_array_sources_targets_impl(const UnnestedArrayData& array_data,
                                                         const UnnestedArrayData& group_array_data);
    template<bool is_source>
    static ColumnPtr _celonis_array_sources_targets_impl(const UnnestedArrayData& array_data,
                                                         const UnnestedArrayData& group_array_data);
};

} // namespace starrocks
