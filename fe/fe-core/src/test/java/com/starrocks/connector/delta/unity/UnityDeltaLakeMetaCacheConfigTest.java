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

package com.starrocks.connector.delta.unity;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.function.Function;

public class UnityDeltaLakeMetaCacheConfigTest {

    @Test
    public void testDefaultsWhenStarRocksHomeUnset() {
        UnityDeltaLakeMetaCacheConfig config =
                UnityDeltaLakeMetaCacheConfig.loadFrom(name -> null);

        assertDefaults(config);
    }

    @Test
    public void testDefaultsWhenFileAbsent(@TempDir Path tempStarRocksHome) throws IOException {
        Files.createDirectories(tempStarRocksHome.resolve("conf"));

        UnityDeltaLakeMetaCacheConfig config =
                UnityDeltaLakeMetaCacheConfig.loadFrom(envLookup(tempStarRocksHome));

        assertDefaults(config);
    }

    @Test
    public void testParsesAllKeysWhenFilePresent(@TempDir Path tempStarRocksHome) throws IOException {
        writePropertiesFile(tempStarRocksHome, ""
                + UnityDeltaLakeMetaCacheConfig.KEY_JSON_TTL_SEC + "=3600\n"
                + UnityDeltaLakeMetaCacheConfig.KEY_JSON_MEMORY_USAGE_RATIO + "=0.05\n"
                + UnityDeltaLakeMetaCacheConfig.KEY_CHECKPOINT_TTL_SEC + "=7200\n"
                + UnityDeltaLakeMetaCacheConfig.KEY_CHECKPOINT_MEMORY_USAGE_RATIO + "=0.2\n");

        UnityDeltaLakeMetaCacheConfig config =
                UnityDeltaLakeMetaCacheConfig.loadFrom(envLookup(tempStarRocksHome));

        Assertions.assertEquals(3600L, config.getJsonTtlSec());
        Assertions.assertEquals(0.05, config.getJsonMemoryUsageRatio());
        Assertions.assertEquals(7200L, config.getCheckpointTtlSec());
        Assertions.assertEquals(0.2, config.getCheckpointMemoryUsageRatio());
    }

