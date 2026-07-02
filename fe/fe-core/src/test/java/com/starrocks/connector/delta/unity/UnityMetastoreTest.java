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
import com.databricks.sdk.service.catalog.AzureUserDelegationSas;
import com.databricks.sdk.service.catalog.DataSourceFormat;
import com.databricks.sdk.service.catalog.GenerateTemporaryTableCredentialResponse;
import com.databricks.sdk.service.catalog.GetMetastoreSummaryResponse;
import com.databricks.sdk.service.catalog.SchemaInfo;
import com.databricks.sdk.service.catalog.TableInfo;
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

import java.util.List;

public class UnityMetastoreTest {

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

    private static TableInfo deltaTable(String tableId) {
        return new TableInfo()
                .setFullName("main.sales.orders")
                .setTableId(tableId)
                .setDataSourceFormat(DataSourceFormat.DELTA)
                .setStorageLocation("s3://bucket/prefix/orders");
    }

    private static GenerateTemporaryTableCredentialResponse awsCreds() {
        return new GenerateTemporaryTableCredentialResponse()
                .setAwsTempCredentials(new AwsCredentials()
                        .setAccessKeyId("AKIA_TEST")
                        .setSecretAccessKey("secret")
                        .setSessionToken("session"));
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
        // Caller passes a differently-cased name; lookup must still resolve.
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

    @Test
    public void testGetDbReturnsNullWhenDatabaseMissing(@Mocked UnityCatalogClient client) {
        SchemaInfo sales = new SchemaInfo().setName("sales");

        new Expectations() {
            {
                client.listSchemas("main");
                result = ImmutableList.of(sales);
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        // A missing database returns null (the connector's "does not exist" contract); the
        // caching layer translates that into a clean unknown-database error for the user.
        Assertions.assertNull(metastore.getDb("missing"));
    }

    @Test
    public void testGetDbReturnsNullWhenCatalogHasNoSchemas(@Mocked UnityCatalogClient client) {
        new Expectations() {
            {
                client.listSchemas("main");
                result = ImmutableList.of();
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        Assertions.assertNull(metastore.getDb("sales"));
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
    public void testGetMetastoreTableReturnsPlainMetastoreTable(@Mocked UnityCatalogClient client) {
        TableInfo info = deltaTable("abc-123")
                .setCreatedAt(1_700_000_000_000L);

        new Expectations() {
            {
                client.getTable("main.sales.orders");
                result = info;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        MetastoreTable mt = metastore.getMetastoreTable("sales", "orders");
        Assertions.assertEquals("s3://bucket/prefix/orders", mt.getTableLocation());
        // Unity Catalog reports millis; MetastoreTable stores seconds.
        Assertions.assertEquals(1_700_000_000L, mt.getCreateTime());
    }

    @Test
    public void testGetMetastoreTableHandlesMissingTimestamps(@Mocked UnityCatalogClient client) {
        TableInfo info = deltaTable("abc-123");

        new Expectations() {
            {
                client.getTable("main.sales.orders");
                result = info;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        MetastoreTable mt = metastore.getMetastoreTable("sales", "orders");
        Assertions.assertEquals(0L, mt.getCreateTime());
    }

    @Test
    public void testResolveCloudConfigurationWithAwsVendedCredentials(@Mocked UnityCatalogClient client) {
        TableInfo info = deltaTable("abc-123");
        GetMetastoreSummaryResponse summary = new GetMetastoreSummaryResponse().setRegion("us-east-1");

        new Expectations() {
            {
                client.getTemporaryTableCredentials("abc-123", "READ");
                result = awsCreds();
                client.getMetastoreSummary();
                result = summary;
                times = 1;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        CloudConfiguration cc = metastore.resolveCloudConfiguration(info);
        Assertions.assertNotNull(cc);
        Assertions.assertEquals(CloudType.AWS, cc.getCloudType());
        Assertions.assertInstanceOf(AwsCloudConfiguration.class, cc);
    }

    @Test
    public void testResolveCloudConfigurationUsesMetastoreRegion(@Mocked UnityCatalogClient client) {
        TableInfo info = deltaTable("abc-123");
        GetMetastoreSummaryResponse summary = new GetMetastoreSummaryResponse().setRegion("eu-central-1");

        new Expectations() {
            {
                client.getTemporaryTableCredentials("abc-123", "READ");
                result = awsCreds();
                times = 2;
                client.getMetastoreSummary();
                result = summary;
                times = 1;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        // First call resolves and memoizes the region.
        CloudConfiguration cc = metastore.resolveCloudConfiguration(info);
        Assertions.assertEquals("eu-central-1",
                ((AwsCloudConfiguration) cc).getAwsCloudCredential().getRegion());
        // Second call must reuse the memoized region (times = 1 above asserts no extra REST hit).
        CloudConfiguration cc2 = metastore.resolveCloudConfiguration(info);
        Assertions.assertEquals("eu-central-1",
                ((AwsCloudConfiguration) cc2).getAwsCloudCredential().getRegion());
    }

    @Test
    public void testResolveCloudConfigurationRetriesAfterRegionLookupFailure(
            @Mocked UnityCatalogClient client) {
        TableInfo info = deltaTable("abc-123");
        GetMetastoreSummaryResponse summary = new GetMetastoreSummaryResponse().setRegion("eu-central-1");

        new Expectations() {
            {
                client.getTemporaryTableCredentials("abc-123", "READ");
                result = awsCreds();
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
        // First lookup fails -> throws and leaves the memo unset so the next call retries.
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metastore.resolveCloudConfiguration(info));
        // Second lookup succeeds -> region resolved.
        CloudConfiguration second = metastore.resolveCloudConfiguration(info);
        Assertions.assertEquals("eu-central-1",
                ((AwsCloudConfiguration) second).getAwsCloudCredential().getRegion());
    }

    @Test
    public void testResolveCloudConfigurationThrowsWhenSummaryFails(@Mocked UnityCatalogClient client) {
        TableInfo info = deltaTable("abc-123");

        new Expectations() {
            {
                client.getTemporaryTableCredentials("abc-123", "READ");
                result = awsCreds();
                client.getMetastoreSummary();
                result = new StarRocksConnectorException("403 forbidden on metastore_summary");
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metastore.resolveCloudConfiguration(info));
    }

    @Test
    public void testResolveCloudConfigurationThrowsWhenSummaryHasNoRegion(@Mocked UnityCatalogClient client) {
        TableInfo info = deltaTable("abc-123");

        new Expectations() {
            {
                client.getTemporaryTableCredentials("abc-123", "READ");
                result = awsCreds();
                client.getMetastoreSummary();
                result = new GetMetastoreSummaryResponse();
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metastore.resolveCloudConfiguration(info),
                "missing region must fail loudly instead of silently falling back to us-east-1");
    }

    @Test
    public void testResolveCloudConfigurationUsesAwsRegionOverride(@Mocked UnityCatalogClient client) {
        TableInfo info = deltaTable("abc-123");

        new Expectations() {
            {
                client.getTemporaryTableCredentials("abc-123", "READ");
                result = awsCreds();
                client.getMetastoreSummary();
                times = 0;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithAwsRegionOverride("us-west-2"));
        CloudConfiguration cc = metastore.resolveCloudConfiguration(info);
        Assertions.assertEquals("us-west-2",
                ((AwsCloudConfiguration) cc).getAwsCloudCredential().getRegion());
    }

    @Test
    public void testResolveCloudConfigurationDoesNotQueryRegionForAzure(@Mocked UnityCatalogClient client) {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders")
                .setTableId("abc-azure")
                .setDataSourceFormat(DataSourceFormat.DELTA)
                .setStorageLocation("abfss://container@account.dfs.core.windows.net/prefix/orders");

        GenerateTemporaryTableCredentialResponse creds = new GenerateTemporaryTableCredentialResponse()
                .setAzureUserDelegationSas(new AzureUserDelegationSas().setSasToken("sv=2022-11-02&sig=fakesignature"));

        new Expectations() {
            {
                client.getTemporaryTableCredentials("abc-azure", "READ");
                result = creds;
                client.getMetastoreSummary();
                times = 0;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        CloudConfiguration cc = metastore.resolveCloudConfiguration(info);
        Assertions.assertEquals(CloudType.AZURE, cc.getCloudType());
    }

    @Test
    public void testResolveCloudConfigurationReturnsNullWhenDisabled(@Mocked UnityCatalogClient client) {
        TableInfo info = deltaTable("abc-123");

        new Expectations() {
            {
                client.getTemporaryTableCredentials(anyString, anyString);
                times = 0;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        Assertions.assertNull(metastore.resolveCloudConfiguration(info));
    }

    @Test
    public void testGetMetastoreTableRejectsNonDelta(@Mocked UnityCatalogClient client) {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.t")
                .setTableId("id")
                .setDataSourceFormat(DataSourceFormat.ICEBERG)
                .setStorageLocation("s3://bucket/t");

        new Expectations() {
            {
                client.getTable("main.sales.t");
                result = info;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metastore.getMetastoreTable("sales", "t"));
    }

    @Test
    public void testGetMetastoreTableRejectsMissingLocation(@Mocked UnityCatalogClient client) {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.managed_tbl")
                .setTableId("id")
                .setDataSourceFormat(DataSourceFormat.DELTA);

        new Expectations() {
            {
                client.getTable("main.sales.managed_tbl");
                result = info;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(false));
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metastore.getMetastoreTable("sales", "managed_tbl"));
    }

    @Test
    public void testResolveCloudConfigurationThrowsOnVendingFailure(@Mocked UnityCatalogClient client) {
        TableInfo info = deltaTable("abc-123");

        new Expectations() {
            {
                client.getTemporaryTableCredentials("abc-123", "READ");
                result = new StarRocksConnectorException("forbidden");
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metastore.resolveCloudConfiguration(info));
        Assertions.assertTrue(ex.getMessage().contains("main.sales.orders"),
                "exception message must include the failing table name; was: " + ex.getMessage());
    }

    @Test
    public void testResolveCloudConfigurationThrowsWhenTableIdMissing(@Mocked UnityCatalogClient client) {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders")
                .setDataSourceFormat(DataSourceFormat.DELTA)
                .setStorageLocation("s3://bucket/prefix/orders");

        new Expectations() {
            {
                client.getTemporaryTableCredentials(anyString, anyString);
                times = 0;
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithVendedCredentials(true));
        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metastore.resolveCloudConfiguration(info));
        Assertions.assertTrue(ex.getMessage().contains("main.sales.orders"),
                "exception message must include the failing table name; was: " + ex.getMessage());
    }

    @Test
    public void testResolveCloudConfigurationThrowsWhenTranslationProducesNothing(
            @Mocked UnityCatalogClient client) {
        TableInfo info = deltaTable("abc-123");

        new Expectations() {
            {
                // UC returned a successful response but populated none of the AWS/Azure/GCP
                // credential fields. The translator yields a DEFAULT/empty CloudConfiguration;
                // we expect the metastore to surface that as a hard failure.
                client.getTemporaryTableCredentials("abc-123", "READ");
                result = new GenerateTemporaryTableCredentialResponse();
            }
        };

        UnityMetastore metastore = new UnityMetastore(client, propsWithAwsRegionOverride("us-east-1"));
        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> metastore.resolveCloudConfiguration(info));
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
