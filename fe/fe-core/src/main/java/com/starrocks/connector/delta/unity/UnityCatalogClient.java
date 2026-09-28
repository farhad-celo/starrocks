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
import com.databricks.sdk.core.DatabricksConfig;
import com.databricks.sdk.core.DatabricksException;
import com.databricks.sdk.core.error.platform.NotFound;
import com.databricks.sdk.service.catalog.ColumnInfo;
import com.databricks.sdk.service.catalog.ColumnTypeName;
import com.databricks.sdk.service.catalog.DataSourceFormat;
import com.databricks.sdk.service.catalog.GetMetastoreSummaryResponse;
import com.databricks.sdk.service.catalog.ListSchemasRequest;
import com.databricks.sdk.service.catalog.ListTablesRequest;
import com.databricks.sdk.service.catalog.SchemaInfo;
import com.databricks.sdk.service.catalog.TableInfo;
import com.databricks.sdk.service.catalog.TableType;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Strings;
import com.google.common.collect.ImmutableList;
import com.google.common.net.PercentEscaper;
import com.starrocks.connector.delta.DeltaLakeTableNotFoundException;
import com.starrocks.connector.exception.StarRocksConnectorException;
import io.unitycatalog.client.ApiClient;
import io.unitycatalog.client.ApiClientBuilder;
import io.unitycatalog.client.ApiException;
import io.unitycatalog.client.api.SchemasApi;
import io.unitycatalog.client.api.TablesApi;
import io.unitycatalog.client.auth.TokenProvider;
import io.unitycatalog.client.delta.api.DeltaTablesApi;
import io.unitycatalog.client.delta.api.DeltaTemporaryCredentialsApi;
import io.unitycatalog.client.delta.model.DeltaCredentialOperation;
import io.unitycatalog.client.delta.model.DeltaCredentialsResponse;
import io.unitycatalog.client.delta.model.DeltaLoadTableResponse;
import io.unitycatalog.client.delta.model.DeltaTableMetadata;
import io.unitycatalog.client.model.ListSchemasResponse;
import io.unitycatalog.client.model.ListTablesResponse;
import io.unitycatalog.client.retry.JitterDelayRetryPolicy;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.net.URI;
import java.net.URISyntaxException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;

/**
 * Adapter over the Databricks SDK {@link WorkspaceClient} and the generated Unity Catalog client.
 * Fabric uses the standard catalog APIs for filesystem-backed table loads; Databricks and OSS use
 * the Unity Catalog {@code delta/v1} client ({@link DeltaTablesApi},
 * {@link DeltaTemporaryCredentialsApi}) for table loading and credential vending. All flavors
 * support PAT and OAuth-M2M auth.
 */
public class UnityCatalogClient implements UnityCatalogApi {
    private static final Logger LOG = LogManager.getLogger(UnityCatalogClient.class);
    private static final PercentEscaper FULL_NAME_ESCAPER = new PercentEscaper("-_.*", false);
    private static final ObjectMapper ERROR_BODY_MAPPER = new ObjectMapper();
    private static final String UNSUPPORTED_TABLE_FORMAT = "UnsupportedTableFormatException";
    private static final String DELTA_APP_VERSION = "4.3.0";
    private static final String SPOOF_SPARK_APP_VERSION = "4.0.0";
    private static final String SPOOF_JAVA_VERSION = "17.0.19";
    private static final String SCALA_APP_VERSION = "2.13.16";
    private static final String LIST_SCHEMAS_OPERATION = "listSchemas";
    private static final String LIST_TABLES_OPERATION = "listTables";
    private static final String GET_TABLE_OPERATION = "getTable";
    private static final String TABLE_EXISTS_OPERATION = "tableExists";
    // Currently no way to retrieve this easily.
    private static final String STARROCKS_VERSION = "3.5";
    private static final long INITIAL_RETRY_DELAY_MS = 1_000L;

    private final WorkspaceClient workspace;
    private final UnityCatalogProperties.Flavor flavor;
    private final SchemasApi catalogSchemasApi;
    private final TablesApi catalogTablesApi;
    // delta/v1 read path, from the OSS unitycatalog-client. Null only in the WorkspaceClient-only
    // test constructor; loadTable / getTableCredentials then fail fast.
    private final DeltaTablesApi deltaTablesApi;
    private final DeltaTemporaryCredentialsApi deltaCredentialsApi;

