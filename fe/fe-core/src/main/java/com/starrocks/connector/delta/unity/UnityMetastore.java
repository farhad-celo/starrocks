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
import com.databricks.sdk.service.catalog.GenerateTemporaryTableCredentialResponse;
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
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.util.List;
import java.util.Objects;
import java.util.stream.Collectors;

/**
 * {@link IMetastore} implementation backed by the Databricks Unity Catalog REST API. Used as the
 * delegate inside {@link UnityBackedDeltaMetastore} to list schemas/tables and resolve per-table
 * storage locations (with optional vended credentials).
 */
public class UnityMetastore implements IMetastore {
    private static final Logger LOG = LogManager.getLogger(UnityMetastore.class);

    private final UnityCatalogApi client;
    private final UnityCatalogProperties properties;
    // The Unity Catalog metastore is pinned to a single AWS region at creation time and Databricks
    // does not support cross-region migration, so the region returned by metastore_summary is
    // immutable for the lifetime of a catalog handle. We memoize it on first successful lookup
    // and skip the REST call thereafter. {@code null} means "not fetched yet (or last attempt
    // failed)" so a transient 5xx naturally retries on the next query.
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
        return toMetastoreTable(dbName, tableName, fetchTableInfo(dbName, tableName));
    }

    /**
     * Build a {@link MetastoreTable} from an already-fetched {@link TableInfo}, without making
     * a fresh REST call. Used by {@link UnityBackedDeltaMetastore#getTable} so that one logical
     * table load only triggers a single {@link UnityCatalogApi#getTable} round-trip even when
     * the {@link CachingUnityCatalogClient} decorator is bypassed (cache disabled or TTL=0).
     */
    public MetastoreTable toMetastoreTable(String dbName, String tableName, TableInfo info) {
        // Unity Catalog returns created_at as epoch milliseconds, but StarRocks table
        // timestamps surfaced via information_schema / SHOW TABLE STATUS are epoch seconds
        // (see Table.createTime and DateUtils.formatTimestampInSeconds). Convert to seconds
        // so downstream formatting does not produce far-future dates.
        long createTime = info.getCreatedAt() != null ? info.getCreatedAt() / 1000L : 0L;
        return new MetastoreTable(dbName, tableName, info.getStorageLocation(), createTime);
    }

    public TableInfo fetchTableInfo(String dbName, String tableName) {
        String fullName = fullName(dbName, tableName);
        TableInfo info;
        try {
            info = client.getTable(fullName);
        } catch (StarRocksConnectorException e) {
            LOG.error("Failed to load Unity Catalog table {}", fullName, e);
            throw e;
        }
        if (info == null || Strings.isNullOrEmpty(info.getStorageLocation())) {
            throw new StarRocksConnectorException(
                    "Unity Catalog table %s is missing a storage_location; only external Delta tables " +
                            "with a resolvable location are supported", fullName);
        }
        if (!isDelta(info)) {
            throw new StarRocksConnectorException(
                    "Unity Catalog table %s has unsupported data_source_format=%s; only DELTA is supported",
                    fullName, info.getDataSourceFormat());
        }
        return info;
    }

    @Override
    public boolean tableExists(String dbName, String tableName) {
        return client.tableExists(fullName(dbName, tableName));
    }

    public CloudConfiguration resolveCloudConfiguration(TableInfo info) {
        if (!properties.isVendedCredentialsEnabled()) {
            return null;
        }
        if (Strings.isNullOrEmpty(info.getTableId())) {
            throw new StarRocksConnectorException(
                    "Unity Catalog table %s has no table_id; cannot vend credentials",
                    info.getFullName());
        }
        GenerateTemporaryTableCredentialResponse creds;
        try {
            creds = client.getTemporaryTableCredentials(info.getTableId(), "READ");
        } catch (StarRocksConnectorException e) {
            throw new StarRocksConnectorException(
                    "Failed to vend Unity Catalog credentials for " + info.getFullName()
                            + ": " + e.getMessage(), e);
        }
        // Region is only consumed by the AWS branch of the translator; resolving it for Azure
        // or GCP would burn a metastore_summary call we do not need and -- worse -- would
        // throw a misleading "AWS metastore" error if UC ever returned no region.
        String awsRegion = (creds != null && creds.getAwsTempCredentials() != null) ? resolveAwsRegion() : null;
        CloudConfiguration cc = UnityCatalogCredentialTranslator.toCloudConfiguration(creds,
                info.getStorageLocation(), awsRegion);
        if (cc == null || cc.getCloudType() == com.starrocks.credential.CloudType.DEFAULT) {
            throw new StarRocksConnectorException(
                    "Unity Catalog returned no recognizable credentials for %s; expected AWS, " +
                            "Azure or GCP fields",
                    info.getFullName());
        }
        return cc;
    }

    /**
     * Resolve the AWS region for vended credentials.
     *
     * <p>Resolution order:
     * <ol>
     *   <li>Operator override via {@link UnityCatalogProperties#getAwsRegionOverride()} if
     *       set -- trusted as-is, no REST call.</li>
     *   <li>Otherwise look up the region from Unity Catalog's {@code metastore_summary}
     *       endpoint exactly once per catalog handle. Region is immutable for the lifetime
     *       of a UC metastore so memoizing on first success avoids any further REST calls.
     *       Throws when UC does not expose a region: a Databricks AWS metastore always
     *       returns one, and silently defaulting to the AWS SDK's {@code us-east-1} would
     *       otherwise cause hard-to-diagnose 403s for buckets in other regions. Transient
     *       REST failures propagate too -- the outer caller treats them like a credential
     *       vend failure and {@link #cachedRegion} stays {@code null} so the next query
     *       retries.</li>
     * </ol>
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

    /**
     * Drop any cached client-side state for {@code (dbName, tableName)}. Called from
     * {@link UnityBackedDeltaMetastore#refreshTable(String, String)} so a manual {@code REFRESH
     * EXTERNAL TABLE} also flushes the {@link CachingUnityCatalogClient} {@code TableInfo} and
     * vended-credentials entries. The default {@link UnityCatalogApi#invalidate(String)}
     * implementation is a no-op for the direct REST client, so this is harmless when no caching
     * decorator is in front of it.
     */
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
