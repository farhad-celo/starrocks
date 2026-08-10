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

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.starrocks.connector.delta.DeltaLakeCatalogProperties;
import com.starrocks.connector.delta.DeltaLakeEngine;
import com.starrocks.connector.delta.DeltaLakeFileStatus;
import com.starrocks.connector.delta.DeltaLakeJsonHandler;
import com.starrocks.connector.delta.DeltaLakeSnapshot;
import com.starrocks.connector.exception.StarRocksConnectorException;
import com.starrocks.connector.metastore.MetastoreTable;
import com.starrocks.credential.CloudConfiguration;
import com.starrocks.credential.CloudType;
import com.starrocks.credential.aws.AwsCloudConfiguration;
import com.starrocks.qe.ConnectContext;
import io.delta.kernel.SnapshotBuilder;
import io.delta.kernel.Table;
import io.delta.kernel.TableManager;
import io.delta.kernel.engine.Engine;
import io.delta.kernel.internal.SnapshotImpl;
import io.delta.kernel.internal.files.ParsedLogData;
import io.unitycatalog.client.delta.model.DeltaCommit;
import io.unitycatalog.client.delta.model.DeltaLoadTableResponse;
import mockit.Expectations;
import mockit.Mock;
import mockit.MockUp;
import mockit.Mocked;
import mockit.Verifications;
import org.apache.hadoop.conf.Configuration;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;

import java.util.Arrays;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Consumer;
import java.util.stream.Stream;

public class UnityBackedDeltaMetastoreTest {

    private static final String S3_LOCATION = "s3://bucket/prefix/orders";

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

    private static UnityCatalogProperties propsNoVendedCredentials() {
        return new UnityCatalogProperties(ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiTEST",
                "unity.catalog.name", "main",
                "unity.catalog.vended-credentials-enabled", "false"));
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

    private static UnityBackedDeltaMetastore metastore(UnityCatalogClient client, UnityCatalogProperties props) {
        return newUnityBacked(new UnityMetastore(client, props), props);
    }