    public UnityCatalogClient(UnityCatalogProperties properties) {
        this(buildWorkspaceClient(properties), buildDeltaApiClient(properties), properties.getFlavor());
    }

    private UnityCatalogClient(WorkspaceClient workspace, ApiClient deltaApiClient,
                               UnityCatalogProperties.Flavor flavor) {
        this(workspace,
                new SchemasApi(deltaApiClient),
                new TablesApi(deltaApiClient),
                new DeltaTablesApi(deltaApiClient),
                new DeltaTemporaryCredentialsApi(deltaApiClient),
                flavor);
    }

    /** Visible for testing. Lets unit tests inject a mocked or fake {@link WorkspaceClient}. */
    @VisibleForTesting
    public UnityCatalogClient(WorkspaceClient workspace) {
        this(workspace, null, null, null, null, UnityCatalogProperties.Flavor.DATABRICKS);
    }

    /** Visible for testing. Lets unit tests inject fake {@code delta/v1} clients. */
    @VisibleForTesting
    UnityCatalogClient(WorkspaceClient workspace, DeltaTablesApi deltaTablesApi,
                       DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        this(workspace, null, null, deltaTablesApi, deltaCredentialsApi,
                UnityCatalogProperties.Flavor.DATABRICKS);
    }

    /** Visible for testing. Lets unit tests inject fake generated OSS clients. */
    @VisibleForTesting
    UnityCatalogClient(WorkspaceClient workspace, SchemasApi catalogSchemasApi, TablesApi catalogTablesApi,
                       DeltaTablesApi deltaTablesApi, DeltaTemporaryCredentialsApi deltaCredentialsApi,
                       UnityCatalogProperties.Flavor flavor) {
        this.flavor = Objects.requireNonNull(flavor, "flavor");
        this.workspace = flavor == UnityCatalogProperties.Flavor.DATABRICKS
                ? Objects.requireNonNull(workspace, "workspace")
                : workspace;
        this.catalogSchemasApi = catalogSchemasApi;
        this.catalogTablesApi = catalogTablesApi;
        this.deltaTablesApi = deltaTablesApi;
        this.deltaCredentialsApi = deltaCredentialsApi;
    }

    private static WorkspaceClient buildWorkspaceClient(UnityCatalogProperties properties) {
        if (properties.getFlavor() != UnityCatalogProperties.Flavor.DATABRICKS) {
            return null;
        }
        DatabricksConfig cfg = new DatabricksConfig().setHost(properties.getHost());
        if (properties.getAuthType() == UnityCatalogProperties.AuthType.OAUTH_M2M) {
            cfg.setAuthType("oauth-m2m")
                    .setClientId(properties.getClientId())
                    .setClientSecret(properties.getClientSecret());
        } else {
            cfg.setAuthType("pat").setToken(properties.getToken());
        }
        // The SDK's HTTP timeout is in seconds; round up so very small values do not become 0.
        long timeoutSec = (properties.getRequestTimeoutMs() + 999L) / 1000L;
        if (timeoutSec > 0) {
            cfg.setHttpTimeoutSeconds((int) Math.min(Integer.MAX_VALUE, timeoutSec));
        }
        return new WorkspaceClient(cfg);
    }

