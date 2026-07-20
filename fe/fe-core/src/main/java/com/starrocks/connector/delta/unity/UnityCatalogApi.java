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

import com.databricks.sdk.service.catalog.GenerateTemporaryTableCredentialResponse;
import com.databricks.sdk.service.catalog.GetMetastoreSummaryResponse;
import com.databricks.sdk.service.catalog.SchemaInfo;
import com.databricks.sdk.service.catalog.TableInfo;

import java.util.List;

/**
 * Narrow Unity Catalog surface area the Delta Lake connector depends on. The interface returns
 * Databricks Java SDK model types directly so that {@link UnityCatalogClient} (live, backed by
 * {@code WorkspaceClient}) and {@link CachingUnityCatalogClient} (decorator) can be swapped
 * interchangeably by {@link UnityMetastore}.
 */
public interface UnityCatalogApi {

    List<SchemaInfo> listSchemas(String ucCatalog);

    List<TableInfo> listTables(String ucCatalog, String schemaName);

    TableInfo getTable(String fullName);

    boolean tableExists(String fullName);

    GenerateTemporaryTableCredentialResponse getTemporaryTableCredentials(String tableId, String operation);

    /**
     * Returns a summary of the Unity Catalog metastore the configured credentials authenticate
     * against. Used by the connector to derive the AWS region for vended S3 credentials so the
     * BE's AWS SDK does not default to {@code us-east-1}. Implementations should cache the
     * result; the value rarely (if ever) changes for the lifetime of a catalog.
     */
    GetMetastoreSummaryResponse getMetastoreSummary();

    /**
     * Drop any cached state referencing {@code fullName}. Called from {@code REFRESH EXTERNAL
     * TABLE} so manual refresh propagates through the caching decorator. Direct (non-caching)
     * implementations have nothing to drop and inherit this no-op default.
     */
    default void invalidate(String fullName) {
    }
}
