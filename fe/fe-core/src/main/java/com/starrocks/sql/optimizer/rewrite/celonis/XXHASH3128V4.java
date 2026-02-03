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

import net.openhft.hashing.LongTupleHashFunction;

import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

import static com.starrocks.sql.optimizer.rewrite.celonis.XXHASH3128V3.SCALAR_NULL_INPUT;

public class XXHASH3128V4 implements CelonisHashFunction {
    private static final long SEED = 0;

    public BigInteger compute(String input) {
        byte[] inputBytes = input.getBytes(StandardCharsets.UTF_8);
        ByteBuffer buffer = ByteBuffer.allocate(inputBytes.length) //
                    .order(ByteOrder.LITTLE_ENDIAN) //
                    .put(inputBytes);

        long[] hash = LongTupleHashFunction.xx128(SEED).hashBytes(buffer.array());

        ByteBuffer outputBuffer = ByteBuffer.allocate(16) //
                        .putLong(hash[1]) //
                        .putLong(hash[0]);

        return new BigInteger(outputBuffer.array());
    }

    public BigInteger computeNull() {
        return compute(SCALAR_NULL_INPUT);
    }
}
