#pragma once

#include "runtime/types.h"
#include "types/logical_type.h"

namespace starrocks::celonis {
// Creates an array type from the given 'element_type'
TypeDescriptor array_type(const LogicalType& element_type);
} // namespace starrocks::celonis
