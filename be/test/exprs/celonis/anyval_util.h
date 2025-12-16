#pragma once

#include "exprs/expr.h"
#include "exprs/function_context.h"

namespace starrocks {

class CelonisAnyValUtil {
public:
    static FunctionContext::TypeDesc column_type_to_type_desc(const TypeDescriptor& type);
};

} // namespace starrocks
