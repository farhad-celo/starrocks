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

import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.starrocks.catalog.Database;
import com.starrocks.catalog.DeltaLakeTable;
import com.starrocks.catalog.Table;
import com.starrocks.connector.DatabaseTableName;
import com.starrocks.connector.MetastoreType;
import com.starrocks.connector.exception.StarRocksConnectorException;
import com.starrocks.connector.hive.HiveMetaClient;
import com.starrocks.connector.hive.HiveMetastore;
import com.starrocks.connector.hive.HiveMetastoreTest;
import com.starrocks.connector.hive.IHiveMetastore;
import com.starrocks.connector.metastore.MetastoreTable;
import com.starrocks.mysql.MysqlCommand;
import com.starrocks.qe.ConnectContext;
import com.starrocks.sql.analyzer.SemanticException;
import io.delta.kernel.Operation;
import io.delta.kernel.Snapshot;
import io.delta.kernel.TransactionBuilder;
import io.delta.kernel.engine.Engine;
import io.delta.kernel.exceptions.CheckpointAlreadyExistsException;
import io.delta.kernel.exceptions.TableNotFoundException;
import io.delta.kernel.internal.SnapshotImpl;
import io.delta.kernel.internal.TableImpl;
import mockit.Expectations;
import mockit.Mock;
import mockit.MockUp;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.hive.metastore.api.NoSuchObjectException;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.jupiter.api.Assertions.assertThrows;

public class CachingDeltaLakeMetastoreTest {
    private HiveMetaClient client;
    private DeltaLakeMetastore metastore;
    private ExecutorService executor;
    private long expireAfterWriteSec = 30;
    private long refreshAfterWriteSec = -1;

    @BeforeEach
    public void setUp() throws Exception {
        client = new HiveMetastoreTest.MockedHiveMetaClient();
        IHiveMetastore hiveMetastore = new HiveMetastore(client, "delta0", MetastoreType.HMS);
        metastore = new HMSBackedDeltaMetastore("delta0", hiveMetastore, new Configuration(),
                new DeltaLakeCatalogProperties(Maps.newHashMap()));
        executor = Executors.newFixedThreadPool(5);
    }

    @Test
    public void testGetAllDatabaseNames() {
        CachingDeltaLakeMetastore cachingDeltaLakeMetastore =
                CachingDeltaLakeMetastore.createCatalogLevelInstance(metastore, executor, expireAfterWriteSec,
                        refreshAfterWriteSec, 100);

        List<String> databaseNames = cachingDeltaLakeMetastore.getAllDatabaseNames();
        Assertions.assertEquals(Lists.newArrayList("db1", "db2"), databaseNames);
        CachingDeltaLakeMetastore queryLevelCache = CachingDeltaLakeMetastore.
                createQueryLevelInstance(cachingDeltaLakeMetastore, 100);
        Assertions.assertEquals(Lists.newArrayList("db1", "db2"), queryLevelCache.getAllDatabaseNames());
    }

    @Test
    public void testGetAllTableNames() {
        CachingDeltaLakeMetastore cachingDeltaLakeMetastore =
                CachingDeltaLakeMetastore.createCatalogLevelInstance(metastore, executor, expireAfterWriteSec,
                        refreshAfterWriteSec, 100);

        List<String> tableNames = cachingDeltaLakeMetastore.getAllTableNames("db1");
        Assertions.assertEquals(Lists.newArrayList("table1", "table2"), tableNames);
        CachingDeltaLakeMetastore queryLevelCache = CachingDeltaLakeMetastore.
                createQueryLevelInstance(cachingDeltaLakeMetastore, 100);
        Assertions.assertEquals(Lists.newArrayList("table1", "table2"), queryLevelCache.getAllTableNames("db1"));
    }

    @Test
    public void testGetDb() {
        CachingDeltaLakeMetastore cachingDeltaLakeMetastore =
                CachingDeltaLakeMetastore.createCatalogLevelInstance(metastore, executor, expireAfterWriteSec,
                        refreshAfterWriteSec, 100);

        Database database = cachingDeltaLakeMetastore.getDb("db1");
        Assertions.assertEquals("db1", database.getFullName());
    }

