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

import com.databricks.sdk.service.catalog.AwsCredentials;
import com.databricks.sdk.service.catalog.DataSourceFormat;
import com.databricks.sdk.service.catalog.GenerateTemporaryTableCredentialResponse;
import com.databricks.sdk.service.catalog.TableInfo;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.starrocks.common.Pair;
import com.starrocks.connector.delta.DeltaLakeCatalogProperties;
import com.starrocks.connector.delta.DeltaLakeEngine;
import com.starrocks.connector.delta.DeltaLakeFileStatus;
import com.starrocks.connector.delta.DeltaLakeJsonHandler;
import com.starrocks.connector.delta.DeltaLakeParquetHandler;
import com.starrocks.connector.delta.DeltaLakeSnapshot;
import com.starrocks.connector.exception.StarRocksConnectorException;
import com.starrocks.connector.metastore.MetastoreTable;
import com.starrocks.credential.CloudConfiguration;
import com.starrocks.credential.CloudType;
import com.starrocks.credential.aws.AwsCloudConfiguration;
import io.delta.kernel.data.ColumnarBatch;
import io.delta.kernel.types.StructType;
import mockit.Expectations;
import mockit.Mock;
import mockit.MockUp;
import mockit.Mocked;
import mockit.Verifications;
import org.apache.hadoop.conf.Configuration;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;

public class UnityBackedDeltaMetastoreTest {

    @BeforeEach
    void resetSharedCache() {
        UnityDeltaLakeMetaCache.resetForTest();
    }

