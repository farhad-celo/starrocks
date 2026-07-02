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

import com.google.common.annotations.VisibleForTesting;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Properties;
import java.util.function.DoublePredicate;
import java.util.function.Function;
import java.util.function.LongPredicate;

/**
 * Sizing knobs for the process-wide {@link UnityDeltaLakeMetaCache} shared by Unity-backed Delta
 * catalogs.
 *
 * <p>Values are loaded once at FE start from {@code $STARROCKS_HOME/conf/unity-cache.properties}.
 * The file is optional: every key falls back to a compile-time default mirroring the long-standing
 * per-catalog {@code DeltaLakeCatalogProperties} defaults. A malformed individual value, or a
 * well-formed value outside the supported range (TTLs must be non-negative; memory-usage ratios
 * must be a finite value in {@code (0, 1]}), logs a single WARN and falls back to the default for
 * that key only; remaining keys are parsed normally. This guards against Guava's
 * {@code expireAfterWrite}/{@code maximumWeight} throwing at cache construction and breaking every
 * Unity catalog on a single bad config value.</p>
 *
 * <p>Configuration is process-wide by design because the cache itself is. Per-catalog DDL no
 * longer carries these knobs for Unity-backed catalogs; the shared cache is sized once at the
 * first {@link UnityDeltaLakeMetaCache#getSharedInstance()} call and not resized at runtime. To
 * change sizing, edit the properties file and restart the FE.</p>
 */
public final class UnityDeltaLakeMetaCacheConfig {
    private static final Logger LOG = LogManager.getLogger(UnityDeltaLakeMetaCacheConfig.class);

    @VisibleForTesting
    static final String CONFIG_FILE_NAME = "unity-cache.properties";
    @VisibleForTesting
    static final String STARROCKS_HOME_ENV = "STARROCKS_HOME";

    public static final String KEY_JSON_TTL_SEC = "unity.delta.json.meta.cache.ttl-sec";
    public static final String KEY_JSON_MEMORY_USAGE_RATIO = "unity.delta.json.meta.cache.memory-usage-ratio";
    public static final String KEY_CHECKPOINT_TTL_SEC = "unity.delta.checkpoint.meta.cache.ttl-sec";
    public static final String KEY_CHECKPOINT_MEMORY_USAGE_RATIO =
            "unity.delta.checkpoint.meta.cache.memory-usage-ratio";

    // Defaults mirror DeltaLakeCatalogProperties so out-of-the-box behavior is identical to the
    // previous per-catalog wiring.
    static final long DEFAULT_JSON_TTL_SEC = 24L * 60L * 60L;
    static final double DEFAULT_JSON_MEMORY_USAGE_RATIO = 0.1;
    static final long DEFAULT_CHECKPOINT_TTL_SEC = 24L * 60L * 60L;
    static final double DEFAULT_CHECKPOINT_MEMORY_USAGE_RATIO = 0.1;

    private final long jsonTtlSec;
    private final double jsonMemoryUsageRatio;
    private final long checkpointTtlSec;
    private final double checkpointMemoryUsageRatio;

    private UnityDeltaLakeMetaCacheConfig(long jsonTtlSec,
                                          double jsonMemoryUsageRatio,
                                          long checkpointTtlSec,
                                          double checkpointMemoryUsageRatio) {
        this.jsonTtlSec = jsonTtlSec;
        this.jsonMemoryUsageRatio = jsonMemoryUsageRatio;
        this.checkpointTtlSec = checkpointTtlSec;
        this.checkpointMemoryUsageRatio = checkpointMemoryUsageRatio;
    }

    public long getJsonTtlSec() {
        return jsonTtlSec;
    }

    public double getJsonMemoryUsageRatio() {
        return jsonMemoryUsageRatio;
    }

    public long getCheckpointTtlSec() {
        return checkpointTtlSec;
    }

    public double getCheckpointMemoryUsageRatio() {
        return checkpointMemoryUsageRatio;
    }

    /**
     * Production entry point. Resolves the config file under {@code $STARROCKS_HOME/conf/} via
     * {@link System#getenv(String)}.
     */
    public static UnityDeltaLakeMetaCacheConfig load() {
        return loadFrom(System::getenv);
    }

    /**
     * Returns a config populated entirely from compile-time defaults. Useful in tests where the
     * properties file is irrelevant.
     */
    public static UnityDeltaLakeMetaCacheConfig defaults() {
        return new UnityDeltaLakeMetaCacheConfig(
                DEFAULT_JSON_TTL_SEC,
                DEFAULT_JSON_MEMORY_USAGE_RATIO,
                DEFAULT_CHECKPOINT_TTL_SEC,
                DEFAULT_CHECKPOINT_MEMORY_USAGE_RATIO);
    }

