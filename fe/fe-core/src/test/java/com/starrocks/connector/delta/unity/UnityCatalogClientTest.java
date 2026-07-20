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

import com.databricks.sdk.WorkspaceClient;
import com.databricks.sdk.core.error.platform.NotFound;
import com.databricks.sdk.service.catalog.AwsCredentials;
import com.databricks.sdk.service.catalog.DataSourceFormat;
import com.databricks.sdk.service.catalog.GenerateTemporaryTableCredentialRequest;
import com.databricks.sdk.service.catalog.GenerateTemporaryTableCredentialResponse;
import com.databricks.sdk.service.catalog.GetMetastoreSummaryResponse;
import com.databricks.sdk.service.catalog.ListSchemasRequest;
import com.databricks.sdk.service.catalog.ListTablesRequest;
import com.databricks.sdk.service.catalog.MetastoresAPI;
import com.databricks.sdk.service.catalog.SchemaInfo;
import com.databricks.sdk.service.catalog.SchemasAPI;
import com.databricks.sdk.service.catalog.TableInfo;
import com.databricks.sdk.service.catalog.TableOperation;
import com.databricks.sdk.service.catalog.TablesAPI;
import com.databricks.sdk.service.catalog.TemporaryTableCredentialsAPI;
import com.google.common.collect.ImmutableList;
import com.starrocks.connector.exception.StarRocksConnectorException;
import mockit.Expectations;
import mockit.Mocked;
import mockit.Verifications;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import java.util.List;

public class UnityCatalogClientTest {

    @Test
    public void testListSchemasDelegatesToSdk(@Mocked WorkspaceClient ws,
                                              @Mocked SchemasAPI schemas) {
        SchemaInfo s1 = new SchemaInfo().setName("sales");
        SchemaInfo s2 = new SchemaInfo().setName("marketing");
        new Expectations() {
            {
                ws.schemas();
                result = schemas;
                schemas.list((ListSchemasRequest) any);
                result = ImmutableList.of(s1, s2);
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws);
        List<SchemaInfo> result = client.listSchemas("main");
        Assertions.assertEquals(2, result.size());
        Assertions.assertEquals("sales", result.get(0).getName());

        new Verifications() {
            {
                ListSchemasRequest captured;
                schemas.list(captured = withCapture());
                Assertions.assertEquals("main", captured.getCatalogName());
            }
        };
    }

    @Test
    public void testListTablesDelegatesToSdk(@Mocked WorkspaceClient ws,
                                             @Mocked TablesAPI tables) {
        TableInfo t1 = new TableInfo().setName("orders").setDataSourceFormat(DataSourceFormat.DELTA);
        TableInfo t2 = new TableInfo().setName("iceberg_tbl").setDataSourceFormat(DataSourceFormat.ICEBERG);
        new Expectations() {
            {
                ws.tables();
                result = tables;
                tables.list((ListTablesRequest) any);
                result = ImmutableList.of(t1, t2);
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws);
        List<TableInfo> result = client.listTables("main", "sales");
        Assertions.assertEquals(2, result.size(), "client must not filter by data_source_format");

        new Verifications() {
            {
                ListTablesRequest captured;
                tables.list(captured = withCapture());
                Assertions.assertEquals("main", captured.getCatalogName());
                Assertions.assertEquals("sales", captured.getSchemaName());
            }
        };
    }

    @Test
    public void testGetTableDelegatesToSdk(@Mocked WorkspaceClient ws,
                                           @Mocked TablesAPI tables) {
        TableInfo info = new TableInfo()
                .setFullName("main.sales.orders")
                .setTableId("abc-123")
                .setDataSourceFormat(DataSourceFormat.DELTA)
                .setStorageLocation("s3://bucket/prefix/orders");
        new Expectations() {
            {
                ws.tables();
                result = tables;
                tables.get("main.sales.orders");
                result = info;
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws);
        TableInfo got = client.getTable("main.sales.orders");
        Assertions.assertEquals("abc-123", got.getTableId());
        Assertions.assertEquals("s3://bucket/prefix/orders", got.getStorageLocation());
        Assertions.assertEquals(DataSourceFormat.DELTA, got.getDataSourceFormat());
    }

    @Test
    public void testTableExistsTrueWhenSdkReturns(@Mocked WorkspaceClient ws,
                                                  @Mocked TablesAPI tables) {
        new Expectations() {
            {
                ws.tables();
                result = tables;
                tables.get("main.sales.orders");
                result = new TableInfo().setFullName("main.sales.orders");
            }
        };
        UnityCatalogClient client = new UnityCatalogClient(ws);
        Assertions.assertTrue(client.tableExists("main.sales.orders"));
    }

    @Test
    public void testTableExistsFalseOnNotFound(@Mocked WorkspaceClient ws,
                                               @Mocked TablesAPI tables) {
        new Expectations() {
            {
                ws.tables();
                result = tables;
                tables.get("main.sales.missing");
                result = new NotFound("table missing", null);
            }
        };
        UnityCatalogClient client = new UnityCatalogClient(ws);
        Assertions.assertFalse(client.tableExists("main.sales.missing"));
    }

    @Test
    public void testGetTemporaryTableCredentialsAws(@Mocked WorkspaceClient ws,
                                                    @Mocked TemporaryTableCredentialsAPI temp) {
        GenerateTemporaryTableCredentialResponse resp = new GenerateTemporaryTableCredentialResponse()
                .setAwsTempCredentials(new AwsCredentials()
                        .setAccessKeyId("AKIA")
                        .setSecretAccessKey("s")
                        .setSessionToken("t"))
                .setExpirationTime(1_700_000_000_000L);
        new Expectations() {
            {
                ws.temporaryTableCredentials();
                result = temp;
                temp.generateTemporaryTableCredentials((GenerateTemporaryTableCredentialRequest) any);
                result = resp;
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws);
        GenerateTemporaryTableCredentialResponse creds = client.getTemporaryTableCredentials("abc", "READ");
        Assertions.assertNotNull(creds.getAwsTempCredentials());
        Assertions.assertEquals("AKIA", creds.getAwsTempCredentials().getAccessKeyId());
        Assertions.assertEquals(Long.valueOf(1_700_000_000_000L), creds.getExpirationTime());

        new Verifications() {
            {
                GenerateTemporaryTableCredentialRequest captured;
                temp.generateTemporaryTableCredentials(captured = withCapture());
                Assertions.assertEquals("abc", captured.getTableId());
                Assertions.assertEquals(TableOperation.READ, captured.getOperation());
            }
        };
    }

    @Test
    public void testGetTemporaryTableCredentialsRejectsUnknownOperation(@Mocked WorkspaceClient ws) {
        UnityCatalogClient client = new UnityCatalogClient(ws);
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> client.getTemporaryTableCredentials("abc", "BOGUS"));
    }

    @Test
    public void testGetMetastoreSummaryDelegates(@Mocked WorkspaceClient ws,
                                                 @Mocked MetastoresAPI metastores) {
        GetMetastoreSummaryResponse summary = new GetMetastoreSummaryResponse().setRegion("eu-central-1");
        new Expectations() {
            {
                ws.metastores();
                result = metastores;
                metastores.summary();
                result = summary;
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws);
        Assertions.assertEquals("eu-central-1", client.getMetastoreSummary().getRegion());
    }
}
