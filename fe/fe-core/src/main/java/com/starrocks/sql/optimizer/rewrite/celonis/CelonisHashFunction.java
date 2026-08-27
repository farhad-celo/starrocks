// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.starrocks.sql.optimizer.rewrite.celonis;

import com.starrocks.catalog.FunctionSet;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;

import java.math.BigInteger;

public interface CelonisHashFunction {
    BigInteger compute(String input);
    BigInteger computeNull();

    /**
     * Computes the hash of an ordered sequence of inputs. Only implemented for the hash functions whose multi-input
     * encoding is known to match the backend.
     *
     * @throws CelonisHashCalculationException if this hash function does not support multiple inputs
     */
    default BigInteger compute(String... inputs) throws CelonisHashCalculationException {
        throw new CelonisHashCalculationException(getClass().getSimpleName() + " does not support multiple inputs");
    }

    static CelonisHashFunction of(CallOperator callOperator) {
        switch (callOperator.getFnName().toLowerCase()) {
            case FunctionSet.CELONIS_XX_HASH3_128_NULLABLE:
                return new XXHASH3128NULLABLE();
            case FunctionSet.CELONIS_XX_HASH3_128_V3:
                return new XXHASH3128V3();
            case FunctionSet.CELONIS_XX_HASH3_128_V4:
                return new XXHASH3128V4();
            default:
                return null;
        }
    }
}
