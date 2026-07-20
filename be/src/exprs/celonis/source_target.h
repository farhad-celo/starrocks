#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisSourceTargetFunctions {
public:
    // Edge configurations for SOURCE/TARGET functions.
    // TODO(gubichev): expand the list.
    enum EdgeConfig {DEFAULT, ANY_TO_ANY};

    DEFINE_VECTORIZED_FN(celonis_array_sources);

private:
    // Constructs a column with results of CELONIS_ARRAY_SOURCES. 'elements' are input array elements (flattened) where
    // 'offsets' are used to compute the layout of the original arrays. 'null_element_offsets' are null indicators for
    // elements of the arrays, while 'null_array_offsets' are null indicators for array themselves
    template <bool has_null, EdgeConfig format>
    static ColumnPtr _celonis_array_sources_impl(FunctionContext* context, const Column& elements,
                                                 const UInt32Column& offsets,
                                                 const NullColumn::Container* null_element_offsets,
                                                 const NullColumn::Container* null_array_offsets);
};

} // namespace starrocks
