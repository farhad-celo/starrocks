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

import com.google.common.collect.Maps;
import com.starrocks.connector.MetastoreType;
import com.starrocks.connector.hive.HiveMetaClient;
import com.starrocks.connector.hive.HiveMetastore;
import com.starrocks.connector.hive.HiveMetastoreTest;
import com.starrocks.connector.hive.IHiveMetastore;
import com.starrocks.connector.metastore.MetastoreTable;
import io.delta.kernel.Table;
import io.delta.kernel.data.ColumnarBatch;
import io.delta.kernel.engine.Engine;
import io.delta.kernel.internal.SnapshotImpl;
import io.delta.kernel.types.StructType;
import io.delta.kernel.utils.FileStatus;
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

import java.lang.reflect.Field;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;
import java.util.stream.Stream;

public class DeltaLakeMetastoreTest {
    private DeltaLakeMetastore metastore;

    @BeforeEach
    public void setUp() throws Exception {
        HiveMetaClient client = new HiveMetastoreTest.MockedHiveMetaClient();
        IHiveMetastore hiveMetastore = new HiveMetastore(client, "delta0", MetastoreType.HMS);
        DeltaLakeCatalogProperties properties = new DeltaLakeCatalogProperties(Maps.newHashMap());
        metastore = new HMSBackedDeltaMetastore("delta0", hiveMetastore, new Configuration(), properties);
    }

    @Test
    public void testCheckpointCacheWeigherHandlesLargeSize() throws Exception {
        // Mock SizeEstimator to return a value larger than Integer.MAX_VALUE
        new MockUp<org.apache.spark.util.SizeEstimator>() {
            @mockit.Mock
            public long estimate(Object obj) {
                // Return a value larger than Integer.MAX_VALUE to simulate overflow scenario
                return 3L * Integer.MAX_VALUE;
            }
        };

        // Create a new DeltaLakeMetastore instance to trigger cache creation with mocked SizeEstimator
        HiveMetaClient client = new HiveMetastoreTest.MockedHiveMetaClient();
        IHiveMetastore hiveMetastore = new HiveMetastore(client, "delta0", MetastoreType.HMS);
        DeltaLakeCatalogProperties properties = new DeltaLakeCatalogProperties(Maps.newHashMap());
        DeltaLakeMetastore testMetastore = new HMSBackedDeltaMetastore("delta0", hiveMetastore,
                new Configuration(), properties);

        // Get the checkpointCache field using reflection
        Field checkpointCacheField = DeltaLakeMetastore.class.getDeclaredField("checkpointCache");
        checkpointCacheField.setAccessible(true);
        @SuppressWarnings("unchecked")
        com.google.common.cache.LoadingCache<com.starrocks.common.Pair<DeltaLakeFileStatus, StructType>,
                List<ColumnarBatch>> checkpointCache =
                (com.google.common.cache.LoadingCache<com.starrocks.common.Pair<DeltaLakeFileStatus, StructType>,
                        List<ColumnarBatch>>) checkpointCacheField.get(testMetastore);

        // Create FileStatus using static factory method
        FileStatus fileStatus = FileStatus.of("s3://bucket/path/to/file.parquet", 100, 123);

        DeltaLakeFileStatus deltaLakeFileStatus = DeltaLakeFileStatus.of(fileStatus);
        StructType structType = new io.delta.kernel.types.StructType(com.google.common.collect.Lists.newArrayList());
        com.starrocks.common.Pair<DeltaLakeFileStatus, StructType> key =
                com.starrocks.common.Pair.create(deltaLakeFileStatus, structType);

        // Put an entry into the cache - this should trigger the weigher
        // If the weigher throws ArithmeticException due to integer overflow, this test will fail
        try {
            checkpointCache.put(key, com.google.common.collect.Lists.newArrayList());
            // If we reach here, the weigher handled the large size correctly
            Assertions.assertTrue(true);
        } catch (ArithmeticException e) {
            Assertions.fail("Weigher should not throw ArithmeticException for large sizes");
        }
    }