    private static UnityCatalogProperties propsWithVendedCredentials() {
        return new UnityCatalogProperties(ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiTEST",
                "unity.catalog.name", "main",
                "unity.catalog.vended-credentials-enabled", "true",
                "unity.catalog.aws.region", "us-east-1"));
    }

    private static TableInfo tableInfo() {
        return new TableInfo()
                .setFullName("main.sales.orders")
                .setTableId("abc-123")
                .setDataSourceFormat(DataSourceFormat.DELTA)
                .setStorageLocation("s3://bucket/prefix/orders")
                .setCreatedAt(1_700_000_000_000L);
    }

    private static GenerateTemporaryTableCredentialResponse creds() {
        return new GenerateTemporaryTableCredentialResponse()
                .setAwsTempCredentials(new AwsCredentials()
                        .setAccessKeyId("AKIA_TEST")
                        .setSecretAccessKey("secret")
                        .setSessionToken("session"));
    }

    private static UnityBackedDeltaMetastore newUnityBacked(UnityMetastore unityMetastore,
                                                            UnityCatalogProperties props) {
        return new UnityBackedDeltaMetastore(
                "delta_unity",
                unityMetastore,
                new Configuration(false),
                new DeltaLakeCatalogProperties(Maps.newHashMap()),
                props);
    }

    @Test
    public void testRefreshTableTriggersFreshCredentialVend(@Mocked UnityCatalogClient client) {
        TableInfo info = tableInfo();
        GenerateTemporaryTableCredentialResponse credentials = creds();

        new Expectations() {
            {
                client.getTable("main.sales.orders");
                result = info;
                times = 2;

                client.getTemporaryTableCredentials("abc-123", "READ");
                result = credentials;
                times = 2;
            }
        };

        UnityMetastore unityMetastore = new UnityMetastore(client, propsWithVendedCredentials());
        UnityBackedDeltaMetastore backed = newUnityBacked(unityMetastore, propsWithVendedCredentials());

        CloudConfiguration first = backed.resolveTableCloudConfiguration("sales", "orders");
        Assertions.assertNotNull(first);
        Assertions.assertEquals(CloudType.AWS, first.getCloudType());
        Assertions.assertInstanceOf(AwsCloudConfiguration.class, first);

        backed.refreshTable("sales", "orders");

        // After refreshTable, the next call must hit UC again.
        CloudConfiguration second = backed.resolveTableCloudConfiguration("sales", "orders");
        Assertions.assertNotNull(second);
        Assertions.assertEquals(CloudType.AWS, second.getCloudType());

        new Verifications() {
            {
                client.invalidate("main.sales.orders");
                times = 1;
            }
        };
    }

    @Test
    public void testRefreshTableNoOpWhenVendedCredentialsDisabled(@Mocked UnityCatalogClient client) {
        UnityCatalogProperties propsNoVend = new UnityCatalogProperties(ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiTEST",
                "unity.catalog.name", "main",
                "unity.catalog.vended-credentials-enabled", "false"));

        UnityMetastore unityMetastore = new UnityMetastore(client, propsNoVend);
        UnityBackedDeltaMetastore backed = new UnityBackedDeltaMetastore(
                "delta_unity",
                unityMetastore,
                new Configuration(false),
                new DeltaLakeCatalogProperties(Maps.newHashMap()),
                propsNoVend);

        backed.refreshTable("sales", "orders");

        new Verifications() {
            {
                client.invalidate("main.sales.orders");
                times = 1;
            }
        };
    }

    @Test
    public void testGetTableInvokesUcGetTableExactlyOnce(@Mocked UnityCatalogClient client) {
        TableInfo info = tableInfo();
        GenerateTemporaryTableCredentialResponse credentials = creds();

        new Expectations() {
            {
                // The whole point of the dedup: one logical UBDM#getTable -> one UC TableInfo
                // fetch and one credential vend, even when metadata caching is bypassed.
                client.getTable("main.sales.orders");
                result = info;
                times = 1;

                client.getTemporaryTableCredentials("abc-123", "READ");
                result = credentials;
                times = 1;
            }
        };

        UnityMetastore unityMetastore = new UnityMetastore(client, propsWithVendedCredentials());
        UnityBackedDeltaMetastore backed = new UnityBackedDeltaMetastore(
                "delta_unity",
                unityMetastore,
                new Configuration(false),
                new DeltaLakeCatalogProperties(Maps.newHashMap()),
                propsWithVendedCredentials()) {
            @Override
            protected DeltaLakeSnapshot getLatestSnapshot(String dbName, String tableName,
                                                         MetastoreTable mt,
                                                         CloudConfiguration cc) {
                // The storage location in the fixture is not reachable from a unit test, so
                // we cannot let the Delta Kernel load run. Asserting the loader received a
                // non-null MetastoreTable plus a per-table AWS CloudConfiguration is what
                // proves UBDM#getTable threaded the dedup'd values through; we then
                // short-circuit with a sentinel so the caller skips snapshot conversion
                // (which would NPE on a null DeltaLakeSnapshot).
                Assertions.assertNotNull(mt);
                Assertions.assertNotNull(cc);
                Assertions.assertEquals(CloudType.AWS, cc.getCloudType());
                Assertions.assertInstanceOf(AwsCloudConfiguration.class, cc);
                throw new SnapshotShortCircuit();
            }
        };

        Assertions.assertThrows(SnapshotShortCircuit.class,
                () -> backed.getTable("sales", "orders"));

        new Verifications() {
            {
                client.getTable("main.sales.orders");
                times = 1;
                client.getTemporaryTableCredentials("abc-123", "READ");
                times = 1;
            }
        };
    }

    /**
     * Sentinel raised from the snapshot-loader override in
     * {@link #testGetTableInvokesUcGetTableExactlyOnce} so the test can verify it reached the
     * loader without paying for an actual Delta Kernel snapshot read.
     */
    private static final class SnapshotShortCircuit extends RuntimeException {
    }

    @Test
    public void testGetTablePropagatesVendingFailure(@Mocked UnityCatalogClient client) {
        TableInfo info = tableInfo();

        new Expectations() {
            {
                client.getTable("main.sales.orders");
                result = info;
                client.getTemporaryTableCredentials("abc-123", "READ");
                result = new StarRocksConnectorException("403 forbidden");
            }
        };

        UnityMetastore unityMetastore = new UnityMetastore(client, propsWithVendedCredentials());
        UnityBackedDeltaMetastore backed = newUnityBacked(unityMetastore, propsWithVendedCredentials());

        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> backed.getTable("sales", "orders"));
        Assertions.assertTrue(ex.getMessage().contains("main.sales.orders"),
                "exception message must include the failing table name; was: " + ex.getMessage());
    }

    @Test
    public void testResolveTableCloudConfigurationPropagatesVendingFailure(
            @Mocked UnityCatalogClient client) {
        TableInfo info = tableInfo();

        new Expectations() {
            {
                client.getTable("main.sales.orders");
                result = info;
                client.getTemporaryTableCredentials("abc-123", "READ");
                result = new StarRocksConnectorException("503 service unavailable");
            }
        };

        UnityMetastore unityMetastore = new UnityMetastore(client, propsWithVendedCredentials());
        UnityBackedDeltaMetastore backed = newUnityBacked(unityMetastore, propsWithVendedCredentials());

        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> backed.resolveTableCloudConfiguration("sales", "orders"));
        Assertions.assertTrue(ex.getMessage().contains("main.sales.orders"),
                "exception message must include the failing table name; was: " + ex.getMessage());
    }

    @Test
    public void testUnityBackedMetastoresShareDeltaMetaCache(@Mocked UnityCatalogClient client) {
        UnityCatalogProperties props = propsWithVendedCredentials();
        UnityBackedDeltaMetastore first = newUnityBacked(new UnityMetastore(client, props), props);
        UnityBackedDeltaMetastore second = newUnityBacked(new UnityMetastore(client, props), props);

        Assertions.assertSame(first.getUnityMetaCache(), second.getUnityMetaCache(),
                "Unity-backed Delta catalogs must share one FE-level JSON/checkpoint cache");
    }

    @Test
    public void testInvalidateAllDoesNotClearSharedDeltaMetaCache(@Mocked UnityCatalogClient client) {
        UnityCatalogProperties props = propsWithVendedCredentials();
        UnityBackedDeltaMetastore backed = newUnityBacked(new UnityMetastore(client, props), props);
        UnityDeltaLakeMetaCache cache = backed.getUnityMetaCache();
        DeltaLakeFileStatus status = DeltaLakeFileStatus.of(
                io.delta.kernel.utils.FileStatus.of("s3://bucket/shared-unity-cache-test.json", 123, 456));

        String scope = props.getPrincipalScope();
        cache.getJsonCache(scope, new Configuration(false), true).put(status, Lists.newArrayList());
        backed.invalidateAll();

        Assertions.assertTrue(cache.containsJson(scope, status),
                "Invalidating one Unity catalog must not clear the process-wide Unity metadata cache");
        Assertions.assertTrue(backed.estimateCount().get("unityJsonCache") >= 1);
        Assertions.assertTrue(backed.getSamples().stream()
                .anyMatch(sample -> sample.second >= 1 && !sample.first.isEmpty()));
    }

    @Test
    public void testSharedMetaCacheIsolatesEntriesByPrincipalScope() {
        UnityDeltaLakeMetaCache cache = UnityDeltaLakeMetaCache.getSharedInstance();
        DeltaLakeFileStatus status = DeltaLakeFileStatus.of(io.delta.kernel.utils.FileStatus.of(
                "s3://bucket/principal-scope-isolation-" + System.nanoTime() + ".json", 1, 2));

        cache.getJsonCache("principal-a", new Configuration(false), true).put(status, Lists.newArrayList());

        Assertions.assertTrue(cache.containsJson("principal-a", status),
                "the populating principal must see its own cached entry");
        Assertions.assertFalse(cache.containsJson("principal-b", status),
                "a different principal must never be served a cache entry it did not populate");
    }

    @Test
    public void testSharedMetaCacheLoaderUsesScopedConfiguration() throws Exception {
        UnityDeltaLakeMetaCache cache = UnityDeltaLakeMetaCache.getSharedInstance();
        DeltaLakeFileStatus status = DeltaLakeFileStatus.of(io.delta.kernel.utils.FileStatus.of(
                "s3://bucket/scoped-config-" + System.nanoTime() + ".json", 123, 456));
        Configuration scoped = new Configuration(false);
        scoped.set("unity.test.scoped-conf", "expected");
        AtomicReference<String> seen = new AtomicReference<>();

        new MockUp<DeltaLakeJsonHandler>() {
            @Mock
            public java.util.List<com.fasterxml.jackson.databind.JsonNode> readJsonFile(String filePath,
                                                                                       Configuration hadoopConf) {
                seen.set(hadoopConf.get("unity.test.scoped-conf"));
                return Lists.newArrayList();
            }
        };

        cache.getJsonCache("test-principal-scope", scoped, false).get(status);

        Assertions.assertEquals("expected", seen.get());
    }

    @Test
    public void testVendedLoaderRunsUnderPerLoadUgi() throws Exception {
        UnityDeltaLakeMetaCache cache = UnityDeltaLakeMetaCache.getSharedInstance();
        String path = "abfss://c@a.dfs.core.windows.net/tables/tid/_delta_log/"
                + System.nanoTime() + ".json";
        DeltaLakeFileStatus status = DeltaLakeFileStatus.of(
                io.delta.kernel.utils.FileStatus.of(path, 1, 2));
        AtomicReference<String> ugiInside = new AtomicReference<>();

        new MockUp<DeltaLakeJsonHandler>() {
            @Mock
            public java.util.List<com.fasterxml.jackson.databind.JsonNode> readJsonFile(String filePath,
                                                                                       Configuration hadoopConf)
                    throws java.io.IOException {
                ugiInside.set(
                        org.apache.hadoop.security.UserGroupInformation.getCurrentUser().getUserName());
                return Lists.newArrayList();
            }
        };

        String loginUgi = org.apache.hadoop.security.UserGroupInformation.getCurrentUser().getUserName();
        cache.getJsonCache("vended-principal-scope", new Configuration(false), true).get(status);

        Assertions.assertEquals(
                com.starrocks.connector.delta.DeltaVendedFsScope.scopeNameForPath(path), ugiInside.get(),
                "vended loader must read under a per-load scope UGI, not the shared login UGI");
        Assertions.assertNotEquals(loginUgi, ugiInside.get());
    }

    @Test
    public void testVendedLoaderReusesEnclosingScopeUgi() throws Exception {
        UnityDeltaLakeMetaCache cache = UnityDeltaLakeMetaCache.getSharedInstance();
        String path = "abfss://c@a.dfs.core.windows.net/tables/tid/_delta_log/"
                + System.nanoTime() + ".json";
        DeltaLakeFileStatus status = DeltaLakeFileStatus.of(
                io.delta.kernel.utils.FileStatus.of(path, 1, 2));
        AtomicReference<String> ugiInside = new AtomicReference<>();

        new MockUp<DeltaLakeJsonHandler>() {
            @Mock
            public java.util.List<com.fasterxml.jackson.databind.JsonNode> readJsonFile(String filePath,
                                                                                       Configuration hadoopConf)
                    throws java.io.IOException {
                ugiInside.set(
                        org.apache.hadoop.security.UserGroupInformation.getCurrentUser().getUserName());
                return Lists.newArrayList();
            }
        };

        // Already inside a per-table scope (the synchronous query / background-refresh path): the loader
        // must reuse this scope's UGI, not open a redundant per-path scope.
        com.starrocks.connector.delta.DeltaVendedFsScope.runScoped("enclosing-scope", () -> {
            cache.getJsonCache("vended-principal-scope", new Configuration(false), true).get(status);
            return null;
        });

        Assertions.assertEquals("enclosing-scope", ugiInside.get(),
                "with an active scope the vended loader must reuse the enclosing UGI");
    }

    @Test
    public void testCheckpointCacheIsolatesEntriesByPrincipalScope() {
        UnityDeltaLakeMetaCache cache = UnityDeltaLakeMetaCache.getSharedInstance();
        Pair<DeltaLakeFileStatus, StructType> key = Pair.create(
                DeltaLakeFileStatus.of(io.delta.kernel.utils.FileStatus.of(
                        "s3://bucket/checkpoint-isolation-" + System.nanoTime() + ".checkpoint.parquet", 1, 2)),
                new StructType());

        cache.getCheckpointCache("principal-a", new Configuration(false), true).put(key, Lists.newArrayList());

        Assertions.assertNotNull(
                cache.getCheckpointCache("principal-a", new Configuration(false), true).getIfPresent(key),
                "the populating principal must see its own cached checkpoint entry");
        Assertions.assertNull(
                cache.getCheckpointCache("principal-b", new Configuration(false), true).getIfPresent(key),
                "a different principal must never be served a checkpoint entry it did not populate");
    }

    @Test
    public void testCheckpointVendedLoaderRunsUnderPerLoadUgi() throws Exception {
        UnityDeltaLakeMetaCache cache = UnityDeltaLakeMetaCache.getSharedInstance();
        String path = "abfss://c@a.dfs.core.windows.net/tables/tid/_delta_log/"
                + System.nanoTime() + ".checkpoint.parquet";
        Pair<DeltaLakeFileStatus, StructType> key = Pair.create(
                DeltaLakeFileStatus.of(io.delta.kernel.utils.FileStatus.of(path, 1, 2)), new StructType());
        AtomicReference<String> ugiInside = new AtomicReference<>();

        new MockUp<DeltaLakeParquetHandler>() {
            @Mock
            public List<ColumnarBatch> readParquetFile(String filePath, long fileSize, long modificationTime,
                                                       StructType physicalSchema, Configuration hadoopConf)
                throws IOException {
                ugiInside.set(
                        org.apache.hadoop.security.UserGroupInformation.getCurrentUser().getUserName());
                return Lists.newArrayList();
            }
        };

        String loginUgi = org.apache.hadoop.security.UserGroupInformation.getCurrentUser().getUserName();
        cache.getCheckpointCache("vended-principal-scope", new Configuration(false), true).get(key);

        Assertions.assertEquals(
                com.starrocks.connector.delta.DeltaVendedFsScope.scopeNameForPath(path), ugiInside.get(),
                "vended checkpoint loader must read under a per-load scope UGI, not the shared login UGI");
        Assertions.assertNotEquals(loginUgi, ugiInside.get());
    }

    @Test
    public void testCheckpointVendedLoaderReusesEnclosingScopeUgi() throws Exception {
        UnityDeltaLakeMetaCache cache = UnityDeltaLakeMetaCache.getSharedInstance();
        String path = "abfss://c@a.dfs.core.windows.net/tables/tid/_delta_log/"
                + System.nanoTime() + ".checkpoint.parquet";
        Pair<DeltaLakeFileStatus, StructType> key = Pair.create(
                DeltaLakeFileStatus.of(io.delta.kernel.utils.FileStatus.of(path, 1, 2)), new StructType());
        AtomicReference<String> ugiInside = new AtomicReference<>();

        new MockUp<DeltaLakeParquetHandler>() {
            @Mock
            public List<ColumnarBatch> readParquetFile(String filePath, long fileSize, long modificationTime,
                                                       StructType physicalSchema, Configuration hadoopConf)
                throws IOException {
                ugiInside.set(
                        org.apache.hadoop.security.UserGroupInformation.getCurrentUser().getUserName());
                return Lists.newArrayList();
            }
        };

        com.starrocks.connector.delta.DeltaVendedFsScope.runScoped("enclosing-scope", () -> {
            cache.getCheckpointCache("vended-principal-scope", new Configuration(false), true).get(key);
            return null;
        });

        Assertions.assertEquals("enclosing-scope", ugiInside.get(),
                "with an active scope the vended checkpoint loader must reuse the enclosing UGI");
    }

    @Test
    public void testCreateEngineWiresSharedCachingHandlers() {
        UnityDeltaLakeMetaCache cache = UnityDeltaLakeMetaCache.getSharedInstance();
        Configuration conf = new Configuration(false);

        DeltaLakeEngine engine = cache.createEngine("principal-scope", conf,
                new DeltaLakeCatalogProperties(Maps.newHashMap()), true);

        Assertions.assertSame(conf, engine.getHadoopConf());
        Assertions.assertFalse(engine.isPerTableConfig(),
                "the shared-cache engine must consult the meta cache, not bypass it");
        Assertions.assertTrue(engine.getJsonHandler() instanceof DeltaLakeJsonHandler,
                "createEngine must wire the caching JSON handler");
        Assertions.assertTrue(engine.getParquetHandler() instanceof DeltaLakeParquetHandler,
                "createEngine must wire the caching parquet handler");
    }
}
