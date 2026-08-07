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

import com.databricks.sdk.service.catalog.DataSourceFormat;
import com.databricks.sdk.service.catalog.GetMetastoreSummaryResponse;
import com.databricks.sdk.service.catalog.TableInfo;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Strings;
import com.starrocks.catalog.Database;
import com.starrocks.connector.ConnectorTableId;
import com.starrocks.connector.exception.StarRocksConnectorException;
import com.starrocks.connector.metastore.IMetastore;
import com.starrocks.connector.metastore.MetastoreTable;
import com.starrocks.credential.CloudConfiguration;
import io.unitycatalog.client.delta.model.DeltaCredentialsResponse;
import io.unitycatalog.client.delta.model.DeltaLoadTableResponse;
import io.unitycatalog.client.delta.model.DeltaTableMetadata;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.util.List;
import java.util.Objects;
import java.util.stream.Collectors;

/**
 * {@link IMetastore} implementation backed by the Databricks Unity Catalog REST API. Used as the
 * delegate inside {@link UnityBackedDeltaMetastore} to list schemas/tables and resolve per-table
 * storage locations (with optional vended credentials). The read path uses the Unity Catalog
 * {@code delta/v1} client for {@code loadTable} and {@code getTableCredentials}.
 */
public class UnityMetastore implements IMetastore {
    private static final Logger LOG = LogManager.getLogger(UnityMetastore.class);
    private static final String READ_OPERATION = "READ";

    private final UnityCatalogApi client;
    private final UnityCatalogProperties properties;
    // A UC metastore's AWS region is immutable, so memoize it on first success. null means
    // "not fetched yet (or last attempt failed)" so a transient failure retries next query.
    private volatile String cachedRegion;

    public UnityMetastore(UnityCatalogApi client, UnityCatalogProperties properties) {
        this.client = Objects.requireNonNull(client, "client");
        this.properties = Objects.requireNonNull(properties, "properties");
    }

    @VisibleForTesting
    UnityCatalogApi getClient() {
        return client;
    }

    @Override
    public List<String> getAllDatabaseNames() {
        return client.listSchemas(properties.getUcCatalogName()).stream()
                .map(s -> s.getName())
                .filter(n -> !Strings.isNullOrEmpty(n))
                .collect(Collectors.toList());
    }

    @Override
    public List<String> getAllTableNames(String dbName) {
        return client.listTables(properties.getUcCatalogName(), dbName).stream()
                .filter(UnityMetastore::isDelta)
                .map(t -> t.getName())
                .filter(n -> !Strings.isNullOrEmpty(n))
                .collect(Collectors.toList());
    }

    @Override
    public Database getDb(String dbName) {
        return client.listSchemas(properties.getUcCatalogName()).stream()
                .filter(s -> dbName.equalsIgnoreCase(s.getName()))
                .findFirst()
                .map(s -> new Database(ConnectorTableId.CONNECTOR_ID_GENERATOR.getNextId().asInt(),
                        dbName,
                        s.getStorageLocation() == null ? "" : s.getStorageLocation()))
                .orElse(null);
    }

    @Override
    public MetastoreTable getMetastoreTable(String dbName, String tableName) {
        return toMetastoreTable(dbName, tableName, loadTable(dbName, tableName));
    }

    /**
     * Build a {@link UnityMetastoreTable} from an already-fetched {@link DeltaLoadTableResponse},
     * which is carried on the returned table so {@link UnityBackedDeltaMetastore} can reuse its
     * inline commits when loading the snapshot (one {@code loadTable} per load).
     */
    public UnityMetastoreTable toMetastoreTable(String dbName, String tableName, DeltaLoadTableResponse response) {
        DeltaTableMetadata metadata = response == null ? null : response.getMetadata();
        if (metadata == null || Strings.isNullOrEmpty(metadata.getLocation())) {
            throw new StarRocksConnectorException(
                    "Unity Catalog table %s is missing a storage location; only external Delta tables " +
                            "with a resolvable location are supported", fullName(dbName, tableName));
        }
        // UC returns created-time in epoch millis; StarRocks table timestamps are epoch seconds.
        long createTime = epochMillisToSeconds(metadata.getCreatedTime());
        return new UnityMetastoreTable(dbName, tableName, metadata.getLocation(), createTime, response);
    }