    /** Build the OSS Unity Catalog {@link ApiClient} shared by the catalog and delta APIs. */
    @VisibleForTesting
    static ApiClient buildDeltaApiClient(UnityCatalogProperties properties) {
        // Databricks rejects delta/v1 with HTTP 400 unless the caller identifies itself in the
        // User-Agent; the builder emits "UnityCatalog-Java-Client/<ver> ..." app tokens.
        ApiClientBuilder builder = ApiClientBuilder.create()
                .uri(properties.getHost())
                .tokenProvider(tokenProvider(properties))
                // maxAttempts counts the initial try, and the builder rejects 0, so translate the
                // retry count into total attempts.
                .retryPolicy(JitterDelayRetryPolicy.builder()
                        .maxAttempts(properties.getMaxRetries() + 1)
                        .initDelayMs(INITIAL_RETRY_DELAY_MS)
                        .build());
        addUserAgentAppVersions(builder, properties);
        ApiClient apiClient = builder.build();

        // Databricks returns fields the OSS delta/v1 spec does not model; tolerate them.
        ObjectMapper mapper = apiClient.getObjectMapper();
        mapper.disable(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES);
        apiClient.setObjectMapper(mapper);

        long timeoutMs = properties.getRequestTimeoutMs();
        if (timeoutMs > 0) {
            Duration timeout = Duration.ofMillis(timeoutMs);
            apiClient.setConnectTimeout(timeout);
            apiClient.setReadTimeout(timeout);
        }
        return apiClient;
    }

    @VisibleForTesting
    static TokenProvider tokenProvider(UnityCatalogProperties properties) {
        Map<String, String> authConfig = new HashMap<>();
        if (properties.getAuthType() == UnityCatalogProperties.AuthType.OAUTH_M2M) {
            authConfig.put("type", "oauth");
            authConfig.put("oauth.uri", properties.getOAuthTokenUrl());
            authConfig.put("oauth.clientId", properties.getClientId());
            authConfig.put("oauth.clientSecret", properties.getClientSecret());
            authConfig.put("oauth.scope", properties.getOAuthScope());
        } else {
            authConfig.put("type", "static");
            authConfig.put("token", properties.getToken());
        }
        return TokenProvider.create(authConfig);
    }

    private static void addUserAgentAppVersions(ApiClientBuilder builder, UnityCatalogProperties properties) {
        builder.addAppVersion("Delta", DELTA_APP_VERSION);
        if (properties.isSpoofUserAgent()) {
            LOG.info("Using Spark-compatible Unity Catalog delta/v1 User-Agent because {} is enabled",
                    UnityCatalogProperties.UNITY_CATALOG_SPOOF_USER_AGENT);
            builder.addAppVersion("Spark", SPOOF_SPARK_APP_VERSION)
                    .addAppVersion("Scala", SCALA_APP_VERSION)
                    .addAppVersion("Java", SPOOF_JAVA_VERSION);
        } else {
            builder.addAppVersion("StarRocks", STARROCKS_VERSION);
            builder.addAppVersion("Java", javaAppVersion());
        }
    }

    @VisibleForTesting
    static String javaAppVersion() {
        String javaVersion = System.getProperty("java.version");
        return javaVersion == null || javaVersion.isEmpty() ? "unknown" : javaVersion;
    }

    @Override
    public List<SchemaInfo> listSchemas(String ucCatalog) {
        if (usesCatalogApi()) {
            return listSchemasViaOssClient(ucCatalog);
        }
        try {
            return ImmutableList.copyOf(
                    requireWorkspaceClient(LIST_SCHEMAS_OPERATION)
                            .schemas().list(new ListSchemasRequest().setCatalogName(ucCatalog)));
        } catch (DatabricksException e) {
            throw wrap(operationContext(LIST_SCHEMAS_OPERATION, ucCatalog), e);
        }
    }

    @Override
    public List<TableInfo> listTables(String ucCatalog, String schemaName) {
        if (usesCatalogApi()) {
            return listTablesViaOssClient(ucCatalog, schemaName);
        }
        try {
            return ImmutableList.copyOf(requireWorkspaceClient(LIST_TABLES_OPERATION).tables().list(
                    new ListTablesRequest().setCatalogName(ucCatalog).setSchemaName(schemaName)));
        } catch (DatabricksException e) {
            throw wrap(operationContext(LIST_TABLES_OPERATION, ucCatalog + "." + schemaName), e);
        }
    }

    @Override
    public TableInfo getTableInfo(String fullName) {
        if (usesCatalogApi()) {
            return getTableInfoViaOssClient(fullName);
        }
        try {
            return requireWorkspaceClient(GET_TABLE_OPERATION).tables().get(encodeFullName(fullName));
        } catch (DatabricksException e) {
            throw wrap(operationContext(GET_TABLE_OPERATION, fullName), e);
        }
    }

