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

package com.starrocks.sql.analyzer.celonis.validate;

import com.starrocks.common.Config;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.util.Set;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class ValidateFunctionWhitelistTest {

    @AfterEach
    void resetConfig() {
        Config.validate_allowed_functions = new String[] {};
    }

    @Test
    void emptyConfigIsExactlyTheDefault() {
        Config.validate_allowed_functions = new String[] {};
        Set<String> effective = ValidateFunctionWhitelist.effectiveAllowedFunctions();
        assertEquals(ValidateFunctionWhitelist.DEFAULT_ALLOWED_FUNCTIONS, effective);
        assertTrue(effective.contains("count"));
    }

    @Test
    void defaultIncludesGeospatial() {
        Set<String> effective = ValidateFunctionWhitelist.effectiveAllowedFunctions();
        // Kept in sync with celostar FUNCTION_ALLOW_LIST — geospatial available from the start.
        assertTrue(effective.contains("st_point"), effective.toString());
        assertTrue(effective.contains("st_contains"), effective.toString());
    }

    @Test
    void configIsAdditiveOnTopOfDefault() {
        Config.validate_allowed_functions = new String[] {"My_Udf", " another_fn "};
        Set<String> effective = ValidateFunctionWhitelist.effectiveAllowedFunctions();
        // Configured names are added (trimmed + lowercased) ...
        assertTrue(effective.contains("my_udf"));
        assertTrue(effective.contains("another_fn"));
        // ... and the default baseline is still allowed (config never drops it).
        assertTrue(effective.contains("upper"));
        assertTrue(effective.contains("count"));
    }

    @Test
    void blankConfigEntriesAreIgnored() {
        Config.validate_allowed_functions = new String[] {"", "   "};
        Set<String> effective = ValidateFunctionWhitelist.effectiveAllowedFunctions();
        assertEquals(ValidateFunctionWhitelist.DEFAULT_ALLOWED_FUNCTIONS, effective);
    }
}
