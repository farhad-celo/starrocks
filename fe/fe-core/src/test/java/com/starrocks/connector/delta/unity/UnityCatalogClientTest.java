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
import com.databricks.sdk.core.DatabricksException;
import com.databricks.sdk.core.error.platform.NotFound;
import com.databricks.sdk.service.catalog.ColumnInfo;
import com.databricks.sdk.service.catalog.ColumnTypeName;
import com.databricks.sdk.service.catalog.DataSourceFormat;
import com.databricks.sdk.service.catalog.GetMetastoreSummaryResponse;
import com.databricks.sdk.service.catalog.ListSchemasRequest;
import com.databricks.sdk.service.catalog.ListTablesRequest;
import com.databricks.sdk.service.catalog.MetastoresAPI;
import com.databricks.sdk.service.catalog.SchemaInfo;
import com.databricks.sdk.service.catalog.SchemasAPI;
import com.databricks.sdk.service.catalog.TableInfo;
import com.databricks.sdk.service.catalog.TableType;
import com.databricks.sdk.service.catalog.TablesAPI;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.starrocks.connector.delta.DeltaLakeTableNotFoundException;
import com.starrocks.connector.exception.StarRocksConnectorException;
import io.unitycatalog.client.ApiClient;
import io.unitycatalog.client.ApiException;
import io.unitycatalog.client.api.SchemasApi;
import io.unitycatalog.client.api.TablesApi;
import io.unitycatalog.client.auth.TokenProvider;
import io.unitycatalog.client.delta.api.DeltaTablesApi;
import io.unitycatalog.client.delta.api.DeltaTemporaryCredentialsApi;
import io.unitycatalog.client.delta.model.DeltaCredentialOperation;
import io.unitycatalog.client.delta.model.DeltaCredentialsResponse;
import io.unitycatalog.client.delta.model.DeltaLoadTableResponse;
import io.unitycatalog.client.model.ListSchemasResponse;
import io.unitycatalog.client.model.ListTablesResponse;
import mockit.Expectations;
import mockit.Mocked;
import mockit.Verifications;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.MethodSource;
import org.junit.jupiter.params.provider.ValueSource;