    @Override
    public boolean tableExists(String fullName) {
        if (usesCatalogApi()) {
            try {
                requireCatalogTablesApi(TABLE_EXISTS_OPERATION).getTable(fullName, null, null);
                return true;
            } catch (ApiException e) {
                if (e.getCode() == 404) {
                    return false;
                }
                throw wrap(operationContext(TABLE_EXISTS_OPERATION, fullName), e);
            } catch (RuntimeException e) {
                throw wrap(operationContext(TABLE_EXISTS_OPERATION, fullName), e);
            }
        }
        try {
            requireWorkspaceClient(TABLE_EXISTS_OPERATION).tables().get(encodeFullName(fullName));
            return true;
        } catch (NotFound e) {
            return false;
        } catch (DatabricksException e) {
            throw wrap(operationContext(TABLE_EXISTS_OPERATION, fullName), e);
        }
    }

    private List<SchemaInfo> listSchemasViaOssClient(String ucCatalog) {
        SchemasApi schemasApi = requireCatalogSchemasApi(LIST_SCHEMAS_OPERATION);
        List<SchemaInfo> result = new ArrayList<>();
        String pageToken = null;
        try {
            do {
                ListSchemasResponse page = schemasApi.listSchemas(ucCatalog, null, pageToken);
                if (page != null && page.getSchemas() != null) {
                    for (io.unitycatalog.client.model.SchemaInfo schema : page.getSchemas()) {
                        if (schema != null) {
                            result.add(toDatabricksSchemaInfo(schema));
                        }
                    }
                }
                pageToken = page == null ? null : emptyToNull(page.getNextPageToken());
            } while (pageToken != null);
            return ImmutableList.copyOf(result);
        } catch (ApiException e) {
            throw wrap(operationContext(LIST_SCHEMAS_OPERATION, ucCatalog), e);
        } catch (RuntimeException e) {
            throw wrap(operationContext(LIST_SCHEMAS_OPERATION, ucCatalog), e);
        }
    }

    private List<TableInfo> listTablesViaOssClient(String ucCatalog, String schemaName) {
        TablesApi tablesApi = requireCatalogTablesApi(LIST_TABLES_OPERATION);
        List<TableInfo> result = new ArrayList<>();
        String pageToken = null;
        try {
            do {
                ListTablesResponse page = tablesApi.listTables(ucCatalog, schemaName, null, pageToken);
                if (page != null && page.getTables() != null) {
                    for (io.unitycatalog.client.model.TableInfo table : page.getTables()) {
                        if (table != null) {
                            result.add(toDatabricksTableInfo(table));
                        }
                    }
                }
                pageToken = page == null ? null : emptyToNull(page.getNextPageToken());
            } while (pageToken != null);
            return ImmutableList.copyOf(result);
        } catch (ApiException e) {
            throw wrap(operationContext(LIST_TABLES_OPERATION, ucCatalog + "." + schemaName), e);
        } catch (RuntimeException e) {
            throw wrap(operationContext(LIST_TABLES_OPERATION, ucCatalog + "." + schemaName), e);
        }
    }

    private TableInfo getTableInfoViaOssClient(String fullName) {
        try {
            return toDatabricksTableInfo(requireCatalogTablesApi(GET_TABLE_OPERATION).getTable(fullName, null, null));
        } catch (ApiException e) {
            if (e.getCode() == 404) {
                throw new DeltaLakeTableNotFoundException("Unity Catalog table %s not found", fullName);
            }
            throw wrap(operationContext(GET_TABLE_OPERATION, fullName), e);
        } catch (RuntimeException e) {
            throw wrap(operationContext(GET_TABLE_OPERATION, fullName), e);
        }
    }

    @Override
    public GetMetastoreSummaryResponse getMetastoreSummary() {
        try {
            return requireWorkspaceClient("metastores.summary").metastores().summary();
        } catch (DatabricksException e) {
            throw wrap("metastores.summary", e);
        }
    }

