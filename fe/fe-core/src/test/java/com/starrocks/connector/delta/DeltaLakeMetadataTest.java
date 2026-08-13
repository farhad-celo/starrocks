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
import com.starrocks.catalog.DeltaLakeTable;
import com.starrocks.catalog.Table;
import com.starrocks.common.jmockit.Deencapsulation;
import com.starrocks.connector.ConnectorMetadatRequestContext;
import com.starrocks.connector.ConnectorProperties;
import com.starrocks.connector.ConnectorTableVersion;
import com.starrocks.connector.ConnectorType;
import com.starrocks.connector.DatabaseTableName;
import com.starrocks.connector.GetRemoteFilesParams;
import com.starrocks.connector.HdfsEnvironment;
import com.starrocks.connector.MetastoreType;
import com.starrocks.connector.PointerType;
import com.starrocks.connector.PredicateSearchKey;
import com.starrocks.connector.RemoteFileInfo;
import com.starrocks.connector.RemoteFileInfoSource;
import com.starrocks.connector.TableVersionRange;
import com.starrocks.connector.exception.StarRocksConnectorException;
import com.starrocks.connector.hive.HiveMetaClient;
import com.starrocks.connector.hive.HiveMetastore;
import com.starrocks.connector.hive.HiveMetastoreTest;
import com.starrocks.connector.hive.IHiveMetastore;
import com.starrocks.qe.ConnectContext;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import com.starrocks.sql.optimizer.validate.ValidateException;
import io.delta.kernel.Scan;
import io.delta.kernel.ScanBuilder;
import io.delta.kernel.data.ColumnVector;
import io.delta.kernel.data.ColumnarBatch;
import io.delta.kernel.data.FilteredColumnarBatch;
import io.delta.kernel.defaults.internal.data.DefaultColumnarBatch;
import io.delta.kernel.defaults.internal.data.vector.DefaultBinaryVector;
import io.delta.kernel.defaults.internal.data.vector.DefaultMapVector;
import io.delta.kernel.defaults.internal.data.vector.DefaultStructVector;
import io.delta.kernel.engine.Engine;
import io.delta.kernel.internal.ScanBuilderImpl;
import io.delta.kernel.internal.SnapshotImpl;
import io.delta.kernel.internal.actions.Metadata;
import io.delta.kernel.internal.actions.Protocol;
import io.delta.kernel.types.BasePrimitiveType;
import io.delta.kernel.types.DataType;
import io.delta.kernel.types.MapType;
import io.delta.kernel.types.StringType;
import io.delta.kernel.types.StructField;
import io.delta.kernel.types.StructType;
import io.delta.kernel.utils.CloseableIterator;
import mockit.Expectations;
import mockit.Mock;
import mockit.MockUp;
import mockit.Mocked;
import org.apache.hadoop.conf.Configuration;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;

import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.concurrent.atomic.AtomicLong;
import java.util.stream.Stream;

public class DeltaLakeMetadataTest {
    private HiveMetaClient client;
    private DeltaLakeMetastore metastore;
    private DeltaLakeMetadata deltaLakeMetadata;

    @BeforeEach
    public void setUp() throws Exception {
        HdfsEnvironment hdfsEnvironment = new HdfsEnvironment(Maps.newHashMap());

        client = new HiveMetastoreTest.MockedHiveMetaClient();
        IHiveMetastore hiveMetastore = new HiveMetastore(client, "delta0", MetastoreType.HMS);

        metastore = new HMSBackedDeltaMetastore("delta0", hiveMetastore,
                new Configuration(), new DeltaLakeCatalogProperties(Maps.newHashMap()));
        DeltaMetastoreOperations deltaOps = new DeltaMetastoreOperations(
                CachingDeltaLakeMetastore.createQueryLevelInstance(metastore, 10000), false,
                MetastoreType.HMS);

        deltaLakeMetadata = new DeltaLakeMetadata(hdfsEnvironment, "delta0", deltaOps, null,
                new ConnectorProperties(ConnectorType.DELTALAKE));
    }

