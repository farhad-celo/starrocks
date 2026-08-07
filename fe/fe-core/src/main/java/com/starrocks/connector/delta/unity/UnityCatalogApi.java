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

import com.databricks.sdk.service.catalog.GetMetastoreSummaryResponse;
import com.databricks.sdk.service.catalog.SchemaInfo;
import com.databricks.sdk.service.catalog.TableInfo;
import io.unitycatalog.client.delta.model.DeltaCredentialsResponse;
import io.unitycatalog.client.delta.model.DeltaLoadTableResponse;

import java.util.List;

/**
 * Narrow Unity Catalog surface the Delta Lake connector depends on. Schema/table discovery and the
 * metastore summary use Databricks SDK model types; the read path ({@link #loadTable},
 * {@link #getTableCredentials}) goes through the Unity Catalog {@code delta/v1} client.
 */
public interface UnityCatalogApi {

    List<SchemaInfo> listSchemas(String ucCatalog);

    List<TableInfo> listTables(String ucCatalog, String schemaName);

    boolean tableExists(String fullName);

    /**
     * Load Delta metadata plus the inline ratified (unbackfilled) commits via {@code delta/v1}
     * {@code loadTable}. The commit list is complete (not paginated) and must not be REST-cached,
     * since it changes on every write.
     */
    DeltaLoadTableResponse loadTable(String ucCatalog, String schemaName, String tableName);

    /**
     * Vend temporary cloud storage credentials via {@code delta/v1} {@code getTableCredentials}.
     * {@code tableId} is the Delta table UUID; it is not sent to the endpoint but participates in
     * the credential cache key so credentials never alias across a drop/recreate of the same name.
     */
    DeltaCredentialsResponse getTableCredentials(String ucCatalog, String schemaName, String tableName,
                                                 String tableId, String operation);

    GetMetastoreSummaryResponse getMetastoreSummary();

    /** Drop any cached state referencing {@code fullName} (for {@code REFRESH EXTERNAL TABLE}). */
    default void invalidate(String fullName) {
    }
}