    @Override
    public DeltaLoadTableResponse loadTable(String ucCatalog, String schemaName, String tableName) {
        if (flavor == UnityCatalogProperties.Flavor.FABRIC) {
            return loadTableViaCatalogApi(ucCatalog, schemaName, tableName);
        }
        if (deltaTablesApi == null) {
            throw new StarRocksConnectorException(
                    "Unity Catalog delta/v1 client is not configured; loadTable is unavailable");
        }
        try {
            return deltaTablesApi.loadTable(ucCatalog, schemaName, tableName);
        } catch (ApiException e) {
            if (e.getCode() == 404) {
                throw new DeltaLakeTableNotFoundException(
                        "Unity Catalog table %s.%s.%s not found", ucCatalog, schemaName, tableName);
            }
            String context = "loadTable(" + ucCatalog + "." + schemaName + "." + tableName + ")";
            // The delta/v1 endpoint only serves Delta tables; a non-404 4xx on a table accessed by
            // name most often means it is not a Delta table (Iceberg / Parquet / view).
            if (e.getCode() >= 400 && e.getCode() < 500) {
                context += "; verify the table exists and its data source format is Delta";
                String message = String.format("Unity Catalog %s failed (HTTP %s): %s",
                        context, e.getCode(), e.getMessage());
                if (isUnsupportedTableFormat(e)) {
                    throw new UnityCatalogUnsupportedTableFormatException(message, e);
                }
                throw new StarRocksConnectorException(message);
            }
            throw wrap(context, e);
        } catch (RuntimeException e) {
            throw wrap("loadTable(" + ucCatalog + "." + schemaName + "." + tableName + ")", e);
        }
    }

    private DeltaLoadTableResponse loadTableViaCatalogApi(String ucCatalog, String schemaName, String tableName) {
        String fullName = ucCatalog + "." + schemaName + "." + tableName;
        TableInfo table = getTableInfoViaOssClient(fullName);
        if (table == null) {
            throw new DeltaLakeTableNotFoundException("Unity Catalog table %s not found", fullName);
        }
        if (table.getDataSourceFormat() != DataSourceFormat.DELTA) {
            throw new UnityCatalogUnsupportedTableFormatException(
                    "Unity Catalog table " + fullName + " is not a Delta table", null);
        }

        DeltaTableMetadata metadata = new DeltaTableMetadata()
                .location(toOneLakeAbfssUri(table.getStorageLocation()))
                .createdTime(table.getCreatedAt())
                .updatedTime(table.getUpdatedAt());
        UUID tableUuid = parseTableUuid(table.getTableId());
        if (tableUuid != null) {
            metadata.setTableUuid(tableUuid);
        }
        return new DeltaLoadTableResponse().metadata(metadata);
    }

    /**
     * Fabric returns HTTPS storage locations, while Hadoop ABFS requires an {@code abfss} URI whose
     * authority identifies the workspace. Converting that shape makes Delta Kernel select the ABFS
     * filesystem and use the configured OneLake OAuth credentials.
     */
    @VisibleForTesting
    static String toOneLakeAbfssUri(String location) {
        if (Strings.isNullOrEmpty(location)) {
            return location;
        }
        try {
            URI source = URI.create(location);
            String host = source.getHost();
            if (!"https".equalsIgnoreCase(source.getScheme()) || host == null
                    || !host.toLowerCase(Locale.ROOT).endsWith(".dfs.fabric.microsoft.com")) {
                return location;
            }

            String[] pathParts = source.getPath().split("/", 3);
            if (pathParts.length != 3 || pathParts[1].isEmpty() || pathParts[2].isEmpty()) {
                return location;
            }

            return new URI("abfss", pathParts[1], host, -1, "/" + pathParts[2], null, null).toASCIIString();
        } catch (IllegalArgumentException | URISyntaxException e) {
            return location;
        }
    }

    private static UUID parseTableUuid(String tableId) {
        if (Strings.isNullOrEmpty(tableId)) {
            return null;
        }
        try {
            return UUID.fromString(tableId);
        } catch (IllegalArgumentException e) {
            return null;
        }
    }