    @Test
    public void testJsonCacheWeigherHandlesLargeSize() throws Exception {
        // Mock SizeEstimator to return a value larger than Integer.MAX_VALUE
        new MockUp<org.apache.spark.util.SizeEstimator>() {
            @mockit.Mock
            public long estimate(Object obj) {
                // Return a value larger than Integer.MAX_VALUE to simulate overflow scenario
                return 3L * Integer.MAX_VALUE;
            }
        };

        // Create a new DeltaLakeMetastore instance to trigger cache creation with mocked SizeEstimator
        HiveMetaClient client = new HiveMetastoreTest.MockedHiveMetaClient();
        IHiveMetastore hiveMetastore = new HiveMetastore(client, "delta0", MetastoreType.HMS);
        DeltaLakeCatalogProperties properties = new DeltaLakeCatalogProperties(Maps.newHashMap());
        DeltaLakeMetastore testMetastore = new HMSBackedDeltaMetastore("delta0", hiveMetastore,
                new Configuration(), properties);

        // Get the jsonCache field using reflection
        Field jsonCacheField = DeltaLakeMetastore.class.getDeclaredField("jsonCache");
        jsonCacheField.setAccessible(true);
        @SuppressWarnings("unchecked")
        com.google.common.cache.LoadingCache<DeltaLakeFileStatus, List<com.fasterxml.jackson.databind.JsonNode>> jsonCache =
                (com.google.common.cache.LoadingCache<DeltaLakeFileStatus,
                        List<com.fasterxml.jackson.databind.JsonNode>>) jsonCacheField.get(testMetastore);

        // Create FileStatus using static factory method
        FileStatus fileStatus = FileStatus.of("s3://bucket/path/to/file.json", 100, 123);

        DeltaLakeFileStatus deltaLakeFileStatus = DeltaLakeFileStatus.of(fileStatus);

        // Put an entry into the cache - this should trigger the weigher
        // If the weigher throws ArithmeticException due to integer overflow, this test will fail
        try {
            jsonCache.put(deltaLakeFileStatus, com.google.common.collect.Lists.newArrayList());
            // If we reach here, the weigher handled the large size correctly
            Assertions.assertTrue(true);
        } catch (ArithmeticException e) {
            Assertions.fail("Weigher should not throw ArithmeticException for large sizes");
        }
    }

    @Test
    public void testGetSnapshotByVersionLoadsRequestedVersion(@Mocked SnapshotImpl snapshot) {
        AtomicLong requestedVersion = new AtomicLong(-1);
        new MockUp<HMSBackedDeltaMetastore>() {
            @Mock
            public MetastoreTable getMetastoreTable(String dbName, String tableName) {
                return new MetastoreTable(dbName, tableName, "s3://bucket/path/to/table", 123);
            }

            @Mock
            public SnapshotImpl loadSnapshotAsOfVersion(DeltaLakeEngine engine, String path, String dbName,
                                                        String tableName, MetastoreTable metastoreTable, long version) {
                requestedVersion.set(version);
                return snapshot;
            }
        };

        new Expectations() {
            {
                snapshot.getVersion();
                result = 3L;
                minTimes = 0;
            }
        };

        DeltaLakeSnapshot result = metastore.getSnapshotByVersion("db1", "table1", 3L);
        Assertions.assertEquals(3L, requestedVersion.get());
        Assertions.assertEquals(3L, result.getVersion());
        Assertions.assertEquals("s3://bucket/path/to/table", result.getPath());
    }

    @FunctionalInterface
    private interface SnapshotLoad {
        DeltaLakeSnapshot load(DeltaLakeMetastore metastore);
    }

    private static Stream<Arguments> defaultKernelLoads() {
        return Stream.of(
                Arguments.of("latest", (SnapshotLoad) m -> m.getLatestSnapshot("db1", "table1")),
                Arguments.of("as of version", (SnapshotLoad) m -> m.getSnapshotByVersion("db1", "table1", 3L)));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("defaultKernelLoads")
    public void testDefaultLoadReadsPublishedLog(String name, SnapshotLoad load,
                                                 @Mocked Table table, @Mocked SnapshotImpl snapshot) {
        new MockUp<HMSBackedDeltaMetastore>() {
            @Mock
            public MetastoreTable getMetastoreTable(String dbName, String tableName) {
                return new MetastoreTable(dbName, tableName, "s3://bucket/path/to/table", 123);
            }
        };
        new Expectations() {
            {
                Table.forPath((Engine) any, anyString);
                result = table;
                table.getLatestSnapshot((Engine) any);
                result = snapshot;
                minTimes = 0;
                table.getSnapshotAsOfVersion((Engine) any, anyLong);
                result = snapshot;
                minTimes = 0;
                snapshot.getVersion();
                result = 3L;
                minTimes = 0;
            }
        };

        DeltaLakeSnapshot result = load.load(metastore);
        Assertions.assertEquals(3L, result.getVersion());
        Assertions.assertEquals("s3://bucket/path/to/table", result.getPath());
    }
}