    @Test
    public void testGetTable() {
        new MockUp<CachingDeltaLakeMetastore>() {
            @mockit.Mock
            public DeltaLakeSnapshot getCachedSnapshot(DatabaseTableName databaseTableName) {
                return new DeltaLakeSnapshot("db1", "table1", null, null,
                        123, "s3://bucket/path/to/table");
            }
        };

        new MockUp<DeltaUtils>() {
            @mockit.Mock
            public DeltaLakeTable convertDeltaSnapshotToSRTable(String catalog, DeltaLakeSnapshot snapshot) {
                return new DeltaLakeTable(1, "delta0", "db1", "table1",
                        Lists.newArrayList(), Lists.newArrayList("ts"), null,
                        "s3://bucket/path/to/table", null, 0);
            }
        };

        CachingDeltaLakeMetastore cachingDeltaLakeMetastore =
                CachingDeltaLakeMetastore.createCatalogLevelInstance(metastore, executor, expireAfterWriteSec,
                        refreshAfterWriteSec, 100);

        Table table = cachingDeltaLakeMetastore.getTable("db1", "table1");
        Assertions.assertTrue(table instanceof DeltaLakeTable);
        DeltaLakeTable deltaLakeTable = (DeltaLakeTable) table;
        Assertions.assertEquals("db1", deltaLakeTable.getCatalogDBName());
        Assertions.assertEquals("table1", deltaLakeTable.getCatalogTableName());
        Assertions.assertEquals("s3://bucket/path/to/table", deltaLakeTable.getTableLocation());
    }

    @Test
    public void testGetLatestSnapshot1() {
        Throwable exception = assertThrows(SemanticException.class, () -> {
            new MockUp<HMSBackedDeltaMetastore>() {
                @mockit.Mock
                public MetastoreTable getMetastoreTable(String dbName, String tableName) {
                    return new MetastoreTable("db1", "table1", "s3://bucket/path/to/table", 123);
                }
            };

            new MockUp<TableImpl>() {
                @mockit.Mock
                public io.delta.kernel.Table forPath(Engine engine, String path) {
                    throw new TableNotFoundException("Table not found");
                }
            };

            metastore.getLatestSnapshot("db1", "table1");
        });
        assertThat(exception.getMessage(), containsString("Failed to find Delta table for delta0.db1.table1"));
    }

    @Test
    public void testGetLatestSnapshot2() {
        Throwable exception = assertThrows(RuntimeException.class, () -> {
            io.delta.kernel.Table table = new io.delta.kernel.Table() {
                public io.delta.kernel.Table forPath(Engine engine, String path) {
                    return this;
                }

                @Override
                public String getPath(Engine engine) {
                    return null;
                }

                @Override
                public SnapshotImpl getLatestSnapshot(Engine engine) {
                    throw new RuntimeException("Failed to get latest snapshot");
                }

                @Override
                public Snapshot getSnapshotAsOfVersion(Engine engine, long versionId) throws TableNotFoundException {
                    return null;
                }

                @Override
                public Snapshot getSnapshotAsOfTimestamp(Engine engine, long millisSinceEpochUTC)
                        throws TableNotFoundException {
                    return null;
                }

                @Override
                public TransactionBuilder createTransactionBuilder(Engine engine, String engineInfo, Operation operation) {
                    return null;
                }

                @Override
                public void checkpoint(Engine engine, long version)
                        throws TableNotFoundException, CheckpointAlreadyExistsException, IOException {
                }
            };

            new MockUp<TableImpl>() {
                @Mock
                public io.delta.kernel.Table forPath(Engine engine, String path) {
                    return table;
                }
            };
            metastore.getLatestSnapshot("db1", "table1");
        });
        assertThat(exception.getMessage(), containsString("Failed to get latest snapshot"));
    }

    @Test
    public void testTableExists() {
        CachingDeltaLakeMetastore cachingDeltaLakeMetastore =
                CachingDeltaLakeMetastore.createCatalogLevelInstance(metastore, executor, expireAfterWriteSec,
                        refreshAfterWriteSec, 100);

        Assertions.assertTrue(cachingDeltaLakeMetastore.tableExists("db1", "table1"));
    }