    /**
     * Returns a config with explicit overrides for all four knobs. Test-only - production code
     * should not bypass the properties file.
     */
    @VisibleForTesting
    public static UnityDeltaLakeMetaCacheConfig withOverrides(long jsonTtlSec,
                                                              double jsonMemoryUsageRatio,
                                                              long checkpointTtlSec,
                                                              double checkpointMemoryUsageRatio) {
        return new UnityDeltaLakeMetaCacheConfig(jsonTtlSec, jsonMemoryUsageRatio,
                checkpointTtlSec, checkpointMemoryUsageRatio);
    }

    /**
     * Test-visible loader that takes an env-lookup function so unit tests can point the loader at
     * a temp dir without mutating the real process environment.
     */
    @VisibleForTesting
    static UnityDeltaLakeMetaCacheConfig loadFrom(Function<String, String> envLookup) {
        Properties properties = readPropertiesFile(envLookup);
        if (properties == null) {
            return defaults();
        }
        return new UnityDeltaLakeMetaCacheConfig(
                parseTtlSec(properties, KEY_JSON_TTL_SEC, DEFAULT_JSON_TTL_SEC),
                parseMemoryUsageRatio(properties, KEY_JSON_MEMORY_USAGE_RATIO,
                        DEFAULT_JSON_MEMORY_USAGE_RATIO),
                parseTtlSec(properties, KEY_CHECKPOINT_TTL_SEC, DEFAULT_CHECKPOINT_TTL_SEC),
                parseMemoryUsageRatio(properties, KEY_CHECKPOINT_MEMORY_USAGE_RATIO,
                        DEFAULT_CHECKPOINT_MEMORY_USAGE_RATIO));
    }

    private static long parseTtlSec(Properties properties, String key, long defaultValue) {
        return parseLong(properties, key, defaultValue,
                v -> v >= 0L,
                "a non-negative number of seconds");
    }

    private static double parseMemoryUsageRatio(Properties properties, String key,
                                                double defaultValue) {
        return parseDouble(properties, key, defaultValue,
                v -> Double.isFinite(v) && v > 0.0 && v <= 1.0,
                "a finite value in (0, 1]");
    }

    private static Properties readPropertiesFile(Function<String, String> envLookup) {
        String starrocksHome = envLookup.apply(STARROCKS_HOME_ENV);
        if (starrocksHome == null || starrocksHome.isEmpty()) {
            return null;
        }
        Path configPath = Paths.get(starrocksHome, "conf", CONFIG_FILE_NAME);
        if (!Files.isReadable(configPath)) {
            return null;
        }
        Properties properties = new Properties();
        try (InputStream in = Files.newInputStream(configPath)) {
            properties.load(in);
        } catch (IOException e) {
            LOG.warn("Failed to read {}; falling back to default Unity Delta meta cache sizing.",
                    configPath, e);
            return null;
        }
        LOG.info("Loaded Unity Delta meta cache sizing from {}", configPath);
        return properties;
    }

    private static long parseLong(Properties properties, String key, long defaultValue,
                                  LongPredicate validator, String validRangeDescription) {
        String raw = properties.getProperty(key);
        if (raw == null) {
            return defaultValue;
        }
        long parsed;
        try {
            parsed = Long.parseLong(raw.trim());
        } catch (NumberFormatException e) {
            LOG.warn("Ignoring malformed long value for {} in unity-cache.properties: '{}'. "
                    + "Falling back to default {}.", key, raw, defaultValue);
            return defaultValue;
        }
        if (!validator.test(parsed)) {
            LOG.warn("Ignoring out-of-range value for {} in unity-cache.properties: {} "
                    + "(expected {}). Falling back to default {}.",
                    key, parsed, validRangeDescription, defaultValue);
            return defaultValue;
        }
        return parsed;
    }

    private static double parseDouble(Properties properties, String key, double defaultValue,
                                      DoublePredicate validator, String validRangeDescription) {
        String raw = properties.getProperty(key);
        if (raw == null) {
            return defaultValue;
        }
        double parsed;
        try {
            parsed = Double.parseDouble(raw.trim());
        } catch (NumberFormatException e) {
            LOG.warn("Ignoring malformed double value for {} in unity-cache.properties: '{}'. "
                    + "Falling back to default {}.", key, raw, defaultValue);
            return defaultValue;
        }
        if (!validator.test(parsed)) {
            LOG.warn("Ignoring out-of-range value for {} in unity-cache.properties: {} "
                    + "(expected {}). Falling back to default {}.",
                    key, parsed, validRangeDescription, defaultValue);
            return defaultValue;
        }
        return parsed;
    }
}
