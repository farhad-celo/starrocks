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

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.cache.AbstractLoadingCache;
import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import com.google.common.cache.LoadingCache;
import com.google.common.collect.Lists;
import com.starrocks.common.Pair;
import com.starrocks.connector.delta.DeltaLakeCacheSizeEstimator;
import com.starrocks.connector.delta.DeltaLakeCatalogProperties;
import com.starrocks.connector.delta.DeltaLakeEngine;
import com.starrocks.connector.delta.DeltaLakeFileStatus;
import com.starrocks.connector.delta.DeltaLakeJsonHandler;
import com.starrocks.connector.delta.DeltaLakeParquetHandler;
import io.delta.kernel.data.ColumnarBatch;
import io.delta.kernel.types.StructType;
import org.apache.hadoop.conf.Configuration;

import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;

/**
 * Process-wide Delta metadata cache for Unity catalogs.
 *
 * <p>Entries are keyed on {@code (principalScope, file identity)} so a hit is only ever served to
 * callers within the same Unity principal boundary; a different principal misses and loads under
 * its own vended credentials. Per-table credentials reach the loader through the scoped views
 * returned by {@link #getCheckpointCache(String, Configuration)} / {@link #getJsonCache(String,
 * Configuration)}.</p>
 *
 * <p>Sized once from {@link UnityDeltaLakeMetaCacheConfig} on first use; restart the FE to
 * change TTLs or memory-usage ratios.</p>
 */
public class UnityDeltaLakeMetaCache {
    private static final int MEMORY_META_SAMPLES = 10;
    private static final UnityDeltaLakeMetaCache SHARED_INSTANCE =
            new UnityDeltaLakeMetaCache(UnityDeltaLakeMetaCacheConfig.load());

    private final Cache<ScopedKey, List<ColumnarBatch>> checkpointCache;
    private final Cache<ScopedKey, List<JsonNode>> jsonCache;

    private UnityDeltaLakeMetaCache(UnityDeltaLakeMetaCacheConfig config) {
        long checkpointCacheSize = Math.round(Runtime.getRuntime().maxMemory()
                * config.getCheckpointMemoryUsageRatio());
        long jsonCacheSize = Math.round(Runtime.getRuntime().maxMemory()
                * config.getJsonMemoryUsageRatio());

        this.checkpointCache = CacheBuilder.newBuilder()
                .expireAfterWrite(config.getCheckpointTtlSec(), TimeUnit.SECONDS)
                .weigher((ScopedKey key, List<ColumnarBatch> value) -> {
                    long structureEstimated = DeltaLakeCacheSizeEstimator.estimateCheckpointByStructure(
                            (Pair<DeltaLakeFileStatus, StructType>) key.innerKey, value);
                    return (int) Math.min(structureEstimated, Integer.MAX_VALUE);
                })
                .maximumWeight(checkpointCacheSize)
                .build();

        this.jsonCache = CacheBuilder.newBuilder()
                .expireAfterWrite(config.getJsonTtlSec(), TimeUnit.SECONDS)
                .weigher((ScopedKey key, List<JsonNode> value) -> {
                    long structureEstimated = DeltaLakeCacheSizeEstimator.estimateJsonByStructure(
                            (DeltaLakeFileStatus) key.innerKey, value);
                    return (int) Math.min(structureEstimated, Integer.MAX_VALUE);
                })
                .maximumWeight(jsonCacheSize)
                .build();
    }

    public static UnityDeltaLakeMetaCache getSharedInstance() {
        return SHARED_INSTANCE;
    }

    DeltaLakeEngine createEngine(String principalScope, Configuration hadoopConfiguration,
                                 DeltaLakeCatalogProperties properties) {
        return DeltaLakeEngine.create(hadoopConfiguration, properties,
                getCheckpointCache(principalScope, hadoopConfiguration),
                getJsonCache(principalScope, hadoopConfiguration), false);
    }

    LoadingCache<Pair<DeltaLakeFileStatus, StructType>, List<ColumnarBatch>> getCheckpointCache(
            String principalScope, Configuration hadoopConfiguration) {
        return new ScopedLoadingCache<>(checkpointCache, principalScope,
                key -> DeltaLakeParquetHandler.readParquetFile(
                        key.first.getPath(), key.second, hadoopConfiguration));
    }

    LoadingCache<DeltaLakeFileStatus, List<JsonNode>> getJsonCache(String principalScope,
                                                                   Configuration hadoopConfiguration) {
        return new ScopedLoadingCache<>(jsonCache, principalScope,
                fileStatus -> DeltaLakeJsonHandler.readJsonFile(
                        fileStatus.getPath(), hadoopConfiguration));
    }

    public Map<String, Long> estimateCount() {
        return Map.of("unityCheckpointCache", checkpointCache.size(), "unityJsonCache", jsonCache.size());
    }

    public List<Pair<List<Object>, Long>> getSamples() {
        List<Object> jsonSamples = jsonCache.asMap().values()
                .stream()
                .limit(MEMORY_META_SAMPLES)
                .collect(Collectors.toList());

        List<Object> checkpointSamples = checkpointCache.asMap().values()
                .stream()
                .limit(MEMORY_META_SAMPLES)
                .collect(Collectors.toList());

        return Lists.newArrayList(Pair.create(jsonSamples, jsonCache.size()),
                Pair.create(checkpointSamples, checkpointCache.size()));
    }

    @VisibleForTesting
    boolean containsJson(String principalScope, DeltaLakeFileStatus fileStatus) {
        return jsonCache.getIfPresent(new ScopedKey(principalScope, fileStatus)) != null;
    }

    @VisibleForTesting
    static void resetForTest() {
        SHARED_INSTANCE.checkpointCache.invalidateAll();
        SHARED_INSTANCE.jsonCache.invalidateAll();
    }

    /** Per-call loader for {@link ScopedLoadingCache}; captures the table-scoped {@link Configuration}. */
    @FunctionalInterface
    private interface ScopedLoader<K, V> {
        V load(K key) throws Exception;
    }

    /** Composite key pinning each entry to the credential scope that populated it. */
    private static final class ScopedKey {
        private final String scope;
        private final Object innerKey;

        private ScopedKey(String scope, Object innerKey) {
            this.scope = scope;
            this.innerKey = innerKey;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof ScopedKey)) {
                return false;
            }
            ScopedKey that = (ScopedKey) o;
            return Objects.equals(scope, that.scope) && Objects.equals(innerKey, that.innerKey);
        }

        @Override
        public int hashCode() {
            return Objects.hash(scope, innerKey);
        }
    }

    /**
     * Presents the process-wide {@link Cache} as a {@link LoadingCache} keyed on the plain file
     * identity the Delta engine passes, pinning every access to {@code scope} and binding the
     * per-table loader at construction time.
     */
    private static final class ScopedLoadingCache<K, V> extends AbstractLoadingCache<K, V> {
        private final Cache<ScopedKey, V> delegate;
        private final String scope;
        private final ScopedLoader<K, V> loader;

        private ScopedLoadingCache(Cache<ScopedKey, V> delegate, String scope, ScopedLoader<K, V> loader) {
            this.delegate = delegate;
            this.scope = scope;
            this.loader = loader;
        }

        @Override
        public V get(K key) throws ExecutionException {
            return delegate.get(new ScopedKey(scope, key), () -> loader.load(key));
        }

        @Override
        public V getIfPresent(Object key) {
            return delegate.getIfPresent(new ScopedKey(scope, key));
        }

        @Override
        public void put(K key, V value) {
            delegate.put(new ScopedKey(scope, key), value);
        }
    }
}