    @Test
    public void testRefreshTable() {
        new Expectations(metastore) {
            {
                metastore.getLatestSnapshot(anyString, "notExistTbl");
                minTimes = 0;
                Throwable targetException = new NoSuchObjectException("no such obj");
                Throwable e = new InvocationTargetException(targetException);
                result = new StarRocksConnectorException("table not exist", e);
            }
        };
        CachingDeltaLakeMetastore cachingDeltaLakeMetastore = new CachingDeltaLakeMetastore(
                metastore, executor, expireAfterWriteSec, refreshAfterWriteSec, 1000);
        try {
            cachingDeltaLakeMetastore.refreshTable("db1", "notExistTbl", true);
            Assertions.fail();
        } catch (Exception e) {
            Assertions.assertTrue(e instanceof StarRocksConnectorException);
            Assertions.assertTrue(e.getMessage().contains("invalidated cache"));
        }

        new MockUp<DeltaLakeMetastore>() {
            @mockit.Mock
            public DeltaLakeSnapshot getLatestSnapshot(String dbName, String tableName) {
                return new DeltaLakeSnapshot("db1", "table1", null, null,
                        123, "s3://bucket/path/to/table");
            }
        };

        new MockUp<DeltaUtils>() {
            @mockit.Mock
            public DeltaLakeTable convertDeltaSnapshotToSRTable(String catalog, DeltaLakeSnapshot snapshot) {
                return new DeltaLakeTable(1, "delta0", "db1", "tbl1",
                        Lists.newArrayList(), Lists.newArrayList("ts"), null,
                        "s3://bucket/path/to/table", null, 0);
            }
        };

        try {
            cachingDeltaLakeMetastore.refreshTable("db1", "tbl1", true);
        } catch (Exception e) {
            Assertions.fail();
        }
    }

    @Test
    public void testRefreshTablePropagatesToDelegate() {
        AtomicInteger delegateRefreshCount = new AtomicInteger();
        DeltaLakeMetastore delegateSpy = new HMSBackedDeltaMetastore(
                "delta0",
                new HiveMetastore(client, "delta0", MetastoreType.HMS),
                new Configuration(),
                new DeltaLakeCatalogProperties(Maps.newHashMap())) {
            @Override
            public void refreshTable(String dbName, String tableName) {
                delegateRefreshCount.incrementAndGet();
            }

            @Override
            public DeltaLakeSnapshot getLatestSnapshot(String dbName, String tableName) {
                return new DeltaLakeSnapshot(dbName, tableName, null, null,
                        123, "s3://bucket/path/to/table");
            }
        };

        CachingDeltaLakeMetastore caching = new CachingDeltaLakeMetastore(
                delegateSpy, executor, expireAfterWriteSec, refreshAfterWriteSec, 1000);
        caching.refreshTable("db1", "table1", true);

        Assertions.assertEquals(1, delegateRefreshCount.get(),
                "CachingDeltaLakeMetastore.refreshTable must propagate to delegate.refreshTable");
    }

    @Test
    public void testRefreshTableInterfaceOverrideInvalidatesAndPropagates() {
        new MockUp<DeltaLakeMetastore>() {
            @mockit.Mock
            public DeltaLakeSnapshot getLatestSnapshot(String dbName, String tableName) {
                return new DeltaLakeSnapshot(dbName, tableName, null, null,
                        123, "s3://bucket/path/to/table");
            }
        };

        CachingDeltaLakeMetastore inner = new CachingDeltaLakeMetastore(
                metastore, executor, expireAfterWriteSec, refreshAfterWriteSec, 1000);
        inner.refreshTable("db1", "table1", true);
        Assertions.assertEquals(1L, inner.estimateCount().get("tableCache"));

        CachingDeltaLakeMetastore outer = CachingDeltaLakeMetastore.createQueryLevelInstance(inner, 100);
        outer.refreshTable("db1", "table1");

        Assertions.assertEquals(0L, inner.estimateCount().get("tableCache"));
    }

    @Test
    public void testInvalidateAll() {
        new MockUp<CachingDeltaLakeMetastore>() {
            @mockit.Mock
            public DeltaLakeSnapshot getCachedSnapshot(DatabaseTableName databaseTableName) {
                return new DeltaLakeSnapshot("db1", "table1", null, null,
                        123, "s3://bucket/path/to/table");
            }
        };

        new MockUp<DeltaUtils>() {
            @mockit.Mock
            public DeltaLakeTable convertDeltaSnapshotToSRTable(String catalog, DeltaLakeSnapshot snapshot) {
                return new DeltaLakeTable(1, "delta0", "db1", "table1",
                        Lists.newArrayList(), Lists.newArrayList("ts"), null,
                        "s3://bucket/path/to/table", null, 123);
            }
        };

        CachingDeltaLakeMetastore cachingDeltaLakeMetastore =
                CachingDeltaLakeMetastore.createCatalogLevelInstance(metastore, executor, expireAfterWriteSec,
                        refreshAfterWriteSec, 100);

        cachingDeltaLakeMetastore.getTable("db1", "table1");
        Assertions.assertFalse(cachingDeltaLakeMetastore.estimateCount().isEmpty());

        cachingDeltaLakeMetastore.invalidateAll();

        Map<String, Long> count = cachingDeltaLakeMetastore.estimateCount();
        Assertions.assertTrue(count.containsKey("tableCache"));
        Assertions.assertEquals(0L, count.get("tableCache"));
    }

