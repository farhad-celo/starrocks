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

public class XXHASH3128V4Test {
    @Test
    public void testEqualityToNativeFunction() {
        final var hasher = new XXHASH3128V4();
        assertEquals("-115062932358609980327512201381724926433", hasher.compute("a").toString());
        assertEquals("-43548290287701266719030436004545105576", hasher.compute("1").toString());
        assertEquals("-165901613936319106251328792755907166665", hasher.compute("fWn!ɔԕڈ#ÅLm{FΟ̉WʊHUɌIɹ̬©;ޤݦм۸ˢoӏ0").toString());
        assertEquals("140510453822038601413216693103982955033", hasher.computeNull().toString());
    }
}
