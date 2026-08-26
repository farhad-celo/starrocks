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
import com.google.common.base.Ticker;
import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import io.unitycatalog.client.delta.model.DeltaCredentialsResponse;
import io.unitycatalog.client.delta.model.DeltaLoadTableResponse;
import io.unitycatalog.client.delta.model.DeltaStorageCredential;

import java.util.List;
import java.util.Objects;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.function.LongSupplier;

/**
 * Caching decorator in front of a {@link UnityCatalogApi} delegate. Backed by three Guava caches:
 * schemas per catalog, tables per (catalog, schema), and {@link DeltaCredentialsResponse} per
 * (catalog, schema, table, tableId, operation).
 *
 * <p>Metadata caches honor {@code unity.catalog.cache.enabled} and {@code unity.catalog.cache.ttl-sec};
 * credentials use a separate TTL independent of that config and are additionally re-vended once
 * the server-side {@code expiration_time_ms} (minus a safety margin) is near, so a cached entry is
 * never served past that safety window regardless of the TTL. {@link #loadTable} is never cached
 * (commits must be read fresh) and {@link #tableExists} always delegates.
 */
public class CachingUnityCatalogClient implements UnityCatalogApi {
    private final UnityCatalogApi delegate;
    private final long credentialsSafetyMarginMs;
    private final LongSupplier clockMillis;

    private final Cache<String, List<SchemaInfo>> schemasCache;
    private final Cache<SchemaKey, List<TableInfo>> tablesCache;
    private final Cache<CredentialsKey, DeltaCredentialsResponse> credentialsCache;

    public CachingUnityCatalogClient(UnityCatalogApi delegate, UnityCatalogProperties properties) {
        this(delegate, properties, Ticker.systemTicker(), System::currentTimeMillis);
    }

    // Visible for testing: lets tests advance a FakeTicker for TTL assertions and a separate
    // wall-clock supplier for credential-expiration assertions.
    CachingUnityCatalogClient(UnityCatalogApi delegate,
                              UnityCatalogProperties properties,
                              Ticker ticker,
                              LongSupplier clockMillis) {
        this.delegate = Objects.requireNonNull(delegate, "delegate");
        Objects.requireNonNull(properties, "properties");
        this.credentialsSafetyMarginMs =
                TimeUnit.SECONDS.toMillis(properties.getCredentialsSafetyMarginSec());
        this.clockMillis = Objects.requireNonNull(clockMillis, "clockMillis");

        long ttlSec = properties.getCacheTtlSec();
        this.schemasCache = newMetadataCache(ticker, ttlSec, properties.isCacheEnabled());
        this.tablesCache = newMetadataCache(ticker, ttlSec, properties.isCacheEnabled());
        this.credentialsCache = newCredentialsCache(ticker, properties.getCredentialsCacheTtlSec());
    }

    private static <K, V> Cache<K, V> newMetadataCache(Ticker ticker, long ttlSec, boolean enabled) {
        return newTtlCache(ticker, ttlSec, enabled);
    }

    private static <K, V> Cache<K, V> newCredentialsCache(Ticker ticker, long ttlSec) {
        return newTtlCache(ticker, ttlSec, true);
    }

    private static <K, V> Cache<K, V> newTtlCache(Ticker ticker, long ttlSec, boolean enabled) {
        CacheBuilder<Object, Object> builder = CacheBuilder.newBuilder().ticker(ticker);
        if (enabled && ttlSec > 0) {
            builder.expireAfterWrite(ttlSec, TimeUnit.SECONDS);
        } else {
            builder.expireAfterWrite(0, TimeUnit.NANOSECONDS);
        }
        return builder.build();
    }

    @Override
    public List<SchemaInfo> listSchemas(String ucCatalog) {
        try {
            return schemasCache.get(ucCatalog, () -> delegate.listSchemas(ucCatalog));
        } catch (ExecutionException e) {
            throw unwrap(e);
        }
    }