    @Test
    public void testInvalidateTable() {
        new MockUp<CachingDeltaLakeMetastore>() {
            @mockit.Mock
            public DeltaLakeSnapshot getCachedSnapshot(DatabaseTableName databaseTableName) {
                return new DeltaLakeSnapshot("db1", "table1", null, null,
                        123, "s3://bucket/path/to/table");
            }
        };

        new MockUp<DeltaUtils>() {
            @mockit.Mock
            public DeltaLakeTable convertDeltaSnapshotToSRTable(String catalog, DeltaLakeSnapshot snapshot) {
                return new DeltaLakeTable(1, "delta0", "db1", "table1",
                        Lists.newArrayList(), Lists.newArrayList("ts"), null,
                        "s3://bucket/path/to/table", null, 123);
            }
        };

        CachingDeltaLakeMetastore cachingDeltaLakeMetastore =
                CachingDeltaLakeMetastore.createCatalogLevelInstance(metastore, executor, expireAfterWriteSec,
                        refreshAfterWriteSec, 100);

        Table table = cachingDeltaLakeMetastore.getTable("db1", "table1");
        Assertions.assertNotNull(table);

        cachingDeltaLakeMetastore.invalidateTable("db1", "table1");

        Map<String, Long> count = cachingDeltaLakeMetastore.estimateCount();
        Assertions.assertTrue(count.containsKey("tableCache"));
        Assertions.assertEquals(0L, count.get("tableCache"));
    }

    @Test
    public void testGetCachedSnapshot() {
        new MockUp<ConnectContext>() {
            @mockit.Mock
            public static ConnectContext get() {
                ConnectContext context = new ConnectContext();
                context.setCommand(MysqlCommand.COM_QUERY);
                return context;
            }
        };

        DeltaLakeSnapshot snapshot = new DeltaLakeSnapshot("db1", "table1", null, null,
                123, "s3://bucket/path/to/table");

        new MockUp<CachingDeltaLakeMetastore>() {
            @mockit.Mock
            public DeltaLakeSnapshot getCachedSnapshot(DatabaseTableName databaseTableName) {
                return snapshot;
            }
        };

        CachingDeltaLakeMetastore cachingDeltaLakeMetastore =
                CachingDeltaLakeMetastore.createCatalogLevelInstance(metastore, executor, expireAfterWriteSec,
                        refreshAfterWriteSec, 100);

        DeltaLakeSnapshot result = cachingDeltaLakeMetastore.getCachedSnapshot(DatabaseTableName.of("db1", "table1"));
        Assertions.assertEquals(snapshot, result);
    }

    @Test
    public void testRefreshTableUsesSnapshotCache() {
        DeltaLakeSnapshot snapshot = new DeltaLakeSnapshot("db1", "table1", null, null,
                123, "s3://bucket/path/to/table");

        new MockUp<DeltaLakeMetastore>() {
            @mockit.Mock
            public DeltaLakeSnapshot getLatestSnapshot(String dbName, String tableName) {
                return snapshot;
            }
        };

        CachingDeltaLakeMetastore cachingDeltaLakeMetastore =
                new CachingDeltaLakeMetastore(metastore, executor, expireAfterWriteSec, refreshAfterWriteSec, 1000);

        try {
            cachingDeltaLakeMetastore.refreshTable("db1", "table1", true);
            Assertions.assertTrue(true);
        } catch (Exception e) {
            Assertions.fail("Refresh table should succeed");
        }
    }

