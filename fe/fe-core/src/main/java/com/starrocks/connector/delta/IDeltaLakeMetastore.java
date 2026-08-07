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

package com.starrocks.connector.delta;

import com.starrocks.catalog.Table;
import com.starrocks.connector.metastore.IMetastore;
import com.starrocks.credential.CloudConfiguration;
import com.starrocks.memory.MemoryTrackable;

import java.util.List;

public interface IDeltaLakeMetastore extends IMetastore, MemoryTrackable {
    String getCatalogName();

    default boolean isVendedCredentialsEnabled() {
        return false;
    }

    /**
     * Returns {@code true} when the operator has opted out of the catalog-level snapshot cache, so
     * {@link CachingDeltaLakeMetastore#getTable} loads a fresh snapshot for every query.
     */
    default boolean isSnapshotCacheBypassed() {
        return false;
    }

    /**
     * Per-table cloud configuration to attach to the SRTable. Without this, snapshot-cache hits in
     * {@link CachingDeltaLakeMetastore#getTable} would skip per-table vended credentials and the BE
     * would get credential-less scan ranges (S3 403s). Returns {@code null} when not vending.
     */
    default CloudConfiguration resolveTableCloudConfiguration(String dbName, String tableName) {
        return null;
    }

    /**
     * Overload supplying an already-known {@code tableLocation} and Delta {@code tableId} (the table
     * UUID) so the snapshot-cache-hit path in {@link CachingDeltaLakeMetastore#getTable} can vend
     * credentials without re-fetching table metadata. Both come from the cached
     * {@link DeltaLakeSnapshot}. The default falls back to {@link #resolveTableCloudConfiguration(String, String)}.
     */
    default CloudConfiguration resolveTableCloudConfiguration(String dbName, String tableName, String tableLocation,
                                                              String tableId) {
        return resolveTableCloudConfiguration(dbName, tableName);
    }

    /**
     * Drop per-table state for {@code (dbName, tableName)} so {@code REFRESH EXTERNAL TABLE} flushes
     * downstream caches. No-op default.
     */
    default void refreshTable(String dbName, String tableName) {
    }

    Table getTable(String dbName, String tableName);

    List<String> getPartitionKeys(String dbName, String tableName);

    DeltaLakeSnapshot getLatestSnapshot(String dbName, String tableName);
}
