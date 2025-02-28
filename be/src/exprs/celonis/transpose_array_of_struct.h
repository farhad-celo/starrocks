#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisTransposeArrayOfStruct {
public:
    /**
     * @param: [input_array_of_struct]
     * @paramType columns: [ANY_ARRAY]
     * @return: ANY_STRUCT
     */
    DEFINE_VECTORIZED_FN(transpose_array_of_struct);
};

} // namespace starrocks