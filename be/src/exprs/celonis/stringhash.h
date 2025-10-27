#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisStringhash {
public:
    /**
     * @param: [input_string]
     * @paramType columns: [VARCHAR]
     * @return: VARCHAR
     * Supports PQL STRINGHASH: https://docs.celonis.com/en/stringhash.html
     */
    DEFINE_VECTORIZED_FN(stringhash);
};

} // namespace starrocks