    @Test
    public void testListDbNames() {
        List<String> dbNames = deltaLakeMetadata.listDbNames(new ConnectContext());
        Assertions.assertEquals(2, dbNames.size());
        Assertions.assertEquals("db1", dbNames.get(0));
        Assertions.assertEquals("db2", dbNames.get(1));
    }

    @Test
    public void testListTableNames() {
        List<String> tableNames = deltaLakeMetadata.listTableNames(new ConnectContext(), "db1");
        Assertions.assertEquals(2, tableNames.size());
        Assertions.assertEquals("table1", tableNames.get(0));
        Assertions.assertEquals("table2", tableNames.get(1));
    }

    @Test
    public void testListPartitionNames(@Mocked SnapshotImpl snapshot, @Mocked ScanBuilder scanBuilder,
                                       @Mocked Scan scan) {
        new MockUp<DeltaLakeMetastore>() {
            @mockit.Mock
            public DeltaLakeSnapshot getLatestSnapshot(String dbName, String tableName) {
                return new DeltaLakeSnapshot("db1", "table1", null, null,
                        123, "s3://bucket/path/to/table");
            }
        };

        new MockUp<DeltaUtils>() {
            @Mock
            public DeltaLakeTable convertDeltaSnapshotToSRTable(String catalog, DeltaLakeSnapshot deltaLakeSnapshot) {
                return new DeltaLakeTable(1, "delta0", "db1", "table1",
                        Lists.newArrayList(), Lists.newArrayList("ts"), snapshot,
                        "s3://bucket/path/to/table", null, 0);
            }
        };

        // mock schema:
        // struct<add:struct<path:string,partitionValues:map<string,string>>>
        List<FilteredColumnarBatch> filteredColumnarBatches = Lists.newArrayList();

        ColumnVector[] addFileCols = new ColumnVector[2];
        addFileCols[0] = new DefaultBinaryVector(BasePrimitiveType.createPrimitive("string"),
                3, new byte[][] {new byte[] {'0', '0', '0', '0'},
                    new byte[] {'0', '0', '0', '1'}, new byte[] {'0', '0', '0', '2'}});

        int[] offsets = new int[] {0, 1, 2, 3};
        DataType mapType = new MapType(StringType.STRING, StringType.STRING, true);
        addFileCols[1] = new DefaultMapVector(3, mapType, Optional.empty(), offsets,
                new DefaultBinaryVector(BasePrimitiveType.createPrimitive("string"),
                        3, new byte[][] {new byte[] {'t', 's'}, new byte[] {'t', 's'}, new byte[] {'t', 's'}}),
                new DefaultBinaryVector(BasePrimitiveType.createPrimitive("string"),
                        3, new byte[][] {new byte[] {'1', '9', '9', '9'}, new byte[] {'2', '0', '0', '0'},
                            new byte[] {'2', '0', '0', '1'}})
        );
        // addFile schema, here we only care about the partitionValues, so not use all fields
        StructType addFileSchema = new StructType(Lists.newArrayList(
                new StructField("path", BasePrimitiveType.createPrimitive("string"), true),
                new StructField("partitionValues", mapType, true)));
        DefaultStructVector addFile = new DefaultStructVector(3, addFileSchema, Optional.empty(), addFileCols);
        // construct a columnar batch which only contains addFile
        ColumnarBatch columnarBatch = new DefaultColumnarBatch(3,
                new StructType(Lists.newArrayList(new StructField("add", addFileSchema, true))),
                new DefaultStructVector[] {addFile});

        FilteredColumnarBatch filteredColumnarBatch = new FilteredColumnarBatch(columnarBatch, Optional.empty());
        filteredColumnarBatches.add(filteredColumnarBatch);
        CloseableIterator<FilteredColumnarBatch> scanFilesAsBatches = new CloseableIterator<FilteredColumnarBatch>() {
            private int index = 0;

            @Override
            public boolean hasNext() {
                return index < filteredColumnarBatches.size();
            }

            @Override
            public FilteredColumnarBatch next() {
                return filteredColumnarBatches.get(index++);
            }

            @Override
            public void close() {
            }
        };

        new Expectations() {
            {
                snapshot.getScanBuilder();
                result = scanBuilder;
                minTimes = 0;

                scanBuilder.build();
                result = scan;
                minTimes = 0;

                scan.getScanFiles((Engine) any);
                result = scanFilesAsBatches;
                minTimes = 0;
            }
        };
        List<String> partitionNames =
                deltaLakeMetadata.listPartitionNames("db1", "table1", ConnectorMetadatRequestContext.DEFAULT);
        Assertions.assertEquals(3, partitionNames.size());
        Assertions.assertEquals("ts=1999", partitionNames.get(0));
        Assertions.assertEquals("ts=2000", partitionNames.get(1));
        Assertions.assertEquals("ts=2001", partitionNames.get(2));
    }

