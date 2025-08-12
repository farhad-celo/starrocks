#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisMultiIn {
public:
    /**
     * @param: [input_struct, struct_of_arrays]
     * @paramType columns: [ANY_STRUCT, ANY_STRUCT]
     * @return: BOOLEAN
     */
    DEFINE_VECTORIZED_FN(multi_in);
};

} // namespace starrocks