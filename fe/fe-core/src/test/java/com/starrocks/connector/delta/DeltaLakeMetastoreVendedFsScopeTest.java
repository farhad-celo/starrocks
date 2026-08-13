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
import com.starrocks.credential.CloudConfiguration;
import com.starrocks.sql.analyzer.SemanticException;
import io.delta.kernel.engine.Engine;
import io.delta.kernel.internal.TableImpl;
import mockit.Mock;
import mockit.MockUp;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.security.UserGroupInformation;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.jupiter.api.Assertions.assertThrows;

/**
 * Exercises the vended-credentials filesystem-scope integration in {@link DeltaLakeMetastore}: when a
 * per-table {@link CloudConfiguration} is present, the kernel snapshot load must run under a
 * throwaway, per-table UGI so the S3AFileSystem instances it builds are reclaimed via closeAllForUGI
 * rather than leaked. The kernel read itself is short-circuited via a {@link TableImpl#forPath} fake
 * (the same seam used by {@link CachingDeltaLakeMetastoreTest}); the fake records the UGI/config it
 * observes.
 */
public class DeltaLakeMetastoreVendedFsScopeTest {
    private static final String S3A_CACHE = "fs.s3a.impl.disable.cache";
    private static final String VENDED_MARKER = "test.vended.marker";

    private static DeltaLakeMetastore newMetastore() {
        HiveMetaClient client = new HiveMetastoreTest.MockedHiveMetaClient();
        IHiveMetastore hiveMetastore = new HiveMetastore(client, "delta0", MetastoreType.HMS);
        return new HMSBackedDeltaMetastore("delta0", hiveMetastore, new Configuration(),
                new DeltaLakeCatalogProperties(Maps.newHashMap()));
    }

    @Test
    public void testVendedSnapshotLoadRunsUnderScopedUgi() {
        List<Captured> calls = captureForPathCalls();
        DeltaLakeMetastore metastore = newMetastore();
        MetastoreTable table = new MetastoreTable("db1", "table1", "s3://bucket/path/to/table", 123);

        SemanticException ex = assertThrows(SemanticException.class, () ->
                metastore.getLatestSnapshot("db1", "table1", table, vendedConfig("creds-v1")));

        assertThat(ex.getMessage(), containsString("Failed to get snapshot for delta0.db1.table1"));
        assertThat(ex.getMessage(), containsString("short-circuit after capture"));
        Captured captured = calls.get(0);
        Assertions.assertEquals("delta-vended-fs-delta0-db1-table1", captured.ugi,
                "vended snapshot load must run under the per-table throwaway UGI");
        Assertions.assertEquals("false", captured.cacheFlag,
                "the per-table vended config must keep the FS cache enabled so closeAllForUGI can reclaim it");
        Assertions.assertEquals("creds-v1", captured.marker,
                "the per-table vended config must be applied to the engine's configuration");
        Assertions.assertTrue(captured.perTableConfig, "the engine must be built in per-table-config mode");
    }

    @Test
    public void testNonVendedSnapshotLoadRunsWithoutScope() {
        List<Captured> calls = captureForPathCalls();
        DeltaLakeMetastore metastore = newMetastore();
        MetastoreTable table = new MetastoreTable("db1", "table1", "s3://bucket/path/to/table", 123);

        assertThrows(SemanticException.class, () ->
                metastore.getLatestSnapshot("db1", "table1", table, null));

        Captured captured = calls.get(0);
        Assertions.assertEquals(currentUser(), captured.ugi,
                "a non-vended load must run under the ambient login UGI, not a throwaway scope");
        Assertions.assertNull(captured.cacheFlag, "a non-vended load must not touch the FS cache flags");
        Assertions.assertFalse(captured.perTableConfig, "the engine must not be in per-table-config mode");
    }

    @Test
    public void testDistinctTablesGetDistinctScopeIdentities() {
        List<Captured> calls = captureForPathCalls();
        DeltaLakeMetastore metastore = newMetastore();

        assertThrows(SemanticException.class, () -> metastore.getLatestSnapshot("db1", "table1",
                new MetastoreTable("db1", "table1", "s3://bucket/t1", 1), vendedConfig("creds")));
        assertThrows(SemanticException.class, () -> metastore.getLatestSnapshot("db1", "table2",
                new MetastoreTable("db1", "table2", "s3://bucket/t2", 1), vendedConfig("creds")));

        Assertions.assertEquals(2, calls.size());
        Assertions.assertNotEquals(calls.get(0).ugi, calls.get(1).ugi,
                "different tables must be isolated under different scope UGI identities");
    }

    // Installs a TableImpl.forPath fake that records what each kernel read observes (UGI, cache flag,
    // applied vended marker, per-table mode) and then short-circuits, so the metastore takes its
    // exception path without any real filesystem access. Returns the list the calls accumulate into.
    private static List<Captured> captureForPathCalls() {
        List<Captured> calls = new ArrayList<>();
        new MockUp<TableImpl>() {
            @Mock
            public io.delta.kernel.Table forPath(Engine engine, String path) {
                DeltaLakeEngine deltaEngine = (DeltaLakeEngine) engine;
                Captured captured = new Captured();
                captured.ugi = currentUser();
                captured.cacheFlag = deltaEngine.getHadoopConf().get(S3A_CACHE);
                captured.marker = deltaEngine.getHadoopConf().get(VENDED_MARKER);
                captured.perTableConfig = deltaEngine.isPerTableConfig();
                calls.add(captured);
                throw new RuntimeException("short-circuit after capture");
            }
        };
        return calls;
    }

    private static final class Captured {
        private String ugi;
        private String cacheFlag;
        private String marker;
        private boolean perTableConfig;
    }

    private static CloudConfiguration vendedConfig(String marker) {
        return new CloudConfiguration() {
            @Override
            public void applyToConfiguration(Configuration configuration) {
                configuration.set(VENDED_MARKER, marker);
            }
        };
    }

    private static String currentUser() {
        try {
            return UserGroupInformation.getCurrentUser().getUserName();
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }
}
