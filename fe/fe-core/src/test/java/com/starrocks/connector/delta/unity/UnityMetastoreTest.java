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
import com.databricks.sdk.service.catalog.SchemaInfo;
import com.databricks.sdk.service.catalog.TableInfo;
import com.databricks.sdk.service.catalog.TableType;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.starrocks.catalog.Database;
import com.starrocks.connector.exception.StarRocksConnectorException;
import com.starrocks.connector.metastore.MetastoreTable;
import com.starrocks.credential.CloudConfiguration;
import com.starrocks.credential.CloudType;
import com.starrocks.credential.aws.AwsCloudConfiguration;
import mockit.Expectations;
import mockit.Mocked;
import mockit.Verifications;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;

import java.util.List;
import java.util.stream.Stream;

public class UnityMetastoreTest {

    private static final String S3_LOCATION = "s3://bucket/prefix/orders";
    private static final String ADLS_LOCATION = "abfss://container@account.dfs.core.windows.net/prefix/orders";
    private static final String TABLE_ID = "11111111-1111-1111-1111-111111111111";

    private static UnityCatalogProperties propsWithVendedCredentials(boolean enabled) {
        return new UnityCatalogProperties(ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiTEST",
                "unity.catalog.name", "main",
                "unity.catalog.vended-credentials-enabled", Boolean.toString(enabled)));
    }

    private static UnityCatalogProperties propsWithAwsRegionOverride(String region) {
        return new UnityCatalogProperties(ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiTEST",
                "unity.catalog.name", "main",
                "unity.catalog.vended-credentials-enabled", "true",
                "unity.catalog.aws.region", region));
    }

    @Test
    public void testGetAllDatabaseNamesFiltersBlank(@Mocked UnityCatalogClient client) {
        SchemaInfo s1 = new SchemaInfo().setName("sales");
        SchemaInfo s2 = new SchemaInfo().setName("");
        SchemaInfo s3 = new SchemaInfo().setName("marketing");

        new Expectations() {
            {
                client.listSchemas("main");
                result = ImmutableList.of(s1, s2, s3);
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        List<String> dbs = metastore.getAllDatabaseNames();
        Assertions.assertEquals(ImmutableList.of("sales", "marketing"), dbs);
    }

    @Test
    public void testGetDbReturnsDatabaseCaseInsensitively(@Mocked UnityCatalogClient client) {
        SchemaInfo sales = new SchemaInfo().setName("sales").setStorageLocation("s3://bucket/sales");

        new Expectations() {
            {
                client.listSchemas("main");
                result = ImmutableList.of(sales);
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        Database db = metastore.getDb("SALES");
        Assertions.assertNotNull(db);
        Assertions.assertEquals("SALES", db.getFullName());
        Assertions.assertEquals("s3://bucket/sales", db.getLocation());
    }

    @Test
    public void testGetDbDefaultsBlankLocationWhenSchemaHasNone(@Mocked UnityCatalogClient client) {
        SchemaInfo sales = new SchemaInfo().setName("sales");

        new Expectations() {
            {
                client.listSchemas("main");
                result = ImmutableList.of(sales);
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        Database db = metastore.getDb("sales");
        Assertions.assertEquals("", db.getLocation());
    }

    private static Stream<Arguments> getDbAbsentCases() {
        return Stream.of(
                Arguments.of(ImmutableList.of(new SchemaInfo().setName("sales")), "missing"),
                Arguments.of(ImmutableList.<SchemaInfo>of(), "sales"));
    }

    @ParameterizedTest
    @MethodSource("getDbAbsentCases")
    public void testGetDbReturnsNullWhenAbsent(List<SchemaInfo> schemas, String query,
                                               @Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.listSchemas("main");
                result = schemas;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        Assertions.assertNull(metastore.getDb(query));
    }

    @Test
    public void testGetAllTableNamesFiltersNonDelta(@Mocked UnityCatalogClient client) {
        TableInfo t1 = new TableInfo().setName("orders").setDataSourceFormat(DataSourceFormat.DELTA);
        TableInfo t2 = new TableInfo().setName("iceberg_tbl").setDataSourceFormat(DataSourceFormat.ICEBERG);
        TableInfo t3 = new TableInfo().setName("delta_two").setDataSourceFormat(DataSourceFormat.DELTA);

        new Expectations() {
            {
                client.listTables("main", "sales");
                result = ImmutableList.of(t1, t2, t3);
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        List<String> tables = metastore.getAllTableNames("sales");
        Assertions.assertEquals(ImmutableList.of("orders", "delta_two"), tables);
    }

    @Test
    public void testGetAllTableNamesIncludesViewsAlongsideDeltaTables(@Mocked UnityCatalogClient client) {
        TableInfo delta = new TableInfo().setName("orders").setDataSourceFormat(DataSourceFormat.DELTA);
        TableInfo iceberg = new TableInfo().setName("iceberg_tbl").setDataSourceFormat(DataSourceFormat.ICEBERG);
        TableInfo view = new TableInfo().setName("orders_view").setTableType(TableType.VIEW);

        new Expectations() {
            {
                client.listTables("main", "sales");
                result = ImmutableList.of(delta, iceberg, view);
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        List<String> tables = metastore.getAllTableNames("sales");
        Assertions.assertEquals(ImmutableList.of("orders", "orders_view"), tables);
    }

    @Test
    public void testGetMetastoreTableReturnsUnityMetastoreTable(@Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.loadTable("main", "sales", "orders");
                result = UnityDeltaModelFixtures.loadResponse(S3_LOCATION, 1_700_000_000_000L, null);
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        MetastoreTable mt = metastore.getMetastoreTable("sales", "orders");
        Assertions.assertInstanceOf(UnityMetastoreTable.class, mt);
        Assertions.assertEquals(S3_LOCATION, mt.getTableLocation());
        // Unity Catalog reports millis; MetastoreTable stores seconds.
        Assertions.assertEquals(1_700_000_000L, mt.getCreateTime());
        Assertions.assertNotNull(((UnityMetastoreTable) mt).getLoadTableResponse());
    }

    @Test
    public void testGetMetastoreTableHandlesMissingTimestamps(@Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.loadTable("main", "sales", "orders");
                result = UnityDeltaModelFixtures.loadResponse(S3_LOCATION, null, null);
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        MetastoreTable mt = metastore.getMetastoreTable("sales", "orders");
        Assertions.assertEquals(0L, mt.getCreateTime());
    }

    @Test
    public void testGetMetastoreTableRejectsMissingLocation(@Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.loadTable("main", "sales", "managed_tbl");
                result = UnityDeltaModelFixtures.loadResponse(null, 1L, null);
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metastore.getMetastoreTable("sales", "managed_tbl"));
    }

    @Test
    public void testResolveCloudConfigurationWithAwsVendedCredentials(@Mocked UnityCatalogClient client) {
        GetMetastoreSummaryResponse summary = new GetMetastoreSummaryResponse().setRegion("us-east-1");

        new Expectations() {
            {
                client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ");
                result = UnityDeltaModelFixtures.awsCredsResponse(1L);
                client.getMetastoreSummary();
                result = summary;
                times = 1;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        CloudConfiguration cc = metastore.resolveCloudConfiguration("sales", "orders", S3_LOCATION, TABLE_ID);
        Assertions.assertNotNull(cc);
        Assertions.assertEquals(CloudType.AWS, cc.getCloudType());
        Assertions.assertInstanceOf(AwsCloudConfiguration.class, cc);
    }

    @Test
    public void testResolveCloudConfigurationUsesMetastoreRegion(@Mocked UnityCatalogClient client) {
        GetMetastoreSummaryResponse summary = new GetMetastoreSummaryResponse().setRegion("eu-central-1");

        new Expectations() {
            {
                client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ");
                result = UnityDeltaModelFixtures.awsCredsResponse(1L);
                times = 2;
                client.getMetastoreSummary();
                result = summary;
                times = 1;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        CloudConfiguration cc = metastore.resolveCloudConfiguration("sales", "orders", S3_LOCATION, TABLE_ID);
        Assertions.assertEquals("eu-central-1",
                ((AwsCloudConfiguration) cc).getAwsCloudCredential().getRegion());
        CloudConfiguration cc2 = metastore.resolveCloudConfiguration("sales", "orders", S3_LOCATION, TABLE_ID);
        Assertions.assertEquals("eu-central-1",
                ((AwsCloudConfiguration) cc2).getAwsCloudCredential().getRegion());
    }

    @Test
    public void testResolveCloudConfigurationRetriesAfterRegionLookupFailure(@Mocked UnityCatalogClient client) {
        GetMetastoreSummaryResponse summary = new GetMetastoreSummaryResponse().setRegion("eu-central-1");

        new Expectations() {
            {
                client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ");
                result = UnityDeltaModelFixtures.awsCredsResponse(1L);
                times = 2;
                client.getMetastoreSummary();
                result = new Object[] {
                        new StarRocksConnectorException("simulated 503 on metastore_summary"),
                        summary
                };
                times = 2;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metastore.resolveCloudConfiguration("sales", "orders", S3_LOCATION, TABLE_ID));
        CloudConfiguration second = metastore.resolveCloudConfiguration("sales", "orders", S3_LOCATION, TABLE_ID);
        Assertions.assertEquals("eu-central-1",
                ((AwsCloudConfiguration) second).getAwsCloudCredential().getRegion());
    }

    private static Stream<Arguments> unresolvableRegionCases() {
        // The region lookup fails outright, or succeeds but carries no region: both must fail loudly
        // instead of silently falling back to us-east-1.
        return Stream.of(
                Arguments.of(new StarRocksConnectorException("403 forbidden on metastore_summary")),
                Arguments.of(new GetMetastoreSummaryResponse()));
    }

    @ParameterizedTest
    @MethodSource("unresolvableRegionCases")
    public void testResolveCloudConfigurationFailsWhenRegionUnresolvable(Object summaryResult,
                                                                         @Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ");
                result = UnityDeltaModelFixtures.awsCredsResponse(1L);
                client.getMetastoreSummary();
                result = summaryResult;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metastore.resolveCloudConfiguration("sales", "orders", S3_LOCATION, TABLE_ID));
    }

    @Test
    public void testResolveCloudConfigurationUsesAwsRegionOverride(@Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ");
                result = UnityDeltaModelFixtures.awsCredsResponse(1L);
                client.getMetastoreSummary();
                times = 0;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithAwsRegionOverride("us-west-2"));
        CloudConfiguration cc = metastore.resolveCloudConfiguration("sales", "orders", S3_LOCATION, TABLE_ID);
        Assertions.assertEquals("us-west-2",
                ((AwsCloudConfiguration) cc).getAwsCloudCredential().getRegion());
    }

    @Test
    public void testResolveCloudConfigurationDoesNotQueryRegionForAzure(@Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ");
                result = UnityDeltaModelFixtures.credsResponse(UnityDeltaModelFixtures.credential(
                        ADLS_LOCATION,
                        UnityDeltaModelFixtures.azureConfig("sv=2022-11-02&sig=fakesignature"), 1L));
                client.getMetastoreSummary();
                times = 0;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        CloudConfiguration cc = metastore.resolveCloudConfiguration("sales", "orders", ADLS_LOCATION, TABLE_ID);
        Assertions.assertEquals(CloudType.AZURE, cc.getCloudType());
    }

    @Test
    public void testResolveCloudConfigurationReturnsNullWhenDisabled(@Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.getTableCredentials(anyString, anyString, anyString, anyString, anyString);
                times = 0;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        Assertions.assertNull(metastore.resolveCloudConfiguration("sales", "orders", S3_LOCATION, TABLE_ID));
    }

    @Test
    public void testResolveCloudConfigurationThrowsOnVendingFailure(@Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ");
                result = new StarRocksConnectorException("forbidden");
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metastore.resolveCloudConfiguration("sales", "orders", S3_LOCATION, TABLE_ID));
        Assertions.assertTrue(ex.getMessage().contains("main.sales.orders"),
                "exception message must include the failing table name; was: " + ex.getMessage());
    }

    @Test
    public void testResolveCloudConfigurationThrowsWhenTranslationProducesNothing(@Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                // UC returned a successful response but no usable credentials. The translator yields
                // a DEFAULT/empty CloudConfiguration; the metastore must surface a hard failure.
                client.getTableCredentials("main", "sales", "orders", TABLE_ID, "READ");
                result = UnityDeltaModelFixtures.credsResponse();
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithAwsRegionOverride("us-east-1"));
        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metastore.resolveCloudConfiguration("sales", "orders", S3_LOCATION, TABLE_ID));
        Assertions.assertTrue(ex.getMessage().contains("main.sales.orders"),
                "exception message must include the failing table name; was: " + ex.getMessage());
    }

    @Test
    public void testTableExists(@Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.tableExists("main.sales.orders");
                result = true;
                client.tableExists("main.sales.missing");
                result = false;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        Assertions.assertTrue(metastore.tableExists("sales", "orders"));
        Assertions.assertFalse(metastore.tableExists("sales", "missing"));
    }

    @Test
    public void testInvalidateTableForwardsToClient(@Mocked UnityCatalogClient client) {
        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        metastore.invalidateTable("sales", "orders");

        new Verifications() {
            {
                client.invalidate("main.sales.orders");
                times = 1;
            }
        };
    }
}
