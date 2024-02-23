#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisQnorm {
public:
    /**
     * @param: [input_value]
     * @paramType columns: [DOUBLE]
     * @return: DOUBLE
     * It implements PQL QNORM: https://docs.celonis.com/en/qnorm.html
     */
    DEFINE_VECTORIZED_FN(qnorm);
};

} // namespace starrocks