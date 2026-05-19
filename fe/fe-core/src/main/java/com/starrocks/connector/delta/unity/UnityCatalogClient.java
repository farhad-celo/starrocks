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
import com.databricks.sdk.service.catalog.GenerateTemporaryTableCredentialRequest;
import com.databricks.sdk.service.catalog.GenerateTemporaryTableCredentialResponse;
import com.databricks.sdk.service.catalog.GetMetastoreSummaryResponse;
import com.databricks.sdk.service.catalog.ListSchemasRequest;
import com.databricks.sdk.service.catalog.ListTablesRequest;
import com.databricks.sdk.service.catalog.SchemaInfo;
import com.databricks.sdk.service.catalog.TableInfo;
import com.databricks.sdk.service.catalog.TableOperation;
import com.google.common.collect.ImmutableList;
import com.google.common.net.PercentEscaper;
import com.starrocks.connector.exception.StarRocksConnectorException;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.util.List;
import java.util.Locale;
import java.util.Objects;

/**
 * Thin adapter over the Databricks Java SDK's {@link WorkspaceClient}. The SDK handles HTTP,
 * JSON serialization, retries, and authentication; this class only translates property-bag
 * config into a {@link DatabricksConfig} and forwards each {@link UnityCatalogApi} method to
 * the matching SDK call.
 *
 * <p>Two auth modes are supported:
 * <ul>
 *   <li>{@link UnityCatalogProperties.AuthType#PAT} (default) -- bearer Personal Access Token
 *       via {@code unity.catalog.token};</li>
 *   <li>{@link UnityCatalogProperties.AuthType#OAUTH_M2M} -- OAuth 2.0 client-credentials flow
 *       via {@code unity.catalog.client-id} + {@code unity.catalog.client-secret}.</li>
 * </ul>
 */
public class UnityCatalogClient implements UnityCatalogApi {
    private static final Logger LOG = LogManager.getLogger(UnityCatalogClient.class);
    private static final String READ_OPERATION = "READ";
    private static final PercentEscaper FULL_NAME_ESCAPER = new PercentEscaper("-_.*", false);

    private final WorkspaceClient workspace;

    public UnityCatalogClient(UnityCatalogProperties properties) {
        this(buildWorkspaceClient(properties));
    }

    /** Visible for testing. Lets unit tests inject a mocked or fake {@link WorkspaceClient}. */
    public UnityCatalogClient(WorkspaceClient workspace) {
        this.workspace = Objects.requireNonNull(workspace, "workspace");
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
    public TableInfo getTable(String fullName) {
        try {
            return workspace.tables().get(encodeFullName(fullName));
        } catch (DatabricksException e) {
            throw wrap("getTable(" + fullName + ")", e);
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
    public GenerateTemporaryTableCredentialResponse getTemporaryTableCredentials(String tableId, String operation) {
        try {
            return workspace.temporaryTableCredentials().generateTemporaryTableCredentials(
                    new GenerateTemporaryTableCredentialRequest()
                            .setTableId(tableId)
                            .setOperation(parseOperation(operation)));
        } catch (DatabricksException e) {
            throw wrap("generateTemporaryTableCredentials(" + tableId + ", " + operation + ")", e);
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

    private static TableOperation parseOperation(String operation) {
        String normalized = operation == null ? READ_OPERATION : operation.trim().toUpperCase(Locale.ROOT);
        try {
            return TableOperation.valueOf(normalized);
        } catch (IllegalArgumentException e) {
            throw new StarRocksConnectorException(
                    "Unsupported Unity Catalog table operation: %s. Allowed: READ, READ_WRITE", operation);
        }
    }

    private static StarRocksConnectorException wrap(String context, DatabricksException e) {
        LOG.warn("Unity Catalog request {} failed: {}", context, e.getMessage());
        return new StarRocksConnectorException("Unity Catalog %s failed: %s", context, e.getMessage());
    }

    private static String encodeFullName(String fullName) {
        return FULL_NAME_ESCAPER.escape(fullName);
    }
}
