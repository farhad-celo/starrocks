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

import com.databricks.sdk.service.catalog.TableInfo;
import com.google.common.annotations.VisibleForTesting;
import com.starrocks.catalog.DeltaLakeTable;
import com.starrocks.common.Pair;
import com.starrocks.connector.delta.DeltaLakeCatalogProperties;
import com.starrocks.connector.delta.DeltaLakeEngine;
import com.starrocks.connector.delta.DeltaLakeMetastore;
import com.starrocks.connector.delta.DeltaLakeSnapshot;
import com.starrocks.connector.delta.DeltaUtils;
import com.starrocks.connector.metastore.MetastoreTable;
import com.starrocks.credential.CloudConfiguration;
import org.apache.hadoop.conf.Configuration;

import java.util.List;
import java.util.Map;

/**
 * {@link DeltaLakeMetastore} variant whose {@code IMetastore} delegate is a
 * {@link UnityMetastore}. Unity-specific snapshot loading uses a process-wide checkpoint/JSON
 * cache, while this class also re-attaches per-table vended cloud credentials to the
 * {@link DeltaLakeTable} that the planner sees.
 *
 * <p>Per-table caching of {@code TableInfo} and vended {@code TemporaryTableCredentials} is the
 * job of {@link CachingUnityCatalogClient}, not this class. We resolve through
 * {@link UnityMetastore} on every call and rely on the client decorator to absorb the REST
 * traffic.</p>
 */
public class UnityBackedDeltaMetastore extends DeltaLakeMetastore {
    private final UnityMetastore unityMetastore;
    private final UnityCatalogProperties unityProperties;
    private final UnityDeltaLakeMetaCache unityMetaCache;

    public UnityBackedDeltaMetastore(String catalogName,
                                     UnityMetastore delegate,
                                     Configuration hdfsConfiguration,
                                     DeltaLakeCatalogProperties deltaLakeCatalogProperties,
                                     UnityCatalogProperties unityProperties) {
        super(catalogName, delegate, hdfsConfiguration, deltaLakeCatalogProperties);
        this.unityMetastore = delegate;
        this.unityProperties = unityProperties;
        this.unityMetaCache = UnityDeltaLakeMetaCache.getSharedInstance();
    }

    @VisibleForTesting
    public UnityCatalogApi getUnityCatalogClient() {
        return unityMetastore.getClient();
    }

    @Override
    public MetastoreTable getMetastoreTable(String dbName, String tableName) {
        return unityMetastore.getMetastoreTable(dbName, tableName);
    }

    @Override
    public CloudConfiguration resolveTableCloudConfiguration(String dbName, String tableName) {
        if (!isVendedCredentialsEnabled()) {
            return null;
        }
        return unityMetastore.resolveCloudConfiguration(
                unityMetastore.fetchTableInfo(dbName, tableName));
    }

    @Override
    public DeltaLakeTable getTable(String dbName, String tableName) {
        TableInfo info = unityMetastore.fetchTableInfo(dbName, tableName);
        CloudConfiguration cc = isVendedCredentialsEnabled()
                ? unityMetastore.resolveCloudConfiguration(info)
                : null;
        MetastoreTable mt = unityMetastore.toMetastoreTable(dbName, tableName, info);
        DeltaLakeSnapshot snapshot = getLatestSnapshot(dbName, tableName, mt, cc);
        DeltaLakeTable t = DeltaUtils.convertDeltaSnapshotToSRTable(getCatalogName(), snapshot);
        if (t != null && cc != null) {
            t.setCloudConfiguration(cc);
        }
        return t;
    }

    @Override
    protected DeltaLakeEngine createDeltaLakeEngine(Configuration effectiveConfiguration, boolean usePerTableConfig) {
        if (isDeltaCacheEnabled()) {
            // Per-table credentials bind into each scoped view's loader and entries are pinned to
            // this catalog's principal scope, so the shared cache is safe regardless of usePerTableConfig.
            return unityMetaCache.createEngine(unityProperties.getPrincipalScope(),
                    effectiveConfiguration, properties);
        }
        // delta-cache.enabled=false means the operator opted out of Delta caching entirely:
        // honor that by forcing bypass on the inherited per-catalog caches as well, even when
        // the caller did not request it. The catalog-level snapshot cache is independently
        // disabled via isSnapshotCacheBypassed().
        return super.createDeltaLakeEngine(effectiveConfiguration, true);
    }

    @Override
    public void invalidateAll() {
        if (!isDeltaCacheEnabled()) {
            super.invalidateAll();
        }
        // The Unity cache is shared across all Unity catalogs in this FE; one catalog shutdown
        // must not evict siblings.
    }

    @Override
    public Map<String, Long> estimateCount() {
        return isDeltaCacheEnabled() ? unityMetaCache.estimateCount() : super.estimateCount();
    }

    @Override
    public List<Pair<List<Object>, Long>> getSamples() {
        return isDeltaCacheEnabled() ? unityMetaCache.getSamples() : super.getSamples();
    }

    @Override
    public boolean isVendedCredentialsEnabled() {
        return unityProperties != null && unityProperties.isVendedCredentialsEnabled();
    }

    /**
     * The latest-snapshot pointer is Unity metadata, so it follows the same on/off and TTL
     * rules as the UC REST cache: bypass whenever {@code unity.catalog.cache.enabled} is
     * {@code false} or {@code unity.catalog.cache.ttl-sec} is {@code 0}. The
     * {@code unity.catalog.delta-cache.enabled} flag is independent and only governs the
     * shared FE-wide JSON/checkpoint metadata-file cache.
     */
    @Override
    public boolean isSnapshotCacheBypassed() {
        if (unityProperties == null) {
            return false;
        }
        return !unityProperties.isCacheEnabled() || unityProperties.getCacheTtlSec() == 0L;
    }

    private boolean isDeltaCacheEnabled() {
        return unityProperties != null && unityProperties.isDeltaCacheEnabled();
    }

    /**
     * Drop the Unity client's cached {@code TableInfo} entry for this table. Called from
     * {@link com.starrocks.connector.delta.CachingDeltaLakeMetastore#refreshTable} which handles
     * {@code REFRESH EXTERNAL TABLE}.
     */
    @Override
    public void refreshTable(String dbName, String tableName) {
        unityMetastore.invalidateTable(dbName, tableName);
    }

    @VisibleForTesting
    public UnityDeltaLakeMetaCache getUnityMetaCache() {
        return unityMetaCache;
    }
}
