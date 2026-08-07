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
import com.databricks.sdk.service.catalog.GetMetastoreSummaryResponse;
import com.databricks.sdk.service.catalog.ListSchemasRequest;
import com.databricks.sdk.service.catalog.ListTablesRequest;
import com.databricks.sdk.service.catalog.SchemaInfo;
import com.databricks.sdk.service.catalog.TableInfo;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.ImmutableList;
import com.google.common.net.PercentEscaper;
import com.starrocks.connector.exception.StarRocksConnectorException;
import io.unitycatalog.client.ApiClient;
import io.unitycatalog.client.ApiClientBuilder;
import io.unitycatalog.client.ApiException;
import io.unitycatalog.client.auth.TokenProvider;
import io.unitycatalog.client.delta.api.DeltaTablesApi;
import io.unitycatalog.client.delta.api.DeltaTemporaryCredentialsApi;
import io.unitycatalog.client.delta.model.DeltaCredentialOperation;
import io.unitycatalog.client.delta.model.DeltaCredentialsResponse;
import io.unitycatalog.client.delta.model.DeltaLoadTableResponse;
import io.unitycatalog.client.retry.JitterDelayRetryPolicy;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.time.Duration;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;

/**
 * Adapter over two clients: the Databricks SDK {@link WorkspaceClient} for schema/table discovery
 * and metastore summary, and the Unity Catalog {@code delta/v1} client ({@link DeltaTablesApi} +
 * {@link DeltaTemporaryCredentialsApi}) for the read path. Both support PAT and OAuth-M2M auth.
 */
public class UnityCatalogClient implements UnityCatalogApi {
    private static final Logger LOG = LogManager.getLogger(UnityCatalogClient.class);
    private static final PercentEscaper FULL_NAME_ESCAPER = new PercentEscaper("-_.*", false);
    private static final String DELTA_APP_VERSION = "4.3.0";
    private static final String SPOOF_SPARK_APP_VERSION = "4.0.0";
    private static final String SPOOF_JAVA_VERSION = "17.0.19";
    private static final String SCALA_APP_VERSION = "2.13.16";
    // Currently no way to retrieve this easily.
    private static final String STARROCKS_VERSION = "3.5";

    private final WorkspaceClient workspace;
    // delta/v1 read path, from the OSS unitycatalog-client. Null only in the WorkspaceClient-only
    // test constructor; loadTable / getTableCredentials then fail fast.
    private final DeltaTablesApi deltaTablesApi;
    private final DeltaTemporaryCredentialsApi deltaCredentialsApi;

    public UnityCatalogClient(UnityCatalogProperties properties) {
        this(buildWorkspaceClient(properties), buildDeltaApiClient(properties));
    }

    private UnityCatalogClient(WorkspaceClient workspace, ApiClient deltaApiClient) {
        this(workspace, new DeltaTablesApi(deltaApiClient), new DeltaTemporaryCredentialsApi(deltaApiClient));
    }

    /** Visible for testing. Lets unit tests inject a mocked or fake {@link WorkspaceClient}. */
    @VisibleForTesting
    public UnityCatalogClient(WorkspaceClient workspace) {
        this(workspace, (DeltaTablesApi) null, (DeltaTemporaryCredentialsApi) null);
    }

    /** Visible for testing. Lets unit tests inject fake {@code delta/v1} clients. */
    @VisibleForTesting
    UnityCatalogClient(WorkspaceClient workspace, DeltaTablesApi deltaTablesApi,
                       DeltaTemporaryCredentialsApi deltaCredentialsApi) {
        this.workspace = Objects.requireNonNull(workspace, "workspace");
        this.deltaTablesApi = deltaTablesApi;
        this.deltaCredentialsApi = deltaCredentialsApi;
    }