import java.net.URI;
import java.net.http.HttpRequest;
import java.util.List;
import java.util.UUID;
import java.util.stream.Stream;

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
    public void testListSchemasDelegatesToOssClient(@Mocked WorkspaceClient ws,
                                                    @Mocked SchemasApi schemasApi,
                                                    @Mocked TablesApi tablesApi,
                                                    @Mocked DeltaTablesApi deltaTablesApi,
                                                    @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        io.unitycatalog.client.model.SchemaInfo s1 = new io.unitycatalog.client.model.SchemaInfo()
                .name("sales")
                .catalogName("main")
                .storageLocation("s3://bucket/sales");
        io.unitycatalog.client.model.SchemaInfo s2 = new io.unitycatalog.client.model.SchemaInfo()
                .name("marketing")
                .catalogName("main");
        new Expectations() {
            {
                try {
                    schemasApi.listSchemas("main", null, null);
                    result = new ListSchemasResponse().addSchemasItem(s1).nextPageToken("next");
                    schemasApi.listSchemas("main", null, "next");
                    result = new ListSchemasResponse().addSchemasItem(s2);
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = ossFlavorClient(ws, schemasApi, tablesApi, deltaTablesApi, deltaCredentialsApi);
        List<SchemaInfo> result = client.listSchemas("main");
        Assertions.assertEquals(2, result.size());
        Assertions.assertEquals("sales", result.get(0).getName());
        Assertions.assertEquals("s3://bucket/sales", result.get(0).getStorageLocation());
        Assertions.assertEquals("marketing", result.get(1).getName());

        new Verifications() {
            {
                ws.schemas();
                times = 0;
            }
        };
    }

    @Test
    public void testListTablesDelegatesToOssClient(@Mocked WorkspaceClient ws,
                                                   @Mocked SchemasApi schemasApi,
                                                   @Mocked TablesApi tablesApi,
                                                   @Mocked DeltaTablesApi deltaTablesApi,
                                                   @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        io.unitycatalog.client.model.TableInfo t1 = new io.unitycatalog.client.model.TableInfo()
                .name("orders")
                .catalogName("main")
                .schemaName("sales")
                .tableId("tbl-1")
                .tableType(io.unitycatalog.client.model.TableType.EXTERNAL)
                .dataSourceFormat(io.unitycatalog.client.model.DataSourceFormat.DELTA);
        io.unitycatalog.client.model.TableInfo t2 = new io.unitycatalog.client.model.TableInfo()
                .name("parquet_tbl")
                .catalogName("main")
                .schemaName("sales")
                .tableType(io.unitycatalog.client.model.TableType.MANAGED)
                .dataSourceFormat(io.unitycatalog.client.model.DataSourceFormat.PARQUET);
        new Expectations() {
            {
                try {
                    tablesApi.listTables("main", "sales", null, null);
                    result = new ListTablesResponse().addTablesItem(t1).nextPageToken("next");
                    tablesApi.listTables("main", "sales", null, "next");
                    result = new ListTablesResponse().addTablesItem(t2);
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = ossFlavorClient(ws, schemasApi, tablesApi, deltaTablesApi, deltaCredentialsApi);
        List<TableInfo> result = client.listTables("main", "sales");
        Assertions.assertEquals(2, result.size(), "client must not filter by data_source_format");
        Assertions.assertEquals("main.sales.orders", result.get(0).getFullName());
        Assertions.assertEquals(TableType.EXTERNAL, result.get(0).getTableType());
        Assertions.assertEquals(DataSourceFormat.DELTA, result.get(0).getDataSourceFormat());
        Assertions.assertEquals(DataSourceFormat.PARQUET, result.get(1).getDataSourceFormat());

        new Verifications() {
            {
                ws.tables();
                times = 0;
            }
        };
    }

    @ParameterizedTest(name = "{1}")
    @CsvSource({
            "true,  catalog API failure",
            "false, OAuth token failure"
    })
    public void testOssFlavorListTablesWrapsFailures(boolean apiFailure, String message,
                                                      @Mocked SchemasApi schemasApi,
                                                      @Mocked TablesApi tablesApi,
                                                      @Mocked DeltaTablesApi deltaTablesApi,
                                                      @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        new Expectations() {
            {
                try {
                    tablesApi.listTables("main", "sales", null, null);
                    result = apiFailure ? new ApiException(500, message) : new RuntimeException(message);
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = ossFlavorClient(
                null, schemasApi, tablesApi, deltaTablesApi, deltaCredentialsApi);
        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> client.listTables("main", "sales"));
        Assertions.assertTrue(ex.getMessage().contains("listTables(main.sales)"));
        Assertions.assertTrue(ex.getMessage().contains(message));
    }

    @Test
    public void testListTablesMapsOssViewType(
            @Mocked SchemasApi schemasApi,
            @Mocked TablesApi tablesApi,
            @Mocked DeltaTablesApi deltaTablesApi,
            @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        io.unitycatalog.client.model.TableInfo viewSummary = new io.unitycatalog.client.model.TableInfo()
                .name("orders_view")
                .catalogName("main")
                .schemaName("sales")
                .tableType(io.unitycatalog.client.model.TableType.VIEW);

        new Expectations() {
            {
                try {
                    tablesApi.listTables("main", "sales", null, null);
                    result = new ListTablesResponse().addTablesItem(viewSummary);
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = ossFlavorClient(
                null, schemasApi, tablesApi, deltaTablesApi, deltaCredentialsApi);
        List<TableInfo> result = client.listTables("main", "sales");

        Assertions.assertEquals(1, result.size());
        Assertions.assertEquals(TableType.VIEW, result.get(0).getTableType());
    }

    @Test
    public void testGetTableInfoDelegatesToOssClient(@Mocked WorkspaceClient ws,
                                                     @Mocked SchemasApi schemasApi,
                                                     @Mocked TablesApi tablesApi,
                                                     @Mocked DeltaTablesApi deltaTablesApi,
                                                     @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        io.unitycatalog.client.model.TableInfo table = new io.unitycatalog.client.model.TableInfo()
                .name("orders")
                .catalogName("main")
                .schemaName("sales")
                .tableId("tbl-1")
                .tableType(io.unitycatalog.client.model.TableType.EXTERNAL)
                .dataSourceFormat(io.unitycatalog.client.model.DataSourceFormat.DELTA)
                .addColumnsItem(new io.unitycatalog.client.model.ColumnInfo()
                        .name("id")
                        .typeText("INT")
                        .typeName(io.unitycatalog.client.model.ColumnTypeName.INT)
                        .typeJson("\"integer\"")
                        .position(0)
                        .nullable(false));
        new Expectations() {
            {
                try {
                    tablesApi.getTable("main.sales.orders", null, null);
                    result = table;
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = ossFlavorClient(ws, schemasApi, tablesApi, deltaTablesApi, deltaCredentialsApi);
        TableInfo result = client.getTableInfo("main.sales.orders");
        Assertions.assertEquals("main.sales.orders", result.getFullName());
        Assertions.assertEquals("tbl-1", result.getTableId());
        Assertions.assertEquals(TableType.EXTERNAL, result.getTableType());
        ColumnInfo column = ImmutableList.copyOf(result.getColumns()).get(0);
        Assertions.assertEquals("id", column.getName());
        Assertions.assertEquals(ColumnTypeName.INT, column.getTypeName());
        Assertions.assertEquals(0L, column.getPosition());
        Assertions.assertFalse(column.getNullable());
    }

    @Test
    public void testGetTableInfoMapsOssViewType(
            @Mocked SchemasApi schemasApi,
            @Mocked TablesApi tablesApi,
            @Mocked DeltaTablesApi deltaTablesApi,
            @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        io.unitycatalog.client.model.TableInfo view = new io.unitycatalog.client.model.TableInfo()
                .name("orders_view")
                .catalogName("main")
                .schemaName("sales")
                .tableType(io.unitycatalog.client.model.TableType.VIEW)
                .viewDefinition("SELECT * FROM sales.orders");
        new Expectations() {
            {
                try {
                    tablesApi.getTable("main.sales.orders_view", null, null);
                    result = view;
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = ossFlavorClient(
                null, schemasApi, tablesApi, deltaTablesApi, deltaCredentialsApi);

        Assertions.assertEquals(TableType.VIEW,
                client.getTableInfo("main.sales.orders_view").getTableType());
    }

    @Test
    public void testTableExistsDelegatesToOssClient(@Mocked WorkspaceClient ws,
                                                    @Mocked SchemasApi schemasApi,
                                                    @Mocked TablesApi tablesApi,
                                                    @Mocked DeltaTablesApi deltaTablesApi,
                                                    @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        new Expectations() {
            {
                try {
                    tablesApi.getTable("main.sales.orders", null, null);
                    result = new io.unitycatalog.client.model.TableInfo().name("orders");
                    tablesApi.getTable("main.sales.missing", null, null);
                    result = new ApiException(404, "missing");
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = ossFlavorClient(ws, schemasApi, tablesApi, deltaTablesApi, deltaCredentialsApi);
        Assertions.assertTrue(client.tableExists("main.sales.orders"));
        Assertions.assertFalse(client.tableExists("main.sales.missing"));
    }

    @Test
    public void testOssFlavorListSchemasWrapsRuntimeFailures(
            @Mocked SchemasApi schemasApi,
            @Mocked TablesApi tablesApi,
            @Mocked DeltaTablesApi deltaTablesApi,
            @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        new Expectations() {
            {
                try {
                    schemasApi.listSchemas("main", null, null);
                    result = new RuntimeException("Failed to renew OAuth token");
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = ossFlavorClient(null, schemasApi, tablesApi, deltaTablesApi, deltaCredentialsApi);
        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> client.listSchemas("main"));
        Assertions.assertTrue(ex.getMessage().contains("listSchemas(main)"));
        Assertions.assertTrue(ex.getMessage().contains("Failed to renew OAuth token"));
    }

    @Test
    public void testOssFlavorGetMetastoreSummaryFailsWithoutDatabricksSdk(
            @Mocked SchemasApi schemasApi,
            @Mocked TablesApi tablesApi,
            @Mocked DeltaTablesApi deltaTablesApi,
            @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        UnityCatalogClient client = ossFlavorClient(null, schemasApi, tablesApi, deltaTablesApi, deltaCredentialsApi);

        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                client::getMetastoreSummary);

        Assertions.assertTrue(ex.getMessage().contains("metastores.summary"));
        Assertions.assertTrue(ex.getMessage().contains("unity.catalog.flavor=oss"));
    }

    private static Stream<Arguments> tableExistsCases() {
        // A returned TableInfo means the table exists; a thrown NotFound means it does not.
        return Stream.of(
                Arguments.of("main.sales.orders", new TableInfo().setFullName("main.sales.orders"), true),
                Arguments.of("main.sales.missing", new NotFound("table missing", null), false));
    }

    @ParameterizedTest
    @MethodSource("tableExistsCases")
    public void testTableExists(String fullName, Object sdkResult, boolean expected,
                                @Mocked WorkspaceClient ws, @Mocked TablesAPI tables) {
        new Expectations() {
            {
                ws.tables();
                result = tables;
                tables.get(fullName);
                result = sdkResult;
            }
        };
        UnityCatalogClient client = new UnityCatalogClient(ws);
        Assertions.assertEquals(expected, client.tableExists(fullName));
    }

    @Test
    public void testGetTableInfoDelegatesToSdk(@Mocked WorkspaceClient ws,
                                               @Mocked TablesAPI tables) {
        TableInfo info = new TableInfo().setFullName("main.sales.orders_view");
        new Expectations() {
            {
                ws.tables();
                result = tables;
                tables.get("main.sales.orders_view");
                result = info;
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws);
        Assertions.assertSame(info, client.getTableInfo("main.sales.orders_view"));
    }

    @Test
    public void testGetTableInfoWrapsSdkError(@Mocked WorkspaceClient ws,
                                              @Mocked TablesAPI tables) {
        new Expectations() {
            {
                ws.tables();
                result = tables;
                tables.get("main.sales.orders_view");
                result = new DatabricksException("permission denied");
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws);
        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> client.getTableInfo("main.sales.orders_view"));

        Assertions.assertTrue(ex.getMessage().contains("getTable(main.sales.orders_view"),
                "wrapped message must identify the table; was: " + ex.getMessage());
        Assertions.assertTrue(ex.getMessage().contains("permission denied"),
                "wrapped message must include the SDK error; was: " + ex.getMessage());
    }

    @Test
    public void testLoadTableDelegatesToDeltaApi(@Mocked WorkspaceClient ws,
                                                 @Mocked DeltaTablesApi deltaTablesApi,
                                                 @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        DeltaLoadTableResponse resp = UnityDeltaModelFixtures.loadResponse(
                "s3://bucket/prefix/orders", 1_700_000_000_000L, null);
        new Expectations() {
            {
                try {
                    deltaTablesApi.loadTable("main", "sales", "orders");
                    result = resp;
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws, deltaTablesApi, deltaCredentialsApi);
        Assertions.assertSame(resp, client.loadTable("main", "sales", "orders"));
    }

    @ParameterizedTest(name = "tableId=''{0}''")
    @CsvSource({
            "11111111-1111-1111-1111-111111111111, true",
            "not-a-uuid,                           false",
            "'',                                   false"
    })
    public void testFabricLoadTableUsesCatalogGetTable(String rawTableId, boolean validTableId,
                                                        @Mocked SchemasApi schemasApi,
                                                        @Mocked TablesApi tablesApi,
                                                        @Mocked DeltaTablesApi deltaTablesApi,
                                                        @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        UUID tableId = validTableId ? UUID.fromString(rawTableId) : null;
        io.unitycatalog.client.model.TableInfo table = new io.unitycatalog.client.model.TableInfo()
                .catalogName("lakehouse")
                .schemaName("dbo")
                .name("orders")
                .tableId(rawTableId)
                .dataSourceFormat(io.unitycatalog.client.model.DataSourceFormat.DELTA)
                .storageLocation("https://onelake.dfs.fabric.microsoft.com/workspace/item/Tables/orders")
                .createdAt(1_700_000_000_000L)
                .updatedAt(1_700_000_001_000L);
        new Expectations() {
            {
                try {
                    tablesApi.getTable("lakehouse.dbo.orders", null, null);
                    result = table;
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(null, schemasApi, tablesApi,
                deltaTablesApi, deltaCredentialsApi, UnityCatalogProperties.Flavor.FABRIC);
        DeltaLoadTableResponse response = client.loadTable("lakehouse", "dbo", "orders");

        Assertions.assertEquals("abfss://workspace@onelake.dfs.fabric.microsoft.com/item/Tables/orders",
                response.getMetadata().getLocation());
        Assertions.assertEquals(tableId, response.getMetadata().getTableUuid());
        Assertions.assertEquals(table.getCreatedAt(), response.getMetadata().getCreatedTime());
        Assertions.assertTrue(response.getCommits().isEmpty());
        Assertions.assertNull(response.getLatestTableVersion());
        new Verifications() {
            {
                try {
                    deltaTablesApi.loadTable(anyString, anyString, anyString);
                    times = 0;
                } catch (ApiException e) {
                    // verification only; not thrown here
                }
            }
        };
    }

    @Test
    public void testFabricLoadTablePreservesNotFound(@Mocked SchemasApi schemasApi,
                                                      @Mocked TablesApi tablesApi,
                                                      @Mocked DeltaTablesApi deltaTablesApi,
                                                      @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        new Expectations() {
            {
                try {
                    tablesApi.getTable("lakehouse.dbo.missing", null, null);
                    result = new ApiException(404, "missing");
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(null, schemasApi, tablesApi,
                deltaTablesApi, deltaCredentialsApi, UnityCatalogProperties.Flavor.FABRIC);
        Assertions.assertThrows(DeltaLakeTableNotFoundException.class,
                () -> client.loadTable("lakehouse", "dbo", "missing"));
    }

    @ParameterizedTest(name = "location=''{0}''")
    @ValueSource(strings = {
            "",
            "not a valid URI [",
            "abfss://workspace@account.dfs.core.windows.net/item/Tables/orders",
            "s3://bucket/orders",
            "https://example.com/workspace/item",
            "https:/workspace/item",
            "https://onelake.dfs.fabric.microsoft.com/workspace",
            "https://onelake.dfs.fabric.microsoft.com/workspace/"
    })
    public void testToOneLakeAbfssUriLeavesOtherLocationsUnchanged(String location) {
        Assertions.assertEquals(location, UnityCatalogClient.toOneLakeAbfssUri(location));
    }

    @Test
    public void testLoadTableUnavailableWhenDeltaClientMissing(@Mocked WorkspaceClient ws) {
        UnityCatalogClient client = new UnityCatalogClient(ws);
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> client.loadTable("main", "sales", "orders"));
    }

    private static Stream<Arguments> loadTableApiErrorCases() {
        // Non-404 4xx responses get the "not a Delta table" hint appended; 5xx are wrapped verbatim.
        // 404 is handled separately (typed not-found), see testLoadTableThrowsNotFoundOn404.
        return Stream.of(
                Arguments.of(400, true),
                Arguments.of(500, false));
    }

    @ParameterizedTest(name = "HTTP {0}")
    @MethodSource("loadTableApiErrorCases")
    public void testLoadTableWrapsApiException(int httpCode, boolean expectFormatHint,
                                               @Mocked WorkspaceClient ws,
                                               @Mocked DeltaTablesApi deltaTablesApi,
                                               @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        new Expectations() {
            {
                try {
                    deltaTablesApi.loadTable("main", "sales", "orders");
                    result = new ApiException(httpCode, "boom");
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws, deltaTablesApi, deltaCredentialsApi);
        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> client.loadTable("main", "sales", "orders"));

        Assertions.assertTrue(ex.getMessage().contains("loadTable(main.sales.orders"),
                "wrapped message must identify the table; was: " + ex.getMessage());
        Assertions.assertTrue(ex.getMessage().contains("HTTP " + httpCode),
                "wrapped message must include the HTTP code; was: " + ex.getMessage());
        Assertions.assertEquals(expectFormatHint,
                ex.getMessage().contains("verify the table exists and its data source format is Delta"),
                "data-source-format hint must appear only for 4xx; was: " + ex.getMessage());
    }

    @Test
    public void testLoadTableMarksUnsupportedTableFormat(@Mocked WorkspaceClient ws,
                                                         @Mocked DeltaTablesApi deltaTablesApi,
                                                         @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        new Expectations() {
            {
                try {
                    deltaTablesApi.loadTable("main", "sales", "orders_view");
                    result = new ApiException(400, "loadTable call failed", null,
                            "{\"error\":{\"type\":\"UnsupportedTableFormatException\","
                                    + "\"message\":\"VIEW UNKNOWN_DATA_SOURCE_FORMAT\"}}");
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws, deltaTablesApi, deltaCredentialsApi);
        UnityCatalogUnsupportedTableFormatException ex =
                Assertions.assertThrows(UnityCatalogUnsupportedTableFormatException.class,
                        () -> client.loadTable("main", "sales", "orders_view"));
        Assertions.assertTrue(ex.getMessage().contains("loadTable(main.sales.orders_view"));
    }

    @Test
    public void testLoadTableMarksUnsupportedTableFormatWhenBodyIsOnlyInMessage(
            @Mocked WorkspaceClient ws,
            @Mocked DeltaTablesApi deltaTablesApi,
            @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        new Expectations() {
            {
                try {
                    deltaTablesApi.loadTable("main", "sales", "orders_view");
                    result = new ApiException(400, "loadTable call failed with: 400 - "
                            + "{\"error\":{\"message\":\"This table is a view; retrieve its definition "
                            + "through the Unity SDK endpoint\",\"type\":\"UnsupportedTableFormatException\","
                            + "\"code\":400,\"stack\":[]}}");
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws, deltaTablesApi, deltaCredentialsApi);
        Assertions.assertThrows(UnityCatalogUnsupportedTableFormatException.class,
                () -> client.loadTable("main", "sales", "orders_view"));
    }

    @Test
    public void testLoadTableUsesGenericErrorWhenUnsupportedFormatBodyCannotBeParsed(
            @Mocked WorkspaceClient ws,
            @Mocked DeltaTablesApi deltaTablesApi,
            @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        new Expectations() {
            {
                try {
                    deltaTablesApi.loadTable("main", "sales", "orders_view");
                    result = new ApiException(400, "loadTable call failed", null, "{not-json");
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws, deltaTablesApi, deltaCredentialsApi);
        StarRocksConnectorException ex = Assertions.assertThrows(StarRocksConnectorException.class,
                () -> client.loadTable("main", "sales", "orders_view"));
        Assertions.assertFalse(ex instanceof UnityCatalogUnsupportedTableFormatException);
        Assertions.assertTrue(ex.getMessage().contains("loadTable(main.sales.orders_view"));
    }

    @Test
    public void testLoadTableThrowsNotFoundOn404(@Mocked WorkspaceClient ws,
                                                 @Mocked DeltaTablesApi deltaTablesApi,
                                                 @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        new Expectations() {
            {
                try {
                    deltaTablesApi.loadTable("main", "sales", "orders");
                    result = new ApiException(404, "not found");
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws, deltaTablesApi, deltaCredentialsApi);
        DeltaLakeTableNotFoundException ex = Assertions.assertThrows(DeltaLakeTableNotFoundException.class,
                () -> client.loadTable("main", "sales", "orders"));
        Assertions.assertTrue(ex.getMessage().contains("main.sales.orders"),
                "not-found message must identify the table; was: " + ex.getMessage());
        Assertions.assertFalse(ex.getMessage().contains("verify the table exists and its data source format is Delta"),
                "a genuine 404 must not carry the not-a-Delta-table hint; was: " + ex.getMessage());
    }

    @Test
    public void testGetTableCredentialsDelegatesToDeltaApi(@Mocked WorkspaceClient ws,
                                                           @Mocked DeltaTablesApi deltaTablesApi,
                                                           @Mocked DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        DeltaCredentialsResponse resp = UnityDeltaModelFixtures.awsCredsResponse(1_700_000_000_000L);
        new Expectations() {
            {
                try {
                    deltaCredentialsApi.getTableCredentials(DeltaCredentialOperation.READ, "main", "sales", "orders");
                    result = resp;
                } catch (ApiException e) {
                    // recording only; not thrown here
                }
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws, deltaTablesApi, deltaCredentialsApi);
        Assertions.assertSame(resp, client.getTableCredentials("main", "sales", "orders", "tbl-uuid", "READ"));
    }

    @Test
    public void testGetTableCredentialsRejectsUnknownOperation(@Mocked WorkspaceClient ws,
                                                               @Mocked DeltaTablesApi deltaTablesApi,
                                                               @Mocked DeltaTemporaryCredentialsApi credsApi) {
        UnityCatalogClient client = new UnityCatalogClient(ws, deltaTablesApi, credsApi);
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> client.getTableCredentials("main", "sales", "orders", "tbl-uuid", "BOGUS"));
    }

    @Test
    public void testGetTableCredentialsUnavailableWhenDeltaClientMissing(@Mocked WorkspaceClient ws) {
        UnityCatalogClient client = new UnityCatalogClient(ws);
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> client.getTableCredentials("main", "sales", "orders", "tbl-uuid", "READ"));
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

    @Test
    public void testZeroMaxRetriesBuildsClient() {
        // JitterDelayRetryPolicy rejects maxAttempts=0, so max-retries=0 must be translated into a
        // total-attempts count of 1 rather than being passed through verbatim.
        UnityCatalogProperties props = new UnityCatalogProperties(ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiTEST",
                "unity.catalog.name", "main",
                "unity.catalog.max-retries", "0"));
        Assertions.assertDoesNotThrow(() -> new UnityCatalogClient(props));
    }

    @Test
    public void testOssFlavorDoesNotBuildDatabricksWorkspaceClient() {
        UnityCatalogProperties props = new UnityCatalogProperties(ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "http://localhost:1")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.flavor", "oss")
                .put("unity.catalog.auth.type", "oauth-m2m")
                .put("unity.catalog.client-id", "client-id")
                .put("unity.catalog.client-secret", "client-secret")
                .put("unity.catalog.oauth-token-url", "http://localhost:1/oauth/token")
                .put("unity.catalog.oauth-scope", "uc.read")
                .put("unity.catalog.request-timeout-ms", "1")
                .put("unity.catalog.max-retries", "0")
                .build());
        Assertions.assertDoesNotThrow(() -> new UnityCatalogClient(props));
    }

    @Test
    public void testOAuthTokenProviderUsesConfiguredEndpointAndScope() {
        UnityCatalogProperties props = new UnityCatalogProperties(ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "http://localhost:1")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.flavor", "oss")
                .put("unity.catalog.auth.type", "oauth-m2m")
                .put("unity.catalog.client-id", "client-id")
                .put("unity.catalog.client-secret", "client-secret")
                .put("unity.catalog.oauth-token-url", "http://localhost:1/oauth2/token")
                .put("unity.catalog.oauth-scope", "uc.read")
                .build());

        TokenProvider provider = UnityCatalogClient.tokenProvider(props);
        Assertions.assertEquals("http://localhost:1/oauth2/token", provider.configs().get("oauth.uri"));
        Assertions.assertEquals("uc.read", provider.configs().get("oauth.scope"));
    }

    @Test
    public void testDefaultUserAgentUsesStarRocks() {
        String userAgent = userAgent(propsWithUserAgentSpoof(false));
        Assertions.assertTrue(userAgent.contains("Delta/4.3.0"));
        Assertions.assertTrue(userAgent.contains("StarRocks/3.5"));
        Assertions.assertTrue(userAgent.contains("Java/" + UnityCatalogClient.javaAppVersion()));
        Assertions.assertFalse(userAgent.contains("Spark/4.0.0"), userAgent);
        Assertions.assertFalse(userAgent.contains("Scala/2.13.16"), userAgent);
    }

    @Test
    public void testSpoofUserAgentPreservesSparkUserAgent() {
        String userAgent = userAgent(propsWithUserAgentSpoof(true));
        Assertions.assertTrue(userAgent.contains("Delta/4.3.0"));
        Assertions.assertTrue(userAgent.contains("Spark/4.0.0"));
        Assertions.assertTrue(userAgent.contains("Scala/2.13.16"));
        Assertions.assertTrue(userAgent.contains("Java/17.0.19"));
        Assertions.assertFalse(userAgent.contains("StarRocks/"), userAgent);
    }

    @ParameterizedTest(name = "[{index}] ''{0}'' -> ''{1}''")
    @CsvSource({
            "cat.schema.Store Name, cat.schema.Store%20Name",
            "cat.schema.a+b,        cat.schema.a%2Bb",
    })
    public void testTableExistsEncodesFullName(String input, String expectedEncoded,
                                               @Mocked WorkspaceClient ws,
                                               @Mocked TablesAPI tables) {
        new Expectations() {
            {
                ws.tables();
                result = tables;
                tables.get(anyString);
                result = new TableInfo();
            }
        };

        UnityCatalogClient client = new UnityCatalogClient(ws);
        Assertions.assertTrue(client.tableExists(input));

        new Verifications() {
            {
                tables.get(expectedEncoded);
            }
        };
    }

    private static UnityCatalogProperties propsWithUserAgentSpoof(boolean spoofUserAgent) {
        ImmutableMap.Builder<String, String> builder = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://example.cloud.databricks.com")
                .put("unity.catalog.token", "dapiTEST")
                .put("unity.catalog.name", "main");
        if (spoofUserAgent) {
            builder.put("unity.catalog.spoof-user-agent", "true");
        }
        return new UnityCatalogProperties(builder.build());
    }

    private static String userAgent(UnityCatalogProperties properties) {
        ApiClient client = UnityCatalogClient.buildDeltaApiClient(properties);
        HttpRequest.Builder requestBuilder = HttpRequest.newBuilder(URI.create("https://example.com"));
        client.getRequestInterceptor().accept(requestBuilder);
        return requestBuilder.build().headers().firstValue("User-Agent").orElseThrow();
    }

    private static UnityCatalogClient ossFlavorClient(WorkspaceClient ws,
                                                         SchemasApi schemasApi,
                                                         TablesApi tablesApi,
                                                         DeltaTablesApi deltaTablesApi,
                                                         DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        return new UnityCatalogClient(ws, schemasApi, tablesApi, deltaTablesApi, deltaCredentialsApi,
                UnityCatalogProperties.Flavor.OSS);
    }
}