    @Test
    public void testRefreshTableTriggersFreshCredentialVend(@Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.loadTable("main", "sales", "orders");
                result = UnityDeltaModelFixtures.loadResponse(S3_LOCATION, 1_700_000_000_000L, null);
                times = 2;

                client.getTableCredentials("main", "sales", "orders", UnityDeltaModelFixtures.TABLE_UUID.toString(), "READ");
                result = UnityDeltaModelFixtures.awsCredsResponse(1L);
                times = 2;
            }
        };

        UnityBackedDeltaMetastore backed = metastore(client, propsWithVendedCredentials());

        CloudConfiguration first = backed.resolveTableCloudConfiguration("sales", "orders");
        Assertions.assertNotNull(first);
        Assertions.assertEquals(CloudType.AWS, first.getCloudType());
        Assertions.assertInstanceOf(AwsCloudConfiguration.class, first);

        backed.refreshTable("sales", "orders");

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
        UnityBackedDeltaMetastore backed = metastore(client, propsNoVendedCredentials());

        backed.refreshTable("sales", "orders");

        new Verifications() {
            {
                client.invalidate("main.sales.orders");
                times = 1;
            }
        };
    }

    @Test
    public void testGetTableInvokesLoadTableExactlyOnce(@Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                // The whole point of the dedup: one logical UBDM#getTable -> one loadTable and one
                // credential vend, even when metadata caching is bypassed.
                client.loadTable("main", "sales", "orders");
                result = UnityDeltaModelFixtures.loadResponse(S3_LOCATION, 1_700_000_000_000L, null);
                times = 1;

                client.getTableCredentials("main", "sales", "orders", UnityDeltaModelFixtures.TABLE_UUID.toString(), "READ");
                result = UnityDeltaModelFixtures.awsCredsResponse(1L);
                times = 1;
            }
        };

        UnityBackedDeltaMetastore backed = metastore(client, propsWithVendedCredentials());

        // The storage location in the fixture is not reachable from a unit test, so we cannot let
        // the Delta Kernel load run. Stubbing the snapshot-load seam and asserting it received a
        // UnityMetastoreTable (which carries the loadTable response) plus a per-table AWS
        // CloudConfiguration proves UBDM#getTable threaded the dedup'd values through; the sentinel
        // then short-circuits so the caller skips snapshot conversion.
        new MockUp<UnityBackedDeltaMetastore>() {
            @Mock
            DeltaLakeSnapshot getLatestSnapshot(String dbName, String tableName, MetastoreTable mt,
                                                CloudConfiguration cc) {
                Assertions.assertInstanceOf(UnityMetastoreTable.class, mt);
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
                client.loadTable("main", "sales", "orders");
                times = 1;
                client.getTableCredentials("main", "sales", "orders", UnityDeltaModelFixtures.TABLE_UUID.toString(), "READ");
                times = 1;
            }
        };
    }

    /**
     * Sentinel raised from the snapshot-loader override in
     * {@link #testGetTableInvokesLoadTableExactlyOnce} so the test can verify it reached the loader
     * without paying for an actual Delta Kernel snapshot read.
     */
    private static final class SnapshotShortCircuit extends RuntimeException {
    }

    private static Stream<Arguments> nonManagedResponseCases() {
        return Stream.of(
                // Carried loadTable response is reused: no extra loadTable round-trip. Non-null
                // properties without the catalog-managed feature exercise the isCatalogManaged
                // feature check returning false.
                Arguments.of(new UnityMetastoreTable("sales", "orders", S3_LOCATION, 0L,
                                UnityDeltaModelFixtures.loadResponse(S3_LOCATION, 1L,
                                        ImmutableMap.of("delta.minReaderVersion", "1"))),
                        0),
                // No carried response: loadSnapshot fetches it via loadTable. Null properties
                // exercise the isCatalogManaged null short-circuit.
                Arguments.of(new MetastoreTable("sales", "orders", S3_LOCATION, 0L), 1));
    }

    @ParameterizedTest
    @MethodSource("nonManagedResponseCases")
    public void testLoadSnapshotReadsPublishedLogForNonManaged(MetastoreTable metastoreTable, int expectedLoadTableCalls,
                                                               @Mocked UnityCatalogClient client,
                                                               @Mocked DeltaLakeEngine engine,
                                                               @Mocked Table table,
                                                               @Mocked SnapshotImpl published) {
        new Expectations() {
            {
                client.loadTable("main", "sales", "orders");
                result = UnityDeltaModelFixtures.loadResponse(S3_LOCATION, 1L, null);
                minTimes = 0;
                Table.forPath((Engine) any, anyString);
                result = table;
                table.getLatestSnapshot((Engine) any);
                result = published;
            }
        };

        UnityBackedDeltaMetastore backed = metastore(client, propsWithVendedCredentials());
        SnapshotImpl result = backed.loadSnapshot(engine, S3_LOCATION, "sales", "orders", metastoreTable);

        Assertions.assertSame(published, result);
        new Verifications() {
            {
                client.loadTable(anyString, anyString, anyString);
                times = expectedLoadTableCalls;
            }
        };
    }

    private static Stream<Arguments> catalogManagedCases() {
        return Stream.of(
                // Commits arrive out of order with a known latest version: toCatalogCommits must
                // sort by version and the builder must receive withMaxCatalogVersion.
                Arguments.of(new DeltaCommit[] {
                        UnityDeltaModelFixtures.commit(2L, 222L, 2_000L),
                        UnityDeltaModelFixtures.commit(1L, 111L, 1_000L)}, 2L, new long[] {1L, 2L}, 1),
                // No commits but a known latest version: builder still receives withMaxCatalogVersion.
                Arguments.of(new DeltaCommit[] {}, 0L, new long[] {}, 1));
    }

    @ParameterizedTest
    @MethodSource("catalogManagedCases")
    public void testLoadSnapshotBuildsSnapshotForCatalogManaged(DeltaCommit[] commits, Long latestVersion,
                                                                long[] expectedVersions, int expectedMaxVersionCalls,
                                                                @Mocked UnityCatalogClient client,
                                                                @Mocked DeltaLakeEngine engine,
                                                                @Mocked TableManager tableManager,
                                                                @Mocked SnapshotBuilder builder,
                                                                @Mocked SnapshotImpl managedSnapshot) {
        DeltaLoadTableResponse response =
                UnityDeltaModelFixtures.catalogManagedLoadResponse(S3_LOCATION, latestVersion, commits);
        UnityMetastoreTable mt = new UnityMetastoreTable("sales", "orders", S3_LOCATION, 0L, response);

        new Expectations() {
            {
                TableManager.loadSnapshot(S3_LOCATION);
                result = builder;
                builder.withLogData((List<ParsedLogData>) any);
                result = builder;
                builder.withMaxCatalogVersion(anyLong);
                result = builder;
                minTimes = 0;
                builder.build((Engine) any);
                result = managedSnapshot;
            }
        };

        UnityBackedDeltaMetastore backed = metastore(client, propsWithVendedCredentials());
        SnapshotImpl result = backed.loadSnapshot(engine, S3_LOCATION, "sales", "orders", mt);

        Assertions.assertSame(managedSnapshot, result);
        String stagedDir = S3_LOCATION + "/_delta_log/_staged_commits/";
        new Verifications() {
            {
                List<ParsedLogData> logData;
                builder.withLogData(logData = withCapture());
                Assertions.assertEquals(expectedVersions.length, logData.size());
                for (int i = 0; i < expectedVersions.length; i++) {
                    final long version = expectedVersions[i];
                    DeltaCommit expected = Arrays.stream(commits)
                            .filter(c -> c.getVersion() == version).findFirst().orElseThrow();
                    ParsedLogData entry = logData.get(i);
                    Assertions.assertEquals(version, entry.getVersion());
                    Assertions.assertEquals(stagedDir + expected.getFileName(), entry.getFileStatus().getPath());
                    Assertions.assertEquals(expected.getFileSize().longValue(), entry.getFileStatus().getSize());
                    Assertions.assertEquals(expected.getFileModificationTimestamp().longValue(),
                            entry.getFileStatus().getModificationTime());
                }
                builder.withMaxCatalogVersion(anyLong);
                times = expectedMaxVersionCalls;
                client.loadTable(anyString, anyString, anyString);
                times = 0;
            }
        };
    }

    @Test
    public void testLoadSnapshotFailsWhenCatalogManagedHasNoLatestVersion(@Mocked UnityCatalogClient client,
                                                                          @Mocked DeltaLakeEngine engine) {
        DeltaLoadTableResponse response =
                UnityDeltaModelFixtures.catalogManagedLoadResponse(S3_LOCATION, null, new DeltaCommit[] {});
        UnityMetastoreTable mt = new UnityMetastoreTable("sales", "orders", S3_LOCATION, 0L, response);

        UnityBackedDeltaMetastore backed = metastore(client, propsWithVendedCredentials());
        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> backed.loadSnapshot(engine, S3_LOCATION, "sales", "orders", mt));
        Assertions.assertTrue(ex.getMessage().contains("no latest version"));
    }

    @Test
    public void testCreateDeltaLakeEngineUsesSharedCacheWhenEnabled(@Mocked UnityCatalogClient client,
                                                                    @Mocked UnityDeltaLakeMetaCache cache,
                                                                    @Mocked DeltaLakeEngine engine) {
        new Expectations() {
            {
                UnityDeltaLakeMetaCache.getSharedInstance();
                result = cache;
                cache.createEngine(anyString, (Configuration) any, (DeltaLakeCatalogProperties) any, true);
                result = engine;
            }
        };

        UnityBackedDeltaMetastore backed = metastore(client, propsWithVendedCredentials());
        Assertions.assertSame(engine, backed.createDeltaLakeEngine(new Configuration(false), true));
    }

    @Test
    public void testCreateDeltaLakeEngineBypassesSharedCacheWhenDisabled(@Mocked UnityCatalogClient client,
                                                                         @Mocked UnityDeltaLakeMetaCache cache) {
        new Expectations() {
            {
                UnityDeltaLakeMetaCache.getSharedInstance();
                result = cache;
            }
        };
        UnityCatalogProperties props = new UnityCatalogProperties(ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiTEST",
                "unity.catalog.name", "main",
                "unity.catalog.delta-cache.enabled", "false"));

        UnityBackedDeltaMetastore backed = metastore(client, props);
        DeltaLakeEngine created = backed.createDeltaLakeEngine(new Configuration(false), false);

        Assertions.assertNotNull(created);
        Assertions.assertTrue(created.isPerTableConfig(),
                "with the shared cache off, super must be invoked in per-table-config mode");
        new Verifications() {
            {
                cache.createEngine(anyString, (Configuration) any, (DeltaLakeCatalogProperties) any, anyBoolean);
                times = 0;
            }
        };
    }

    private static Stream<Arguments> vendingFailureEntryPoints() {
        return Stream.of(
                Arguments.of("getTable",
                        (Consumer<UnityBackedDeltaMetastore>) b -> b.getTable("sales", "orders")),
                Arguments.of("resolveTableCloudConfiguration",
                        (Consumer<UnityBackedDeltaMetastore>) b -> b.resolveTableCloudConfiguration("sales", "orders")));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("vendingFailureEntryPoints")
    public void testVendingFailurePropagatesTableName(String entryPoint, Consumer<UnityBackedDeltaMetastore> action,
                                                      @Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.loadTable("main", "sales", "orders");
                result = UnityDeltaModelFixtures.loadResponse(S3_LOCATION, 1_700_000_000_000L, null);
                client.getTableCredentials("main", "sales", "orders", UnityDeltaModelFixtures.TABLE_UUID.toString(), "READ");
                result = new StarRocksConnectorException("403 forbidden");
            }
        };

        UnityBackedDeltaMetastore backed = metastore(client, propsWithVendedCredentials());

        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> action.accept(backed));
        Assertions.assertTrue(ex.getMessage().contains("main.sales.orders"),
                "exception message must include the failing table name; was: " + ex.getMessage());
    }

    @Test
    public void testResolveTableCloudConfigurationNullWhenVendedDisabled(@Mocked UnityCatalogClient client) {
        UnityBackedDeltaMetastore backed = metastore(client, propsNoVendedCredentials());

        Assertions.assertNull(backed.resolveTableCloudConfiguration("sales", "orders"));
        Assertions.assertNull(backed.resolveTableCloudConfiguration("sales", "orders", S3_LOCATION, "uuid"));

        new Verifications() {
            {
                client.getTableCredentials(anyString, anyString, anyString, anyString, anyString);
                times = 0;
            }
        };
    }

    @Test
    public void testResolveTableCloudConfigurationCacheHitSkipsLoadTable(@Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.getTableCredentials("main", "sales", "orders", "uuid-123", "READ");
                result = UnityDeltaModelFixtures.awsCredsResponse(1L);
            }
        };

        UnityBackedDeltaMetastore backed = metastore(client, propsWithVendedCredentials());
        CloudConfiguration cc = backed.resolveTableCloudConfiguration("sales", "orders", S3_LOCATION, "uuid-123");

        Assertions.assertEquals(CloudType.AWS, cc.getCloudType());
        new Verifications() {
            {
                client.loadTable(anyString, anyString, anyString);
                times = 0;
            }
        };
    }

    private static Stream<Arguments> snapshotCacheBypassCases() {
        return Stream.of(
                Arguments.of("true", "60", false),
                Arguments.of("false", "60", true),
                Arguments.of("true", "0", true));
    }

    @ParameterizedTest(name = "cacheEnabled={0} ttlSec={1}")
    @MethodSource("snapshotCacheBypassCases")
    public void testIsSnapshotCacheBypassed(String cacheEnabled, String ttlSec, boolean expected,
                                            @Mocked UnityCatalogClient client) {
        UnityCatalogProperties props = new UnityCatalogProperties(ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiTEST",
                "unity.catalog.name", "main",
                "unity.catalog.cache.enabled", cacheEnabled,
                "unity.catalog.cache.ttl-sec", ttlSec));

        Assertions.assertEquals(expected, metastore(client, props).isSnapshotCacheBypassed());
    }

    @Test
    public void testSessionVariableBypassesUnitySnapshotCache(@Mocked UnityCatalogClient client) {
        UnityCatalogProperties props = new UnityCatalogProperties(ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiTEST",
                "unity.catalog.name", "main",
                "unity.catalog.cache.enabled", "true",
                "unity.catalog.cache.ttl-sec", "60"));

        ConnectContext context = new ConnectContext();
        context.getSessionVariable().setEnableUnityTableSnapshotCache(false);
        context.setThreadLocalInfo();
        try {
            Assertions.assertTrue(metastore(client, props).isSnapshotCacheBypassed());
        } finally {
            ConnectContext.remove();
        }
    }

    @Test
    public void testUnityBackedMetastoresShareDeltaMetaCache(@Mocked UnityCatalogClient client) {
        UnityCatalogProperties props = propsWithVendedCredentials();
        UnityBackedDeltaMetastore first = metastore(client, props);
        UnityBackedDeltaMetastore second = metastore(client, props);

        Assertions.assertSame(first.getUnityMetaCache(), second.getUnityMetaCache(),
                "Unity-backed Delta catalogs must share one FE-level JSON/checkpoint cache");
    }

    @Test
    public void testInvalidateAllDoesNotClearSharedDeltaMetaCache(@Mocked UnityCatalogClient client) {
        UnityCatalogProperties props = propsWithVendedCredentials();
        UnityBackedDeltaMetastore backed = metastore(client, props);
        UnityDeltaLakeMetaCache cache = backed.getUnityMetaCache();
        DeltaLakeFileStatus status = DeltaLakeFileStatus.of(
                io.delta.kernel.utils.FileStatus.of("s3://bucket/shared-unity-cache-test.json", 123, 456));

        String scope = props.getPrincipalScope();
        cache.getJsonCache(scope, new Configuration(false), false).put(status, Lists.newArrayList());
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

        cache.getJsonCache("principal-a", new Configuration(false), false).put(status, Lists.newArrayList());

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
}