    @Test
    public void testCacheMemoryUsage() {
        new MockUp<CachingDeltaLakeMetastore>() {
            @mockit.Mock
            public DeltaLakeSnapshot getCachedSnapshot(DatabaseTableName databaseTableName) {
                return new DeltaLakeSnapshot("db1", "table1", null, null,
                        123, "s3://bucket/path/to/table");
            }
        };

        new MockUp<DeltaUtils>() {
            @mockit.Mock
            public DeltaLakeTable convertDeltaSnapshotToSRTable(String catalog, DeltaLakeSnapshot snapshot) {
                return new DeltaLakeTable(1, "delta0", "db1", "table1",
                        Lists.newArrayList(), Lists.newArrayList("ts"), null,
                        "s3://bucket/path/to/table", null, 0);
            }
        };

        CachingDeltaLakeMetastore cachingDeltaLakeMetastore =
                CachingDeltaLakeMetastore.createCatalogLevelInstance(metastore, executor, expireAfterWriteSec,
                        refreshAfterWriteSec, 100);

        cachingDeltaLakeMetastore.getDb("db1");
        cachingDeltaLakeMetastore.getTable("db1", "table1");

        Assertions.assertTrue(cachingDeltaLakeMetastore.estimateSize() > 0);
        Assertions.assertFalse(cachingDeltaLakeMetastore.estimateCount().isEmpty());
        Assertions.assertTrue(cachingDeltaLakeMetastore.estimateCount().containsKey("databaseCache"));
        Assertions.assertTrue(cachingDeltaLakeMetastore.estimateCount().containsKey("tableCache"));
    }

    @Test
    public void testInvalidateTableUsesSnapshotCache() {
        DeltaLakeSnapshot snapshot = new DeltaLakeSnapshot("db1", "table1", null, null,
                123, "s3://bucket/path/to/table");

        new MockUp<DeltaLakeMetastore>() {
            @mockit.Mock
            public DeltaLakeSnapshot getLatestSnapshot(String dbName, String tableName) {
                return snapshot;
            }
        };

        CachingDeltaLakeMetastore cachingDeltaLakeMetastore =
                new CachingDeltaLakeMetastore(metastore, executor, expireAfterWriteSec, refreshAfterWriteSec, 1000);

        cachingDeltaLakeMetastore.refreshTable("db1", "table1", true);

        Map<String, Long> count = cachingDeltaLakeMetastore.estimateCount();
        Assertions.assertTrue(count.containsKey("tableCache"));
        Assertions.assertEquals(1L, count.get("tableCache"));

        cachingDeltaLakeMetastore.invalidateTable("db1", "table1");

        count = cachingDeltaLakeMetastore.estimateCount();
        Assertions.assertTrue(count.containsKey("tableCache"));
        Assertions.assertEquals(0L, count.get("tableCache"));
    }

    @Test
    public void testGetTableUsesSnapshotCacheWhenDelegateAllows() {
        Assertions.assertFalse(metastore.isSnapshotCacheBypassed(),
                "HMSBackedDeltaMetastore should not bypass the snapshot cache by default");

        new MockUp<DeltaLakeMetastore>() {
            @mockit.Mock
            public DeltaLakeSnapshot getLatestSnapshot(String dbName, String tableName) {
                return new DeltaLakeSnapshot(dbName, tableName, null, null,
                        123, "s3://bucket/path/to/table");
            }
        };
        new MockUp<DeltaUtils>() {
            @mockit.Mock
            public DeltaLakeTable convertDeltaSnapshotToSRTable(String catalog, DeltaLakeSnapshot snapshot) {
                return new DeltaLakeTable(1, "delta0", snapshot.getDbName(), snapshot.getTableName(),
                        Lists.newArrayList(), Lists.newArrayList("ts"), null,
                        "s3://bucket/path/to/table", null, 0);
            }
        };

        CachingDeltaLakeMetastore caching = CachingDeltaLakeMetastore.createCatalogLevelInstance(
                metastore, executor, expireAfterWriteSec, refreshAfterWriteSec, 100);
        Table table = caching.getTable("db1", "table1");

        Assertions.assertNotNull(table);
        Map<String, Long> count = caching.estimateCount();
        Assertions.assertTrue(count.containsKey("tableCache"));
        Assertions.assertEquals(1L, count.get("tableCache"));
    }

