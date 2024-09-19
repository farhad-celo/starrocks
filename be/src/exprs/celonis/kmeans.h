#pragma once

#include "exprs/function_helper.h"

namespace starrocks {

class CelonisKmeans {
public:
    /**
     * @param: [point, model]
     * @paramType columns: [ARRAY_DOUBLE, VARCHAR]
     * @return: BIGINT
     * This function computes the cluster index (the index of the closest centroid) of the point.
     * A valid model (represented by the k centroids) should be in the below format (assuming m features):
     * "x_11,x_12,...,x_1m;x_21,x_22,...,x_2m;...;x_k1,x_k2,...,x_km"
     */
    DEFINE_VECTORIZED_FN(apply_kmeans_model);

    static Status prepare(FunctionContext* context, FunctionContext::FunctionStateScope scope);

    static Status close(FunctionContext* context, FunctionContext::FunctionStateScope scope);

private:
    DEFINE_VECTORIZED_FN(apply_kmeans_constant_model);

    DEFINE_VECTORIZED_FN(apply_kmeans_non_constant_model);
};

} // namespace starrocks