    @Override
    public List<TableInfo> listTables(String ucCatalog, String schemaName) {
        SchemaKey key = new SchemaKey(ucCatalog, schemaName);
        try {
            return tablesCache.get(key, () -> delegate.listTables(ucCatalog, schemaName));
        } catch (ExecutionException e) {
            throw unwrap(e);
        }
    }

    @Override
    public TableInfo getTableInfo(String fullName) {
        return delegate.getTableInfo(fullName);
    }

    @Override
    public boolean tableExists(String fullName) {
        return delegate.tableExists(fullName);
    }

    @Override
    public GetMetastoreSummaryResponse getMetastoreSummary() {
        return delegate.getMetastoreSummary();
    }

    @Override
    public DeltaLoadTableResponse loadTable(String ucCatalog, String schemaName, String tableName) {
        return delegate.loadTable(ucCatalog, schemaName, tableName);
    }

    @Override
    public DeltaCredentialsResponse getTableCredentials(String ucCatalog, String schemaName, String tableName,
                                                        String tableId, String operation) {
        // tableId (Delta table UUID) is part of the key so credentials never survive a drop/recreate
        // that reuses the three-part name; operation keeps READ and READ_WRITE leases distinct.
        CredentialsKey key = new CredentialsKey(ucCatalog, schemaName, tableName, tableId, operation);
        DeltaCredentialsResponse cached = credentialsCache.getIfPresent(key);
        if (cached != null && !isNearExpiry(cached)) {
            return cached;
        }
        DeltaCredentialsResponse fresh =
                delegate.getTableCredentials(ucCatalog, schemaName, tableName, tableId, operation);
        if (fresh != null && !isNearExpiry(fresh)) {
            credentialsCache.put(key, fresh);
        }
        return fresh;
    }

    private boolean isNearExpiry(DeltaCredentialsResponse creds) {
        if (creds == null || creds.getStorageCredentials().isEmpty()) {
            return true;
        }
        long earliest = Long.MAX_VALUE;
        for (DeltaStorageCredential credential : creds.getStorageCredentials()) {
            Long expiration = credential.getExpirationTimeMs();
            earliest = Math.min(earliest, expiration);
        }
        return earliest - credentialsSafetyMarginMs <= clockMillis.getAsLong();
    }

    private static RuntimeException unwrap(ExecutionException e) {
        Throwable cause = e.getCause();
        if (cause instanceof RuntimeException) {
            return (RuntimeException) cause;
        }
        if (cause instanceof Error) {
            throw (Error) cause;
        }
        return new RuntimeException(cause == null ? e : cause);
    }

    private static final class SchemaKey {
        private final String catalogName;
        private final String schemaName;

        SchemaKey(String catalogName, String schemaName) {
            this.catalogName = catalogName;
            this.schemaName = schemaName;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof SchemaKey)) {
                return false;
            }
            SchemaKey that = (SchemaKey) o;
            return Objects.equals(catalogName, that.catalogName)
                    && Objects.equals(schemaName, that.schemaName);
        }

        @Override
        public int hashCode() {
            return Objects.hash(catalogName, schemaName);
        }
    }

    private static final class CredentialsKey {
        private final String catalogName;
        private final String schemaName;
        private final String tableName;
        private final String tableId;
        private final String operation;

        CredentialsKey(String catalogName, String schemaName, String tableName, String tableId, String operation) {
            this.catalogName = catalogName;
            this.schemaName = schemaName;
            this.tableName = tableName;
            this.tableId = tableId;
            this.operation = operation;
        }

        @Override
        public boolean equals(Object o) {
            if (this == o) {
                return true;
            }
            if (!(o instanceof CredentialsKey)) {
                return false;
            }
            CredentialsKey that = (CredentialsKey) o;
            return Objects.equals(catalogName, that.catalogName)
                    && Objects.equals(schemaName, that.schemaName)
                    && Objects.equals(tableName, that.tableName)
                    && Objects.equals(tableId, that.tableId)
                    && Objects.equals(operation, that.operation);
        }

        @Override
        public int hashCode() {
            return Objects.hash(catalogName, schemaName, tableName, tableId, operation);
        }
    }
}