    @Override
    public DeltaCredentialsResponse getTableCredentials(String ucCatalog, String schemaName, String tableName,
                                                        String tableId, String operation) {
        // tableId is not part of the delta/v1 request; it only keys the credential cache upstream.
        if (deltaCredentialsApi == null) {
            throw new StarRocksConnectorException(
                    "Unity Catalog delta/v1 client is not configured; getTableCredentials is unavailable");
        }
        try {
            return deltaCredentialsApi.getTableCredentials(parseOperation(operation), ucCatalog, schemaName, tableName);
        } catch (ApiException e) {
            throw wrap("getTableCredentials(" + ucCatalog + "." + schemaName + "." + tableName
                    + ", " + operation + ")", e);
        } catch (RuntimeException e) {
            throw wrap("getTableCredentials(" + ucCatalog + "." + schemaName + "." + tableName
                    + ", " + operation + ")", e);
        }
    }

    private WorkspaceClient requireWorkspaceClient(String operation) {
        if (workspace == null) {
            throw new StarRocksConnectorException(
                    "Unity Catalog Databricks SDK client is not configured; %s is unavailable when %s=%s",
                    operation, UnityCatalogProperties.UNITY_CATALOG_FLAVOR,
                    flavor.name().toLowerCase(Locale.ROOT));
        }
        return workspace;
    }

    private boolean usesCatalogApi() {
        return flavor != UnityCatalogProperties.Flavor.DATABRICKS;
    }

    private SchemasApi requireCatalogSchemasApi(String operation) {
        if (catalogSchemasApi == null) {
            throw new StarRocksConnectorException(
                    "Unity Catalog generated catalog client is not configured; %s is unavailable", operation);
        }
        return catalogSchemasApi;
    }

    private TablesApi requireCatalogTablesApi(String operation) {
        if (catalogTablesApi == null) {
            throw new StarRocksConnectorException(
                    "Unity Catalog generated catalog client is not configured; %s is unavailable", operation);
        }
        return catalogTablesApi;
    }

    private static SchemaInfo toDatabricksSchemaInfo(io.unitycatalog.client.model.SchemaInfo schema) {
        return new SchemaInfo()
                .setCatalogName(schema.getCatalogName())
                .setComment(schema.getComment())
                .setCreatedAt(schema.getCreatedAt())
                .setCreatedBy(schema.getCreatedBy())
                .setFullName(schema.getFullName())
                .setName(schema.getName())
                .setOwner(schema.getOwner())
                .setProperties(schema.getProperties())
                .setSchemaId(schema.getSchemaId())
                .setStorageLocation(schema.getStorageLocation())
                .setStorageRoot(schema.getStorageRoot())
                .setUpdatedAt(schema.getUpdatedAt())
                .setUpdatedBy(schema.getUpdatedBy());
    }

    private static TableInfo toDatabricksTableInfo(io.unitycatalog.client.model.TableInfo table) {
        if (table == null) {
            return null;
        }
        TableInfo result = new TableInfo()
                .setCatalogName(table.getCatalogName())
                .setComment(table.getComment())
                .setCreatedAt(table.getCreatedAt())
                .setCreatedBy(table.getCreatedBy())
                .setDataSourceFormat(toDatabricksDataSourceFormat(table.getDataSourceFormat()))
                .setFullName(fullTableName(table))
                .setName(table.getName())
                .setOwner(table.getOwner())
                .setProperties(table.getProperties())
                .setSchemaName(table.getSchemaName())
                .setStorageLocation(table.getStorageLocation())
                .setTableId(table.getTableId())
                .setTableType(toDatabricksTableType(table.getTableType()))
                .setUpdatedAt(table.getUpdatedAt())
                .setUpdatedBy(table.getUpdatedBy())
                .setViewDefinition(table.getViewDefinition());
        if (table.getColumns() != null) {
            List<ColumnInfo> columns = new ArrayList<>();
            for (io.unitycatalog.client.model.ColumnInfo column : table.getColumns()) {
                if (column != null) {
                    columns.add(toDatabricksColumnInfo(column));
                }
            }
            result.setColumns(columns);
        }
        return result;
    }