    @Test
    public void testMissingKeysFallBackToDefaults(@TempDir Path tempStarRocksHome) throws IOException {
        writePropertiesFile(tempStarRocksHome,
                UnityDeltaLakeMetaCacheConfig.KEY_JSON_TTL_SEC + "=3600\n");

        UnityDeltaLakeMetaCacheConfig config =
                UnityDeltaLakeMetaCacheConfig.loadFrom(envLookup(tempStarRocksHome));

        Assertions.assertEquals(3600L, config.getJsonTtlSec(),
                "explicit json TTL must be honored");
        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_JSON_MEMORY_USAGE_RATIO,
                config.getJsonMemoryUsageRatio(),
                "missing json memory ratio must fall back to default");
        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_CHECKPOINT_TTL_SEC,
                config.getCheckpointTtlSec(),
                "missing checkpoint TTL must fall back to default");
        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_CHECKPOINT_MEMORY_USAGE_RATIO,
                config.getCheckpointMemoryUsageRatio(),
                "missing checkpoint memory ratio must fall back to default");
    }

    @Test
    public void testMalformedValueFallsBackForThatKeyOnly(@TempDir Path tempStarRocksHome) throws IOException {
        writePropertiesFile(tempStarRocksHome, ""
                + UnityDeltaLakeMetaCacheConfig.KEY_JSON_TTL_SEC + "=not-a-number\n"
                + UnityDeltaLakeMetaCacheConfig.KEY_CHECKPOINT_MEMORY_USAGE_RATIO + "=0.42\n");

        UnityDeltaLakeMetaCacheConfig config =
                UnityDeltaLakeMetaCacheConfig.loadFrom(envLookup(tempStarRocksHome));

        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_JSON_TTL_SEC,
                config.getJsonTtlSec(),
                "malformed json TTL must fall back to default for that key");
        Assertions.assertEquals(0.42, config.getCheckpointMemoryUsageRatio(),
                "well-formed sibling key must still be parsed");
    }

    @Test
    public void testWhitespaceAroundValuesIsTrimmed(@TempDir Path tempStarRocksHome) throws IOException {
        writePropertiesFile(tempStarRocksHome,
                UnityDeltaLakeMetaCacheConfig.KEY_JSON_TTL_SEC + "=   1234   \n");

        UnityDeltaLakeMetaCacheConfig config =
                UnityDeltaLakeMetaCacheConfig.loadFrom(envLookup(tempStarRocksHome));

        Assertions.assertEquals(1234L, config.getJsonTtlSec());
    }

    @Test
    public void testNegativeTtlFallsBackToDefault(@TempDir Path tempStarRocksHome) throws IOException {
        writePropertiesFile(tempStarRocksHome, ""
                + UnityDeltaLakeMetaCacheConfig.KEY_JSON_TTL_SEC + "=-1\n"
                + UnityDeltaLakeMetaCacheConfig.KEY_CHECKPOINT_TTL_SEC + "=-99999\n");

        UnityDeltaLakeMetaCacheConfig config =
                UnityDeltaLakeMetaCacheConfig.loadFrom(envLookup(tempStarRocksHome));

        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_JSON_TTL_SEC,
                config.getJsonTtlSec(),
                "negative json TTL must fall back to default to avoid Guava IAE");
        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_CHECKPOINT_TTL_SEC,
                config.getCheckpointTtlSec(),
                "negative checkpoint TTL must fall back to default to avoid Guava IAE");
    }

    @Test
    public void testZeroTtlIsAccepted(@TempDir Path tempStarRocksHome) throws IOException {
        // Guava expireAfterWrite(0) is legal (entries expire immediately); the loader must
        // not reject it. Operators are expected to use `unity.catalog.delta-cache.enabled=false`
        // to disable the cache, but a 0 TTL should not poison the singleton.
        writePropertiesFile(tempStarRocksHome,
                UnityDeltaLakeMetaCacheConfig.KEY_JSON_TTL_SEC + "=0\n");

        UnityDeltaLakeMetaCacheConfig config =
                UnityDeltaLakeMetaCacheConfig.loadFrom(envLookup(tempStarRocksHome));

        Assertions.assertEquals(0L, config.getJsonTtlSec());
    }

    @Test
    public void testNonPositiveRatioFallsBackToDefault(@TempDir Path tempStarRocksHome) throws IOException {
        writePropertiesFile(tempStarRocksHome, ""
                + UnityDeltaLakeMetaCacheConfig.KEY_JSON_MEMORY_USAGE_RATIO + "=0\n"
                + UnityDeltaLakeMetaCacheConfig.KEY_CHECKPOINT_MEMORY_USAGE_RATIO + "=-0.1\n");

        UnityDeltaLakeMetaCacheConfig config =
                UnityDeltaLakeMetaCacheConfig.loadFrom(envLookup(tempStarRocksHome));

        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_JSON_MEMORY_USAGE_RATIO,
                config.getJsonMemoryUsageRatio(),
                "ratio=0 must fall back: a zero-weight cache evicts every insert");
        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_CHECKPOINT_MEMORY_USAGE_RATIO,
                config.getCheckpointMemoryUsageRatio(),
                "negative ratio must fall back: Guava maximumWeight rejects negatives");
    }

    @Test
    public void testRatioAboveOneFallsBackToDefault(@TempDir Path tempStarRocksHome) throws IOException {
        writePropertiesFile(tempStarRocksHome,
                UnityDeltaLakeMetaCacheConfig.KEY_JSON_MEMORY_USAGE_RATIO + "=1.5\n");

        UnityDeltaLakeMetaCacheConfig config =
                UnityDeltaLakeMetaCacheConfig.loadFrom(envLookup(tempStarRocksHome));

        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_JSON_MEMORY_USAGE_RATIO,
                config.getJsonMemoryUsageRatio(),
                "ratio>1 would oversubscribe heap; must fall back to default");
    }

    @Test
    public void testRatioAtOneIsAccepted(@TempDir Path tempStarRocksHome) throws IOException {
        // 1.0 is the upper boundary - the entire heap is reserved for the cache. Unusual but
        // legal; operators may want this on a dedicated FE host.
        writePropertiesFile(tempStarRocksHome,
                UnityDeltaLakeMetaCacheConfig.KEY_JSON_MEMORY_USAGE_RATIO + "=1.0\n");

        UnityDeltaLakeMetaCacheConfig config =
                UnityDeltaLakeMetaCacheConfig.loadFrom(envLookup(tempStarRocksHome));

        Assertions.assertEquals(1.0, config.getJsonMemoryUsageRatio());
    }

    @Test
    public void testNonFiniteRatioFallsBackToDefault(@TempDir Path tempStarRocksHome) throws IOException {
        writePropertiesFile(tempStarRocksHome, ""
                + UnityDeltaLakeMetaCacheConfig.KEY_JSON_MEMORY_USAGE_RATIO + "=NaN\n"
                + UnityDeltaLakeMetaCacheConfig.KEY_CHECKPOINT_MEMORY_USAGE_RATIO + "=Infinity\n");

        UnityDeltaLakeMetaCacheConfig config =
                UnityDeltaLakeMetaCacheConfig.loadFrom(envLookup(tempStarRocksHome));

        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_JSON_MEMORY_USAGE_RATIO,
                config.getJsonMemoryUsageRatio(),
                "NaN must fall back: comparisons against NaN are always false");
        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_CHECKPOINT_MEMORY_USAGE_RATIO,
                config.getCheckpointMemoryUsageRatio(),
                "Infinity must fall back: cache weight cannot be unbounded");
    }

    @Test
    public void testOutOfRangeKeyDoesNotPoisonSiblingKey(@TempDir Path tempStarRocksHome) throws IOException {
        writePropertiesFile(tempStarRocksHome, ""
                + UnityDeltaLakeMetaCacheConfig.KEY_JSON_TTL_SEC + "=-5\n"
                + UnityDeltaLakeMetaCacheConfig.KEY_CHECKPOINT_TTL_SEC + "=42\n");

        UnityDeltaLakeMetaCacheConfig config =
                UnityDeltaLakeMetaCacheConfig.loadFrom(envLookup(tempStarRocksHome));

        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_JSON_TTL_SEC,
                config.getJsonTtlSec(),
                "out-of-range json TTL must fall back");
        Assertions.assertEquals(42L, config.getCheckpointTtlSec(),
                "well-formed sibling key must still be parsed");
    }

    private static void writePropertiesFile(Path starRocksHome, String contents) throws IOException {
        Path confDir = Files.createDirectories(starRocksHome.resolve("conf"));
        Files.writeString(confDir.resolve(UnityDeltaLakeMetaCacheConfig.CONFIG_FILE_NAME), contents);
    }

    private static Function<String, String> envLookup(Path starRocksHome) {
        return name -> UnityDeltaLakeMetaCacheConfig.STARROCKS_HOME_ENV.equals(name)
                ? starRocksHome.toString()
                : null;
    }

    private static void assertDefaults(UnityDeltaLakeMetaCacheConfig config) {
        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_JSON_TTL_SEC,
                config.getJsonTtlSec());
        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_JSON_MEMORY_USAGE_RATIO,
                config.getJsonMemoryUsageRatio());
        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_CHECKPOINT_TTL_SEC,
                config.getCheckpointTtlSec());
        Assertions.assertEquals(UnityDeltaLakeMetaCacheConfig.DEFAULT_CHECKPOINT_MEMORY_USAGE_RATIO,
                config.getCheckpointMemoryUsageRatio());
    }
}
