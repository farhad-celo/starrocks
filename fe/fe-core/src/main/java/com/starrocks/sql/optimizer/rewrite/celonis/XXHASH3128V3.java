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

public class XXHASH3128V3 implements CelonisHashFunction {
    public static final String SCALAR_NULL_INPUT = "_$CeL0nIs_ReSeRvEd_NuLl_";
    private static final long SEED = 0;

    public BigInteger compute(String input) {
        byte[] inputBytes = input.getBytes(StandardCharsets.UTF_8);
        final var suffix = "_" + inputBytes.length + "_";
        byte[] suffixBytes = suffix.getBytes(StandardCharsets.UTF_8);

        ByteBuffer inputAndSuffixBytes = ByteBuffer.allocate(inputBytes.length + suffixBytes.length) //
                .order(ByteOrder.LITTLE_ENDIAN) //
                .put(inputBytes) //
                .put(suffixBytes);

        long[] hash = LongTupleHashFunction.xx128(SEED).hashBytes(inputAndSuffixBytes.array());

        ByteBuffer outputBuffer = ByteBuffer.allocate(16) //
                        .order(ByteOrder.BIG_ENDIAN) //
                        .putLong(hash[1]) //
                        .putLong(hash[0]);

        return new BigInteger(outputBuffer.array());
    }

    public BigInteger computeNull() {
        return compute(SCALAR_NULL_INPUT);
    }
}