    private static WorkspaceClient buildWorkspaceClient(UnityCatalogProperties properties) {
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

    /** Build the OSS {@code delta/v1} {@link ApiClient} shared by the two delta APIs. */
    @VisibleForTesting
    static ApiClient buildDeltaApiClient(UnityCatalogProperties properties) {
        Map<String, String> authConfig = new HashMap<>();
        if (properties.getAuthType() == UnityCatalogProperties.AuthType.OAUTH_M2M) {
            authConfig.put("type", "oauth");
            authConfig.put("oauth.uri", properties.getHost() + "/oidc/v1/token");
            authConfig.put("oauth.clientId", properties.getClientId());
            authConfig.put("oauth.clientSecret", properties.getClientSecret());
        } else {
            authConfig.put("type", "static");
            authConfig.put("token", properties.getToken());
        }

        // Databricks rejects delta/v1 with HTTP 400 unless the caller identifies itself in the
        // User-Agent; the builder emits "UnityCatalog-Java-Client/<ver> ..." app tokens.
        ApiClientBuilder builder = ApiClientBuilder.create()
                .uri(properties.getHost())
                .tokenProvider(TokenProvider.create(authConfig))
                // maxAttempts counts the initial try, and the builder rejects 0, so translate the
                // retry count into total attempts.
                .retryPolicy(JitterDelayRetryPolicy.builder()
                        .maxAttempts(properties.getMaxRetries() + 1).build());
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
        try {
            return ImmutableList.copyOf(
                    workspace.schemas().list(new ListSchemasRequest().setCatalogName(ucCatalog)));
        } catch (DatabricksException e) {
            throw wrap("listSchemas(" + ucCatalog + ")", e);
        }
    }

    @Override
    public List<TableInfo> listTables(String ucCatalog, String schemaName) {
        try {
            return ImmutableList.copyOf(workspace.tables().list(
                    new ListTablesRequest().setCatalogName(ucCatalog).setSchemaName(schemaName)));
        } catch (DatabricksException e) {
            throw wrap("listTables(" + ucCatalog + "." + schemaName + ")", e);
        }
    }

    @Override
    public boolean tableExists(String fullName) {
        try {
            workspace.tables().get(encodeFullName(fullName));
            return true;
        } catch (NotFound e) {
            return false;
        } catch (DatabricksException e) {
            throw wrap("tableExists(" + fullName + ")", e);
        }
    }

    @Override
    public GetMetastoreSummaryResponse getMetastoreSummary() {
        try {
            return workspace.metastores().summary();
        } catch (DatabricksException e) {
            throw wrap("metastores.summary", e);
        }
    }

    @Override
    public DeltaLoadTableResponse loadTable(String ucCatalog, String schemaName, String tableName) {
        if (deltaTablesApi == null) {
            throw new StarRocksConnectorException(
                    "Unity Catalog delta/v1 client is not configured; loadTable is unavailable");
        }
        try {
            return deltaTablesApi.loadTable(ucCatalog, schemaName, tableName);
        } catch (ApiException e) {
            String context = "loadTable(" + ucCatalog + "." + schemaName + "." + tableName + ")";
            // The delta/v1 endpoint only serves Delta tables; a 4xx on a table accessed by name
            // most often means it is missing or not a Delta table (Iceberg / Parquet / view).
            if (e.getCode() >= 400 && e.getCode() < 500) {
                context += "; verify the table exists and its data source format is Delta";
            }
            throw wrap(context, e);
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
        }
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

    private static StarRocksConnectorException wrap(String context, DatabricksException e) {
        LOG.warn("Unity Catalog request {} failed: {}", context, e.getMessage());
        return new StarRocksConnectorException("Unity Catalog %s failed: %s", context, e.getMessage());
    }

    private static StarRocksConnectorException wrap(String context, ApiException e) {
        LOG.warn("Unity Catalog request {} failed: code={} body={}", context, e.getCode(), e.getResponseBody());
        return new StarRocksConnectorException("Unity Catalog %s failed (HTTP %s): %s",
                context, e.getCode(), e.getMessage());
    }

    private static String encodeFullName(String fullName) {
        return FULL_NAME_ESCAPER.escape(fullName);
    }
}
