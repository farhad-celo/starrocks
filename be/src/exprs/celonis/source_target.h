#pragma once

#include "exprs/celonis/util.h"
#include "exprs/function_helper.h"

namespace starrocks {

enum class SourceTargetType { SOURCE, TARGET };

// TODO (mkennecke): Support other edge configurations
enum class SourceTargetEdgeConfig { DEFAULT, ANY_TO_ANY };

template <SourceTargetType SOURCE_TARGET_TYPE>
class CelonisSourceTarget {
public:
    /**
     * @param: [input_array, edge_configuration, (group_array)]
     * @paramType columns: [ARRAY of INT | DATETIME | BIGINT | VARCHAR, VARCHAR, (ARRAY of BIGINT)]
     * @return: input_array type
     * Supports PQL SOURCE and TARGET https://confluence.celonis.com/display/PQLdevelopment/SOURCE+-+TARGET
     * Only "any->any" edge_configuration is supported.
     */
    DEFINE_VECTORIZED_FN(array_sources_targets);

    static Status array_sources_targets_prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);
    static Status array_sources_targets_close(FunctionContext* context, FunctionContext::FunctionStateScope scope);
};

} // namespace starrocks
