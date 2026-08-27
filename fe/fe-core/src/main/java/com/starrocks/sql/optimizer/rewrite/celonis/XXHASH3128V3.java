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
import java.util.ArrayList;
import java.util.List;

public class XXHASH3128V3 implements CelonisHashFunction {
    public static final String SCALAR_NULL_INPUT = "_$CeL0nIs_ReSeRvEd_NuLl_";
    private static final long SEED = 0;

    @Override
    public BigInteger compute(String input) {
        return computeInputs(input);
    }

    /**
     * Computes the V3 hash for an ordered sequence of scalar inputs. A null element is encoded using the reserved scalar
     * null marker, matching the backend implementation.
     *
     * @throws CelonisHashCalculationException if the input array is null, there are no inputs, or their encoding is too
     *         large to fit in the single buffer used for hashing
    */
    @Override
    public BigInteger compute(String... inputs) throws CelonisHashCalculationException {
        if (inputs == null) {
            throw new CelonisHashCalculationException("XXHASH3_128_V3 inputs must not be null");
        }
        if (inputs.length == 0) {
            throw new CelonisHashCalculationException("XXHASH3_128_V3 requires at least one input");
        }

        try {
            return computeInputs(inputs);
        } catch (ArithmeticException exception) {
            throw new CelonisHashCalculationException(
                    "XXHASH3_128_V3 inputs are too large to encode in a single buffer", exception);
        }
    }

    private BigInteger computeInputs(String... inputs) {
        List<byte[]> encodedParts = new ArrayList<>(inputs.length * 2);
        int encodedLength = 0;
        for (String input : inputs) {
            String encodedInput = input == null ? SCALAR_NULL_INPUT : input;
            byte[] inputBytes = encodedInput.getBytes(StandardCharsets.UTF_8);
            byte[] suffixBytes = ("_" + inputBytes.length + "_").getBytes(StandardCharsets.UTF_8);

            encodedParts.add(inputBytes);
            encodedParts.add(suffixBytes);
            encodedLength = Math.addExact(encodedLength, inputBytes.length);
            encodedLength = Math.addExact(encodedLength, suffixBytes.length);
        }

        ByteBuffer inputBytes = ByteBuffer.allocate(encodedLength).order(ByteOrder.LITTLE_ENDIAN);
        encodedParts.forEach(inputBytes::put);

        long[] hash = LongTupleHashFunction.xx128(SEED).hashBytes(inputBytes.array());

        ByteBuffer outputBuffer = ByteBuffer.allocate(16) //
                        .order(ByteOrder.BIG_ENDIAN) //
                        .putLong(hash[1]) //
                        .putLong(hash[0]);

        return new BigInteger(outputBuffer.array());
    }

    public BigInteger computeNull() {
        return computeInputs(new String[] {null});
    }
}