    private static ColumnInfo toDatabricksColumnInfo(io.unitycatalog.client.model.ColumnInfo column) {
        return new ColumnInfo()
                .setComment(column.getComment())
                .setName(column.getName())
                .setNullable(column.getNullable())
                .setPartitionIndex(toLong(column.getPartitionIndex()))
                .setPosition(toLong(column.getPosition()))
                .setTypeIntervalType(column.getTypeIntervalType())
                .setTypeJson(column.getTypeJson())
                .setTypeName(toDatabricksColumnTypeName(column.getTypeName()))
                .setTypePrecision(toLong(column.getTypePrecision()))
                .setTypeScale(toLong(column.getTypeScale()))
                .setTypeText(column.getTypeText());
    }

    private static String fullTableName(io.unitycatalog.client.model.TableInfo table) {
        if (table.getCatalogName() == null || table.getSchemaName() == null || table.getName() == null) {
            return null;
        }
        return table.getCatalogName() + "." + table.getSchemaName() + "." + table.getName();
    }

    private static DataSourceFormat toDatabricksDataSourceFormat(
            io.unitycatalog.client.model.DataSourceFormat format) {
        return format == null ? null : enumValue(DataSourceFormat.class, format.getValue());
    }

    private static TableType toDatabricksTableType(io.unitycatalog.client.model.TableType type) {
        return type == null ? null : enumValue(TableType.class, type.getValue());
    }

    private static ColumnTypeName toDatabricksColumnTypeName(io.unitycatalog.client.model.ColumnTypeName type) {
        return type == null ? null : enumValue(ColumnTypeName.class, type.getValue());
    }

    private static <T extends Enum<T>> T enumValue(Class<T> enumType, String value) {
        if (value == null) {
            return null;
        }
        try {
            return Enum.valueOf(enumType, value);
        } catch (IllegalArgumentException e) {
            return null;
        }
    }

    private static Long toLong(Integer value) {
        return value == null ? null : value.longValue();
    }

    private static String emptyToNull(String value) {
        return value == null || value.isEmpty() ? null : value;
    }

    private static DeltaCredentialOperation parseOperation(String operation) {
        if (operation == null) {
            return DeltaCredentialOperation.READ;
        }
        try {
            String normalized = operation.trim().toUpperCase(Locale.ROOT);
            return DeltaCredentialOperation.valueOf(normalized);
        } catch (IllegalArgumentException e) {
            throw new StarRocksConnectorException(
                    "Unsupported Unity Catalog table operation: %s. Allowed: READ, READ_WRITE", operation);
        }
    }

    private static boolean isUnsupportedTableFormat(ApiException e) {
        return e.getCode() == 400
                && (UNSUPPORTED_TABLE_FORMAT.equals(errorType(e.getResponseBody()))
                || UNSUPPORTED_TABLE_FORMAT.equals(errorType(e.getMessage())));
    }

    private static String errorType(String errorPayload) {
        if (errorPayload == null || errorPayload.isEmpty()) {
            return null;
        }
        try {
            int jsonStart = errorPayload.indexOf('{');
            if (jsonStart < 0) {
                return null;
            }
            JsonNode type = ERROR_BODY_MAPPER.readTree(errorPayload.substring(jsonStart)).path("error").path("type");
            return type.isTextual() ? type.asText() : null;
        } catch (Exception e) {
            return null;
        }
    }

    private static StarRocksConnectorException wrap(String context, DatabricksException e) {
        LOG.warn("Unity Catalog request {} failed: {}", context, e.getMessage());
        return new StarRocksConnectorException("Unity Catalog %s failed: %s", context, e.getMessage());
    }

    private static StarRocksConnectorException wrap(String context, ApiException e) {
        LOG.warn("Unity Catalog request {} failed: code={} body={}", context, e.getCode(), e.getResponseBody());
        return new StarRocksConnectorException("Unity Catalog %s failed (HTTP %s): %s",
                context, e.getCode(), e.getMessage());
    }

    private static StarRocksConnectorException wrap(String context, RuntimeException e) {
        LOG.warn("Unity Catalog request {} failed: {}", context, e.getMessage(), e);
        return new StarRocksConnectorException(
                String.format("Unity Catalog %s failed: %s", context, e.getMessage()), e);
    }

    private static String encodeFullName(String fullName) {
        return FULL_NAME_ESCAPER.escape(fullName);
    }

    private static String operationContext(String operation, String target) {
        return operation + "(" + target + ")";
    }
}
