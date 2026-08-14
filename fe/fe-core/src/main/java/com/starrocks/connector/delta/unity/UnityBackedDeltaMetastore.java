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
import com.databricks.sdk.service.catalog.TableType;
import com.google.common.annotations.VisibleForTesting;
import com.starrocks.catalog.DeltaLakeTable;
import com.starrocks.catalog.Table;
import com.starrocks.common.Pair;
import com.starrocks.connector.delta.DeltaLakeCatalogProperties;
import com.starrocks.connector.delta.DeltaLakeEngine;
import com.starrocks.connector.delta.DeltaLakeMetastore;
import com.starrocks.connector.delta.DeltaLakeSnapshot;
import com.starrocks.connector.delta.DeltaUtils;
import com.starrocks.connector.exception.StarRocksConnectorException;
import com.starrocks.connector.metastore.MetastoreTable;
import com.starrocks.credential.CloudConfiguration;
import com.starrocks.qe.ConnectContext;
import io.delta.kernel.SnapshotBuilder;
import io.delta.kernel.TableManager;
import io.delta.kernel.internal.SnapshotImpl;
import io.delta.kernel.internal.files.ParsedCatalogCommitData;
import io.delta.kernel.internal.files.ParsedLogData;
import io.delta.kernel.internal.tablefeatures.TableFeatures;
import io.delta.kernel.utils.FileStatus;
import io.unitycatalog.client.delta.model.DeltaCommit;
import io.unitycatalog.client.delta.model.DeltaLoadTableResponse;
import io.unitycatalog.client.delta.model.DeltaTableMetadata;
import org.apache.hadoop.conf.Configuration;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.UUID;

