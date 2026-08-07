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
import com.databricks.sdk.service.catalog.DataSourceFormat;
import com.databricks.sdk.service.catalog.GetMetastoreSummaryResponse;
import com.databricks.sdk.service.catalog.ListSchemasRequest;
import com.databricks.sdk.service.catalog.ListTablesRequest;
import com.databricks.sdk.service.catalog.MetastoresAPI;
import com.databricks.sdk.service.catalog.SchemaInfo;
import com.databricks.sdk.service.catalog.SchemasAPI;
import com.databricks.sdk.service.catalog.TableInfo;
import com.databricks.sdk.service.catalog.TablesAPI;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.starrocks.connector.exception.StarRocksConnectorException;
import io.unitycatalog.client.ApiClient;
import io.unitycatalog.client.ApiException;
import io.unitycatalog.client.delta.api.DeltaTablesApi;
import io.unitycatalog.client.delta.api.DeltaTemporaryCredentialsApi;
import io.unitycatalog.client.delta.model.DeltaCredentialOperation;
import io.unitycatalog.client.delta.model.DeltaCredentialsResponse;
import io.unitycatalog.client.delta.model.DeltaLoadTableResponse;
import mockit.Expectations;
import mockit.Mocked;
import mockit.Verifications;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.MethodSource;

import java.net.URI;
import java.net.http.HttpRequest;
import java.util.List;
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

    @Test
    public void testLoadTableUnavailableWhenDeltaClientMissing(@Mocked WorkspaceClient ws) {
        UnityCatalogClient client = new UnityCatalogClient(ws);
        Assertions.assertThrows(StarRocksConnectorException.class,
                () -> client.loadTable("main", "sales", "orders"));
    }

    private static Stream<Arguments> loadTableApiErrorCases() {
        // Only 4xx responses get the "not a Delta table" hint appended; 5xx are wrapped verbatim.
        return Stream.of(
                Arguments.of(400, true),
                Arguments.of(404, true),
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
}
