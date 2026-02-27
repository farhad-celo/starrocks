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

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

public class XXHASH3128NULLABLETest {
    @Test
    public void testEqualityToNativeFunction() {
        final var hasher = new XXHASH3128NULLABLE();
        assertEquals("-3064065747166489328281421148979907626", hasher.compute("a").toString());
        assertEquals("-144608462304864223298616749235096385510", hasher.compute("1").toString());
        assertEquals("-13577519804223448105674676604219862806",
                hasher.compute("fWn!ɔԕڈ#ÅLm{FΟ̉WʊHUɌIɹ̬©;ޤݦм۸ˢoӏ0").toString());
        assertNull(hasher.computeNull());
    }
}