/**
 * {@link DeltaLakeMetastore} backed by a {@link UnityMetastore}: metadata, inline catalog-owned
 * commits and vended credentials all come from the Unity Catalog {@code delta/v1} client. Each
 * logical load performs one {@code loadTable} (its response is carried on the
 * {@link UnityMetastoreTable} and reused when building the snapshot) and, when enabled, one
 * (cache-absorbed) {@code getTableCredentials}.
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
        UnityMetastoreTable mt = (UnityMetastoreTable) getMetastoreTable(dbName, tableName);
        return unityMetastore.resolveCloudConfiguration(dbName, tableName, mt.getTableLocation(), tableUuid(mt));
    }

    /**
     * Vended-credentials resolution for the snapshot-cache-hit path, where the location and Delta
     * table UUID are already known from the cached {@link DeltaLakeSnapshot}, so no {@code loadTable}
     * is needed.
     */
    @Override
    public CloudConfiguration resolveTableCloudConfiguration(String dbName, String tableName, String tableLocation,
                                                             String tableId) {
        if (!isVendedCredentialsEnabled()) {
            return null;
        }
        return unityMetastore.resolveCloudConfiguration(dbName, tableName, tableLocation, tableId);
    }

    @Override
    public DeltaLakeTable getTable(String dbName, String tableName) {
        Pair<UnityMetastoreTable, CloudConfiguration> loaded = loadTableAndConfig(dbName, tableName);
        UnityMetastoreTable mt = loaded.first;
        CloudConfiguration cc = loaded.second;
        DeltaLakeSnapshot snapshot = getLatestSnapshot(dbName, tableName, mt, cc);
        DeltaLakeTable t = DeltaUtils.convertDeltaSnapshotToSRTable(getCatalogName(), snapshot);
        if (t != null && cc != null) {
            t.setCloudConfiguration(cc);
        }
        return t;
    }

    @Override
    public Table getView(String dbName, String tableName) {
        return resolveView(dbName, tableName);
    }

    private Table resolveView(String dbName, String tableName) {
        TableInfo info = unityMetastore.getTableInfo(dbName, tableName);
        if (info == null || info.getTableType() != TableType.VIEW) {
            return null;
        }
        return UnityViewConverter.toDeltaLakeView(getCatalogName(), dbName, tableName, info);
    }

    @Override
    public DeltaLakeSnapshot getLatestSnapshot(String dbName, String tableName) {
        Pair<UnityMetastoreTable, CloudConfiguration> loaded = loadTableAndConfig(dbName, tableName);
        return getLatestSnapshot(dbName, tableName, loaded.first, loaded.second);
    }

    @Override
    public DeltaLakeSnapshot getSnapshotByVersion(String dbName, String tableName, long version) {
        Pair<UnityMetastoreTable, CloudConfiguration> loaded = loadTableAndConfig(dbName, tableName);
        return getSnapshotByVersion(dbName, tableName, loaded.first, loaded.second, version);
    }

    /**
     * Fetch the table once (metadata + inline commits) and, when enabled, vend its credentials. The
     * returned {@link UnityMetastoreTable} carries the {@code loadTable} response so the snapshot
     * build reuses it instead of re-loading.
     */
    private Pair<UnityMetastoreTable, CloudConfiguration> loadTableAndConfig(String dbName, String tableName) {
        UnityMetastoreTable mt = (UnityMetastoreTable) getMetastoreTable(dbName, tableName);
        CloudConfiguration cc = isVendedCredentialsEnabled()
                ? unityMetastore.resolveCloudConfiguration(dbName, tableName, mt.getTableLocation(), tableUuid(mt))
                : null;
        return Pair.create(mt, cc);
    }

    private static String tableUuid(UnityMetastoreTable mt) {
        DeltaLoadTableResponse response = mt == null ? null : mt.getLoadTableResponse();
        DeltaTableMetadata metadata = response == null ? null : response.getMetadata();
        UUID uuid = metadata == null ? null : metadata.getTableUuid();
        return uuid == null ? null : uuid.toString();
    }

    // Unbackfilled catalog-owned commits live at <table>/_delta_log/_staged_commits/<version>.<uuid>.json.
    private static final String STAGED_COMMITS_SUBDIR = "/_delta_log/_staged_commits/";

    /**
     * For catalog-managed tables the newest commits are owned by Unity Catalog and not yet in the
     * filesystem {@code _delta_log}; they arrive inline on the {@code loadTable} response and are
     * handed to Kernel as staged catalog commits. Non-managed tables use the default filesystem read.
     */
    @Override
    protected SnapshotImpl loadSnapshot(DeltaLakeEngine engine, String path, String dbName, String tableName,
                                        MetastoreTable metastoreTable) {
        DeltaLoadTableResponse response = loadTableResponse(metastoreTable, dbName, tableName);
        if (!isCatalogManaged(response.getMetadata())) {
            return super.loadSnapshot(engine, path, dbName, tableName, metastoreTable);
        }
        return buildCatalogManagedSnapshot(engine, path, dbName, tableName, response, null);
    }

    // Time travel: as loadSnapshot but pinned to version. Non-managed uses the default filesystem read.
    @Override
    protected SnapshotImpl loadSnapshotAsOfVersion(DeltaLakeEngine engine, String path, String dbName, String tableName,
                                                   MetastoreTable metastoreTable, long version) {
        DeltaLoadTableResponse response = loadTableResponse(metastoreTable, dbName, tableName);
        if (!isCatalogManaged(response.getMetadata())) {
            return super.loadSnapshotAsOfVersion(engine, path, dbName, tableName, metastoreTable, version);
        }
        return buildCatalogManagedSnapshot(engine, path, dbName, tableName, response, version);
    }

    private DeltaLoadTableResponse loadTableResponse(MetastoreTable metastoreTable, String dbName, String tableName) {
        if (metastoreTable instanceof UnityMetastoreTable) {
            DeltaLoadTableResponse response = ((UnityMetastoreTable) metastoreTable).getLoadTableResponse();
            if (response != null) {
                return response;
            }
        }
        return unityMetastore.loadTable(dbName, tableName);
    }

    private SnapshotImpl buildCatalogManagedSnapshot(DeltaLakeEngine engine, String path, String dbName,
                                                     String tableName, DeltaLoadTableResponse response,
                                                     Long version) {
        Long latestVersion = response.getLatestTableVersion();
        if (latestVersion == null) {
            throw new StarRocksConnectorException(
                    "Catalog-managed Delta table %s.%s returned no latest version from Unity Catalog",
                    dbName, tableName);
        }
        SnapshotBuilder builder = TableManager.loadSnapshot(path)
                .withLogData(toCatalogCommits(response.getCommits(), path))
                .withMaxCatalogVersion(latestVersion);
        if (version != null) {
            builder = builder.atVersion(version);
        }
        return (SnapshotImpl) builder.build(engine);
    }

    private static boolean isCatalogManaged(DeltaTableMetadata metadata) {
        Map<String, String> tableProperties = metadata == null ? null : metadata.getProperties();
        return tableProperties != null && TableFeatures.isPropertiesManuallySupportingTableFeature(
                tableProperties, TableFeatures.CATALOG_MANAGED_RW_FEATURE);
    }

    /**
     * Convert Unity's inline commit descriptors into Kernel {@link ParsedCatalogCommitData}, sorted
     * by version as the builder requires. {@code file_name} is the bare basename under
     * {@code _staged_commits/}.
     */
    private static List<ParsedLogData> toCatalogCommits(List<DeltaCommit> commits, String tablePath) {
        List<ParsedLogData> logData = new ArrayList<>();
        if (commits == null || commits.isEmpty()) {
            return logData;
        }
        String stagedCommitDir = stripTrailingSlash(tablePath) + STAGED_COMMITS_SUBDIR;
        commits.stream()
                .sorted(Comparator.comparing(DeltaCommit::getVersion))
                .forEach(commit -> {
                    FileStatus fileStatus = FileStatus.of(stagedCommitDir + commit.getFileName(),
                            commit.getFileSize(), commit.getFileModificationTimestamp());
                    logData.add(ParsedCatalogCommitData.forFileStatus(fileStatus));
                });
        return logData;
    }

    private static String stripTrailingSlash(String value) {
        int end = value.length();
        while (end > 0 && value.charAt(end - 1) == '/') {
            end--;
        }
        return value.substring(0, end);
    }

    @Override
    protected DeltaLakeEngine createDeltaLakeEngine(Configuration effectiveConfiguration, boolean usePerTableConfig) {
        if (isDeltaCacheEnabled()) {
            // usePerTableConfig marks vended credentials: the shared cache's loaders must then read under
            // a per-load UGI so a stale/cross-table SAS lease can't be reused from Hadoop's FS cache.
            return unityMetaCache.createEngine(unityProperties.getPrincipalScope(),
                    effectiveConfiguration, properties, usePerTableConfig);
        }
        // delta-cache.enabled=false: operator opted out of Delta caching, so bypass the inherited
        // per-catalog caches too. The catalog-level snapshot cache is disabled via isSnapshotCacheBypassed().
        return super.createDeltaLakeEngine(effectiveConfiguration, true);
    }

    @Override
    public void invalidateAll() {
        // The Unity cache is shared across all Unity catalogs in this FE, so leave it alone here.
        if (!isDeltaCacheEnabled()) {
            super.invalidateAll();
        }
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
     * The snapshot pointer is Unity metadata, so it follows the UC REST cache rules: bypass when
     * {@code unity.catalog.cache.enabled} is false or {@code unity.catalog.cache.ttl-sec} is 0.
     */
    @Override
    public boolean isSnapshotCacheBypassed() {
        if (unityProperties == null) {
            return false;
        }
        return !isSessionSnapshotCacheEnabled()
                || !unityProperties.isCacheEnabled()
                || unityProperties.getCacheTtlSec() == 0L;
    }

    private boolean isSessionSnapshotCacheEnabled() {
        return ConnectContext.getSessionVariableOrDefault().isEnableUnityTableSnapshotCache();
    }

    private boolean isDeltaCacheEnabled() {
        return unityProperties != null && unityProperties.isDeltaCacheEnabled();
    }

    @Override
    public void refreshTable(String dbName, String tableName) {
        unityMetastore.invalidateTable(dbName, tableName);
    }

    @VisibleForTesting
    public UnityDeltaLakeMetaCache getUnityMetaCache() {
        return unityMetaCache;
    }
}
