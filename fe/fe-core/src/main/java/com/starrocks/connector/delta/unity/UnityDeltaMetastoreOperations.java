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

import com.starrocks.catalog.Table;
import com.starrocks.connector.DatabaseTableName;
import com.starrocks.connector.MetastoreType;
import com.starrocks.connector.delta.CachingDeltaLakeMetastore;
import com.starrocks.connector.delta.DeltaMetastoreOperations;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Unity-specific table lookup wrapper. The generic Delta cache may load a snapshot directly and
 * bypass {@link UnityBackedDeltaMetastore#getTable}, so view fallback has to sit above that cache
 * where the operation can still return either a Delta table or a connector view.
 */
public class UnityDeltaMetastoreOperations extends DeltaMetastoreOperations {
    private final CachingDeltaLakeMetastore metastore;
    // UnityDeltaMetastoreOperations is created with the per-query metastore, so this cache is
    // scoped to one query and prevents repeated Delta load attempts for the same resolved view.
    private final Map<DatabaseTableName, Table> resolvedViews = new ConcurrentHashMap<>();

    public UnityDeltaMetastoreOperations(CachingDeltaLakeMetastore metastore, boolean enableCatalogLevelCache,
                                         MetastoreType metastoreType) {
        super(metastore, enableCatalogLevelCache, metastoreType);
        this.metastore = metastore;
    }

    @Override
    public Table getTable(String dbName, String tableName) {
        DatabaseTableName key = DatabaseTableName.of(dbName, tableName);
        Table resolvedView = resolvedViews.get(key);
        if (resolvedView != null) {
            return resolvedView;
        }
        try {
            return super.getTable(dbName, tableName);
        } catch (UnityCatalogUnsupportedTableFormatException e) {
            Table view = metastore.getView(dbName, tableName);
            if (view != null) {
                resolvedViews.put(key, view);
                return view;
            }
            throw e;
        }
    }
}