    @Test
    public void testTableExists() {
        Assertions.assertTrue(deltaLakeMetadata.tableExists(new ConnectContext(), "db1", "table1"));
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
                return new DeltaLakeTable(1, "delta0", "db1", "table1", Lists.newArrayList(),
                        Lists.newArrayList("col1"), null, "path/to/table", null, 0);
            }
        };
        DeltaLakeTable deltaTable = (DeltaLakeTable) deltaLakeMetadata.getTable(new ConnectContext(), "db1", "table1");
        Assertions.assertNotNull(deltaTable);
        Assertions.assertEquals("table1", deltaTable.getName());
        Assertions.assertEquals(Table.TableType.DELTALAKE, deltaTable.getType());
        Assertions.assertEquals("path/to/table", deltaTable.getTableLocation());
    }

    @Test
    public void testGetTableVersionRangeLatestUsesTableVersion() {
        DeltaLakeTable table = tableWithVersion(7L);
        TableVersionRange range = deltaLakeMetadata.getTableVersionRange("db1", table, Optional.empty(), Optional.empty());
        Assertions.assertEquals(Optional.of(7L), range.end());
    }

    private static Stream<Arguments> supportedVersionLiterals() {
        return Stream.of(
                Arguments.of("tinyint", ConstantOperator.createTinyInt((byte) 3), 3L),
                Arguments.of("int", ConstantOperator.createInt(5), 5L),
                Arguments.of("bigint", ConstantOperator.createBigint(8L), 8L));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("supportedVersionLiterals")
    public void testGetTableVersionRangeAsOfVersion(String name, ConstantOperator versionLiteral, long expectedVersion) {
        DeltaLakeTable table = tableWithVersion(7L);
        ConnectorTableVersion endVersion =
                new ConnectorTableVersion(PointerType.VERSION, versionLiteral);
        TableVersionRange range =
                deltaLakeMetadata.getTableVersionRange("db1", table, Optional.empty(), Optional.of(endVersion));
        Assertions.assertEquals(Optional.of(expectedVersion), range.end());
    }

    @Test
    public void testGetTableVersionRangeStartVersionUnsupported() {
        DeltaLakeTable table = tableWithVersion(7L);
        ConnectorTableVersion startVersion =
                new ConnectorTableVersion(PointerType.VERSION, ConstantOperator.createBigint(1L));
        Assertions.assertThrows(StarRocksConnectorException.class, () ->
                deltaLakeMetadata.getTableVersionRange("db1", table, Optional.of(startVersion), Optional.empty()));
    }

    private static Stream<Arguments> unsupportedVersionLiterals() {
        return Stream.of(
                Arguments.of("temporal", new ConnectorTableVersion(PointerType.TEMPORAL,
                        ConstantOperator.createBigint(1L)), "Unsupported Delta table version type"),
                Arguments.of("negative", new ConnectorTableVersion(PointerType.VERSION,
                        ConstantOperator.createBigint(-1L)), "non-negative integer version"),
                Arguments.of("varchar", new ConnectorTableVersion(PointerType.VERSION,
                        ConstantOperator.createVarchar("main")), "Unsupported Delta version type"));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("unsupportedVersionLiterals")
    public void testGetTableVersionRangeRejectsUnsupportedVersion(String name, ConnectorTableVersion endVersion,
                                                                  String expectedMessage) {
        DeltaLakeTable table = tableWithVersion(7L);
        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class, () ->
                deltaLakeMetadata.getTableVersionRange("db1", table, Optional.empty(), Optional.of(endVersion)));
        Assertions.assertTrue(ex.getMessage().contains(expectedMessage));
    }

    private static GetRemoteFilesParams versionParams(long version) {
        return versionParams(Optional.of(version));
    }

    private static GetRemoteFilesParams versionParams(Optional<Long> end) {
        return GetRemoteFilesParams.newBuilder()
                .setTableVersionRange(TableVersionRange.withEnd(end))
                .setPredicate(null)
                .setFieldNames(Lists.newArrayList())
                .build();
    }

    private static AtomicLong recordSnapshotByVersionLoads() {
        AtomicLong loadedVersion = new AtomicLong(Long.MIN_VALUE);
        new MockUp<DeltaMetastoreOperations>() {
            @Mock
            public DeltaLakeSnapshot getSnapshotByVersion(String dbName, String tableName, long version) {
                loadedVersion.set(version);
                return null;
            }
        };
        return loadedVersion;
    }

    @FunctionalInterface
    private interface MetadataEntryPoint {
        void invoke(DeltaLakeMetadata metadata, DeltaLakeTable table);
    }

    private static Stream<Arguments> versionedMetadataEntryPoints() {
        return Stream.of(
                Arguments.of("getRemoteFiles", false, (MetadataEntryPoint) (metadata, table) ->
                        metadata.getRemoteFiles(table, versionParams(3L))),
                Arguments.of("getRemoteFilesAsync", false, (MetadataEntryPoint) (metadata, table) ->
                        metadata.getRemoteFilesAsync(table, versionParams(3L))),
                Arguments.of("getTableStatistics", true, (MetadataEntryPoint) (metadata, table) ->
                        metadata.getTableStatistics(null, table, Maps.newHashMap(), Lists.newArrayList(), null,
                                -1, TableVersionRange.withEnd(Optional.of(3L)))));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("versionedMetadataEntryPoints")
    public void testVersionedEntryPointsLoadRequestedSnapshot(String name, boolean enableExternalStats,
                                                              MetadataEntryPoint entryPoint) {
        DeltaLakeTable table = tableWithVersion(7L);
        DeltaLakeMetadata metadata = enableExternalStats ? metadataWithExternalStats() : deltaLakeMetadata;
        AtomicLong loadedVersion = recordSnapshotByVersionLoads();
        Assertions.assertThrows(StarRocksConnectorException.class, () -> entryPoint.invoke(metadata, table));
        Assertions.assertEquals(3L, loadedVersion.get());
    }

    @Test
    public void testVersionedEntryPointValidatesPinnedSnapshotBeforeConversion(@Mocked SnapshotImpl snapshot) {
        DeltaLakeTable table = tableWithVersion(7L);
        DeltaLakeMetadata metadata = metadataWithOps(new DeltaMetastoreOperations(
                newCachingMetastore(), false, MetastoreType.HMS) {
            @Override
            public DeltaLakeSnapshot getSnapshotByVersion(String dbName, String tableName, long version) {
                return new DeltaLakeSnapshot(dbName, tableName, null, snapshot,
                        123, version, "s3://bucket/path/to/table");
            }
        });

        new Expectations() {
            {
                snapshot.getProtocol();
                result = null;
                minTimes = 0;

                snapshot.getMetadata();
                result = null;
                minTimes = 0;
            }
        };

        ValidateException ex = Assertions.assertThrows(ValidateException.class,
                () -> metadata.getRemoteFiles(table, versionParams(3L)));
        Assertions.assertTrue(ex.getMessage().contains("Delta table is missing protocol or metadata information."));
    }

    @Test
    public void testVersionedEntryPointConvertsValidatedPinnedSnapshot(@Mocked SnapshotImpl snapshot,
                                                                        @Mocked Protocol protocol,
                                                                        @Mocked Metadata metadata) {
        DeltaLakeTable table = tableWithVersion(7L);
        new MockUp<DeltaMetastoreOperations>() {
            @Mock
            public DeltaLakeSnapshot getSnapshotByVersion(String dbName, String tableName, long version) {
                return new DeltaLakeSnapshot(dbName, tableName, null, snapshot, 123, version,
                        "s3://bucket/path/to/table");
            }
        };
        new Expectations() {
            {
                snapshot.getProtocol();
                result = protocol;
                minTimes = 0;
                snapshot.getMetadata();
                result = metadata;
                minTimes = 0;
            }
        };
        new MockUp<DeltaUtils>() {
            @Mock
            public DeltaLakeTable convertDeltaSnapshotToSRTable(String catalog, DeltaLakeSnapshot pinned) {
                return tableWithVersion(3L);
            }
        };

        PredicateSearchKey key = PredicateSearchKey.of("db1", "table1", 3L, null);
        markScanned(key);
        putSplitTasks(key, List.of(fileScanTask()));

        List<RemoteFileInfo> files = deltaLakeMetadata.getRemoteFiles(table, versionParams(3L));
        Assertions.assertEquals(1, files.size());
    }

    @Test
    public void testGetRemoteFilesHandlesMissingAndCachedSplitTasks() {
        DeltaLakeTable table = tableWithVersion(7L);
        GetRemoteFilesParams params = versionParams(7L);
        PredicateSearchKey key = PredicateSearchKey.of("db1", "table1", 7L, params.getPredicate());
        FileScanTask first = fileScanTask();
        FileScanTask second = fileScanTask();
        markScanned(key);

        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> deltaLakeMetadata.getRemoteFiles(table, params));
        Assertions.assertTrue(ex.getMessage().contains("Missing deltalake split task for table:[db1.table1]"));

        putSplitTasks(key, List.of(first, second));

        List<RemoteFileInfo> files = deltaLakeMetadata.getRemoteFiles(table, params);

        Assertions.assertEquals(2, files.size());
        Assertions.assertTrue(files.get(0) instanceof DeltaRemoteFileInfo);
        Assertions.assertSame(first, ((DeltaRemoteFileInfo) files.get(0)).getFileScanTask());
        Assertions.assertSame(second, ((DeltaRemoteFileInfo) files.get(1)).getFileScanTask());
    }

    @Test
    public void testGetRemoteFilesAsyncBuildsRemoteInfoSource(@Mocked SnapshotImpl snapshot,
                                                              @Mocked Metadata metadata,
                                                              @Mocked ScanBuilderImpl scanBuilder) throws Exception {
        DeltaLakeTable table = new DeltaLakeTable(1, "delta0", "db1", "table1", Lists.newArrayList(),
                Lists.newArrayList(), snapshot, "s3://bucket/path/to/table", null, 0L, 7L);

        new Expectations() {
            {
                snapshot.getMetadata();
                result = metadata;
                minTimes = 0;

                metadata.getSchema();
                result = new StructType(Lists.newArrayList());
                minTimes = 0;

                metadata.getPartitionColNames();
                result = Set.of();
                minTimes = 0;

                snapshot.getScanBuilder();
                result = scanBuilder;
                minTimes = 0;

                scanBuilder.withFilter((io.delta.kernel.expressions.Predicate) any);
                result = scanBuilder;
                minTimes = 0;
            }
        };

        RemoteFileInfoSource source = deltaLakeMetadata.getRemoteFilesAsync(table, versionParams(7L));

        Assertions.assertNotNull(source);
        source.close();
    }

    private static Stream<Arguments> loadedTableVersionRanges() {
        return Stream.of(
                Arguments.of("current version", versionParams(7L)),
                Arguments.of("no version pinned", versionParams(Optional.empty())));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("loadedTableVersionRanges")
    public void testGetRemoteFilesLoadedVersionSkipsSnapshotLoad(String name, GetRemoteFilesParams params) {
        DeltaLakeTable table = tableWithVersion(7L);
        AtomicLong loadedVersion = recordSnapshotByVersionLoads();
        Assertions.assertThrows(Exception.class,
                () -> deltaLakeMetadata.getRemoteFiles(table, params));
        Assertions.assertEquals(Long.MIN_VALUE, loadedVersion.get());
    }

    @Test
    public void testGetTableReturnsNullWhenDeltaOpsReturnsNull() {
        DeltaLakeMetadata metadata = metadataWithOps(new DeltaMetastoreOperations(
                newCachingMetastore(), false, MetastoreType.HMS) {
            @Override
            public Table getTable(String dbName, String tableName) {
                return null;
            }
        });

        Assertions.assertNull(metadata.getTable(new ConnectContext(), "db1", "missing"));
    }

    @Test
    public void testGetTablePropagatesConnectorException() {
        DeltaLakeMetadata metadata = metadataWithOps(new DeltaMetastoreOperations(
                newCachingMetastore(), false, MetastoreType.HMS) {
            @Override
            public Table getTable(String dbName, String tableName) {
                throw new StarRocksConnectorException(
                        "Unity Catalog loadTable(main.db1.table1) failed (HTTP 0): EOF reached while reading");
            }
        });

        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metadata.getTable(new ConnectContext(), "db1", "table1"));
        Assertions.assertTrue(ex.getMessage().contains("HTTP 0"),
                "transport failures must not be flattened into table-not-found; was: " + ex.getMessage());
    }

    @Test
    public void testGetTableReturnsNullWhenTableNotFound() {
        DeltaLakeMetadata metadata = metadataWithOps(new DeltaMetastoreOperations(
                newCachingMetastore(), false, MetastoreType.HMS) {
            @Override
            public Table getTable(String dbName, String tableName) {
                throw new DeltaLakeTableNotFoundException(
                        "Unity Catalog table main.db1.missing not found");
            }
        });

        Assertions.assertNull(metadata.getTable(new ConnectContext(), "db1", "missing"));
    }

    private DeltaLakeMetadata metadataWithOps(DeltaMetastoreOperations deltaOps) {
        return new DeltaLakeMetadata(new HdfsEnvironment(Maps.newHashMap()), "delta0", deltaOps, null,
                new ConnectorProperties(ConnectorType.DELTALAKE));
    }

    private DeltaLakeMetadata metadataWithExternalStats() {
        return new DeltaLakeMetadata(new HdfsEnvironment(Maps.newHashMap()), "delta0",
                new DeltaMetastoreOperations(newCachingMetastore(), false, MetastoreType.HMS), null,
                new ConnectorProperties(ConnectorType.DELTALAKE, Map.of(
                        ConnectorProperties.ENABLE_GET_STATS_FROM_EXTERNAL_METADATA, "true")));
    }

    private CachingDeltaLakeMetastore newCachingMetastore() {
        return CachingDeltaLakeMetastore.createQueryLevelInstance(metastore, 10000);
    }

    private void markScanned(PredicateSearchKey key) {
        Deencapsulation.<Set<PredicateSearchKey>>getField(deltaLakeMetadata, "scannedTables").add(key);
    }

    private void putSplitTasks(PredicateSearchKey key, List<FileScanTask> tasks) {
        Deencapsulation.<Map<PredicateSearchKey, List<FileScanTask>>>getField(deltaLakeMetadata, "splitTasks")
                .put(key, tasks);
    }

    private static FileScanTask fileScanTask() {
        return new FileScanTask(null, 0L, Map.of(), null);
    }

    private static DeltaLakeTable tableWithVersion(long version) {
        return new DeltaLakeTable(1, "delta0", "db1", "table1", Lists.newArrayList(),
            Lists.newArrayList(), null, "s3://bucket/path/to/table", null, 0L, version);
    }
}