    private static long epochMillisToSeconds(Long epochMillis) {
        return epochMillis != null ? epochMillis / 1000L : 0L;
    }

    /** Load Delta metadata plus inline catalog-owned commits via {@code delta/v1} {@code loadTable}. */
    public DeltaLoadTableResponse loadTable(String dbName, String tableName) {
        try {
            return client.loadTable(properties.getUcCatalogName(), dbName, tableName);
        } catch (StarRocksConnectorException e) {
            LOG.error("Failed to load Unity Catalog table {}", fullName(dbName, tableName), e);
            throw e;
        }
    }

    @Override
    public boolean tableExists(String dbName, String tableName) {
        return client.tableExists(fullName(dbName, tableName));
    }

    /**
     * Vend cloud credentials for {@code (dbName, tableName)} at {@code tableLocation} and translate
     * them into a StarRocks {@link CloudConfiguration}. {@code tableId} (Delta table UUID) keys the
     * credential cache. Returns {@code null} when vended credentials are disabled.
     */
    public CloudConfiguration resolveCloudConfiguration(String dbName, String tableName, String tableLocation,
                                                        String tableId) {
        if (!properties.isVendedCredentialsEnabled()) {
            return null;
        }
        DeltaCredentialsResponse creds;
        try {
            creds = client.getTableCredentials(properties.getUcCatalogName(), dbName, tableName, tableId,
                    READ_OPERATION);
        } catch (StarRocksConnectorException e) {
            throw new StarRocksConnectorException(
                    "Failed to vend Unity Catalog credentials for " + fullName(dbName, tableName)
                            + ": " + e.getMessage(), e);
        }
        // Region is only consumed by the AWS branch; resolving it for Azure/GCP would waste a
        // metastore_summary call and could raise a misleading "AWS metastore" error.
        String awsRegion = UnityCatalogCredentialTranslator.hasAwsCredential(creds, tableLocation)
                ? resolveAwsRegion() : null;
        CloudConfiguration cc = UnityCatalogCredentialTranslator.toCloudConfiguration(creds, tableLocation, awsRegion);
        if (cc == null || cc.getCloudType() == com.starrocks.credential.CloudType.DEFAULT) {
            throw new StarRocksConnectorException(
                    "Unity Catalog returned no recognizable credentials for %s; expected AWS, " +
                            "Azure or GCP fields",
                    fullName(dbName, tableName));
        }
        return cc;
    }

    /**
     * Resolve the AWS region for vended credentials: the operator override if set, otherwise a
     * once-per-handle {@code metastore_summary} lookup. Throws when UC exposes no region rather
     * than defaulting to {@code us-east-1}, which would cause opaque 403s for other regions.
     */
    private String resolveAwsRegion() {
        String override = properties.getAwsRegionOverride();
        if (override != null) {
            return override;
        }
        if (cachedRegion != null) {
            return cachedRegion;
        }
        GetMetastoreSummaryResponse summary = client.getMetastoreSummary();
        if (summary == null || Strings.isNullOrEmpty(summary.getRegion())) {
            throw new StarRocksConnectorException(
                    "Unity Catalog metastore_summary did not return an AWS region for catalog " +
                            "'%s'. Vended credentials require a region; either verify that the " +
                            "configured catalog points at a Databricks AWS metastore, or set " +
                            "'%s' explicitly on the catalog.",
                    properties.getUcCatalogName(), UnityCatalogProperties.UNITY_CATALOG_AWS_REGION);
        }
        cachedRegion = summary.getRegion().trim();
        return cachedRegion;
    }

    /** Drop cached client-side state for {@code (dbName, tableName)} (for {@code REFRESH EXTERNAL TABLE}). */
    public void invalidateTable(String dbName, String tableName) {
        client.invalidate(fullName(dbName, tableName));
    }

    private String fullName(String dbName, String tableName) {
        return properties.getUcCatalogName() + "." + dbName + "." + tableName;
    }

    private static boolean isDelta(TableInfo info) {
        return info != null && info.getDataSourceFormat() == DataSourceFormat.DELTA;
    }
}