    @Test
    public void testGetTableBypassesSnapshotCacheWhenDelegateOptsOut() {
        IHiveMetastore hiveMetastore = new HiveMetastore(client, "delta0", MetastoreType.HMS);
        DeltaLakeTable sentinel = new DeltaLakeTable(42, "delta0", "db1", "table1",
                Lists.newArrayList(), Lists.newArrayList("ts"), null,
                "s3://bucket/path/to/table", null, 0);
        HMSBackedDeltaMetastore bypassDelegate = new HMSBackedDeltaMetastore(
                "delta0", hiveMetastore, new Configuration(),
                new DeltaLakeCatalogProperties(Maps.newHashMap())) {
            @Override
            public boolean isSnapshotCacheBypassed() {
                return true;
            }

            @Override
            public DeltaLakeTable getTable(String dbName, String tableName) {
                return sentinel;
            }
        };

        CachingDeltaLakeMetastore caching = CachingDeltaLakeMetastore.createCatalogLevelInstance(
                bypassDelegate, executor, expireAfterWriteSec, refreshAfterWriteSec, 100);
        Table table = caching.getTable("db1", "table1");

        Assertions.assertSame(sentinel, table, "bypass path must return the delegate's value");
        Map<String, Long> count = caching.estimateCount();
        Assertions.assertTrue(count.containsKey("tableCache"));
        Assertions.assertEquals(0L, count.get("tableCache"));
    }

    @Test
    public void testLayeredQueryCacheStaysActiveWhileCatalogCacheBypasses() {
        // Mirrors the production layering: per-query CachingDeltaLakeMetastore around a
        // catalog-level CachingDeltaLakeMetastore around the real metastore. The catalog
        // layer must honor the underlying bypass (so cross-query state is not reused with
        // stale embedded credentials) but the per-query cache stays active so intra-query
        // planning lookups don't multiply upstream RPCs.
        AtomicInteger upstreamLoads = new AtomicInteger();
        new MockUp<DeltaLakeMetastore>() {
            @Mock
            public DeltaLakeSnapshot getLatestSnapshot(String dbName, String tableName) {
                upstreamLoads.incrementAndGet();
                return new DeltaLakeSnapshot(dbName, tableName, null, null,
                        123, "s3://bucket/path/to/table");
            }
        };
        new MockUp<DeltaUtils>() {
            @Mock
            public DeltaLakeTable convertDeltaSnapshotToSRTable(String catalog, DeltaLakeSnapshot snapshot) {
                return new DeltaLakeTable(1, "delta0", snapshot.getDbName(), snapshot.getTableName(),
                        Lists.newArrayList(), Lists.newArrayList("ts"), null,
                        "s3://bucket/path/to/table", null, 0);
            }
        };

        IHiveMetastore hiveMetastore = new HiveMetastore(client, "delta0", MetastoreType.HMS);
        HMSBackedDeltaMetastore bypassDelegate = new HMSBackedDeltaMetastore(
                "delta0", hiveMetastore, new Configuration(),
                new DeltaLakeCatalogProperties(Maps.newHashMap())) {
            @Override
            public boolean isSnapshotCacheBypassed() {
                return true;
            }
        };

        CachingDeltaLakeMetastore catalogCache = CachingDeltaLakeMetastore.createCatalogLevelInstance(
                bypassDelegate, executor, expireAfterWriteSec, refreshAfterWriteSec, 100);
        CachingDeltaLakeMetastore queryCache = CachingDeltaLakeMetastore.createQueryLevelInstance(
                catalogCache, 100);

        // Two getTable calls on the same query layer must dedup -> upstream sees one load.
        Table first = queryCache.getTable("db1", "table1");
        Table second = queryCache.getTable("db1", "table1");

        Assertions.assertNotNull(first);
        Assertions.assertNotNull(second);
        // The DeltaLakeTable wrapper is rebuilt by DeltaUtils per call -- proving snapshot
        // dedup at the kernel layer (one upstream load) is the meaningful intra-query
        // invariant, not pointer equality of the wrapper.
        Assertions.assertEquals(1, upstreamLoads.get(),
                "intra-query snapshot loads must not multiply when the catalog layer bypasses");

        Map<String, Long> queryCount = queryCache.estimateCount();
        Assertions.assertTrue(queryCount.containsKey("tableCache"));
        Assertions.assertEquals(1L, queryCount.get("tableCache"),
                "per-query snapshot cache must stay active even when the inner layer bypasses");
        Map<String, Long> catalogCount = catalogCache.estimateCount();
        Assertions.assertTrue(catalogCount.containsKey("tableCache"));
        Assertions.assertEquals(0L, catalogCount.get("tableCache"),
                "catalog-level snapshot cache must stay empty when the underlying metastore opts out");
    }
}
