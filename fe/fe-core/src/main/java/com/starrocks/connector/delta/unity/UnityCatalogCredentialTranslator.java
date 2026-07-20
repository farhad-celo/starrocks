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
import com.databricks.sdk.service.catalog.GcpOauthToken;
import com.databricks.sdk.service.catalog.GenerateTemporaryTableCredentialResponse;
import com.google.common.base.Strings;
import com.starrocks.credential.CloudConfiguration;
import com.starrocks.credential.CloudConfigurationFactory;
import com.starrocks.credential.CloudType;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.net.URI;
import java.net.URISyntaxException;
import java.util.HashMap;
import java.util.Map;

import static com.starrocks.connector.share.credential.CloudConfigurationConstants.AZURE_ADLS2_ENDPOINT;
import static com.starrocks.connector.share.credential.CloudConfigurationConstants.AZURE_ADLS2_SAS_TOKEN;
import static com.starrocks.connector.share.credential.CloudConfigurationConstants.AZURE_ADLS2_STORAGE_ACCOUNT;

/**
 * Translates the {@link GenerateTemporaryTableCredentialResponse} returned by the Databricks
 * Java SDK into a StarRocks {@link CloudConfiguration} by routing each cloud through the
 * existing {@link CloudConfigurationFactory} entry points
 * ({@link CloudConfigurationFactory#buildCloudConfigurationForVendedCredentials} for AWS,
 * {@link CloudConfigurationFactory#buildCloudConfigurationForStorage} for Azure).
 *
 * <p>The SDK response carries a top-level envelope ({@code awsTempCredentials} /
 * {@code azureUserDelegationSas} / {@code gcpOauthToken}); we flatten the fields we care about
 * into the property keys that the factory already understands, and the factory picks the right
 * cloud provider.</p>
 *
 * <p>v1 supports AWS S3 and Azure ADLS Gen2 only.</p>
 */
public final class UnityCatalogCredentialTranslator {
    private static final Logger LOG = LogManager.getLogger(UnityCatalogCredentialTranslator.class);

    // Iceberg's S3FileIOProperties / AwsClientProperties key names, reused because
    // CloudConfigurationFactory.buildCloudConfigurationForVendedCredentials already
    // understands them.
    private static final String S3_ACCESS_KEY_ID = "s3.access-key-id";
    private static final String S3_SECRET_ACCESS_KEY = "s3.secret-access-key";
    private static final String S3_SESSION_TOKEN = "s3.session-token";
    private static final String CLIENT_REGION = "client.region";

    private UnityCatalogCredentialTranslator() {
    }

    /**
     * Build a {@link CloudConfiguration} from a UC temporary-credentials response.
     *
     * @param creds         the SDK response
     * @param tableLocation the table's {@code storageLocation} (e.g. {@code s3://bucket/path} or
     *                      {@code abfss://container@account.dfs.core.windows.net/path}); required
     *                      for Azure so we can derive the storage account endpoint.
     * @return a non-null {@link CloudConfiguration}; may be a {@code DEFAULT} one if no supported
     * credential shape was present (in which case the caller should fall back to the
     * catalog-level config).
     */
    public static CloudConfiguration toCloudConfiguration(GenerateTemporaryTableCredentialResponse creds,
                                                          String tableLocation) {
        return toCloudConfiguration(creds, tableLocation, null);
    }

    /**
     * Overload that also accepts an optional AWS region. UC's temporary-credentials API does not
     * return a region, but the BE's AWS C++ SDK defaults to {@code us-east-1} and does not
     * follow cross-region 301 redirects, so callers that know the bucket region (e.g. from a
     * catalog-level property) should pass it here; it is serialized into the resulting
     * {@link CloudConfiguration} and ultimately onto the scan range delivered to the BE.
     */
    public static CloudConfiguration toCloudConfiguration(GenerateTemporaryTableCredentialResponse creds,
                                                          String tableLocation,
                                                          String awsRegion) {
        if (creds == null) {
            return CloudConfigurationFactory.buildCloudConfigurationForStorage(new HashMap<>());
        }

        AwsCredentials aws = creds.getAwsTempCredentials();
        if (aws != null
                && !Strings.isNullOrEmpty(aws.getAccessKeyId())
                && !Strings.isNullOrEmpty(aws.getSecretAccessKey())
                && !Strings.isNullOrEmpty(aws.getSessionToken())) {
            Map<String, String> props = new HashMap<>();
            props.put(S3_ACCESS_KEY_ID, aws.getAccessKeyId());
            props.put(S3_SECRET_ACCESS_KEY, aws.getSecretAccessKey());
            props.put(S3_SESSION_TOKEN, aws.getSessionToken());
            if (!Strings.isNullOrEmpty(awsRegion)) {
                props.put(CLIENT_REGION, awsRegion);
            }
            CloudConfiguration cc = CloudConfigurationFactory.buildCloudConfigurationForVendedCredentials(props);
            if (cc.getCloudType() != CloudType.DEFAULT) {
                return cc;
            }
            LOG.warn("Unity Catalog returned AWS credentials but CloudConfigurationFactory could not " +
                    "build a cloud configuration from them (tableLocation={})", tableLocation);
        }

        AzureUserDelegationSas sas = creds.getAzureUserDelegationSas();
        if (sas != null && !Strings.isNullOrEmpty(sas.getSasToken())) {
            String endpoint = extractAdlsEndpoint(tableLocation);
            if (endpoint != null) {
                Map<String, String> props = new HashMap<>();
                props.put(AZURE_ADLS2_SAS_TOKEN, sas.getSasToken());
                int firstDot = endpoint.indexOf('.');
                if (firstDot > 0) {
                    props.put(AZURE_ADLS2_STORAGE_ACCOUNT, endpoint.substring(0, firstDot));
                }
                props.put(AZURE_ADLS2_ENDPOINT, endpoint);
                CloudConfiguration cc = CloudConfigurationFactory.buildCloudConfigurationForStorage(props);
                if (cc.getCloudType() != CloudType.DEFAULT) {
                    return cc;
                }
            } else {
                LOG.warn("Unity Catalog returned an Azure SAS token but the table location '{}' does not " +
                        "expose an ADLS Gen2 endpoint; falling back to catalog-level credentials", tableLocation);
            }
        }

        GcpOauthToken gcp = creds.getGcpOauthToken();
        if (gcp != null && !Strings.isNullOrEmpty(gcp.getOauthToken())) {
            LOG.debug("Unity Catalog returned GCP OAuth token but GCS is not supported in v1; falling back " +
                    "to catalog-level credentials");
        }

        return CloudConfigurationFactory.buildCloudConfigurationForStorage(new HashMap<>());
    }

    /**
     * Given a table URI like {@code abfss://container@account.dfs.core.windows.net/path} return
     * the endpoint piece {@code account.dfs.core.windows.net} suitable for appending to
     * {@link com.starrocks.credential.azure.AzureCloudConfigurationProvider#ADLS_SAS_TOKEN}.
     * Returns {@code null} if the scheme is not ADLS Gen2.
     */
    private static final String ADLS_GEN2_SUFFIX = ".dfs.core.windows.net";

    static String extractAdlsEndpoint(String tableLocation) {
        if (Strings.isNullOrEmpty(tableLocation)) {
            return null;
        }
        String trimmed = tableLocation.trim();
        String lower = trimmed.toLowerCase();
        if (!(lower.startsWith("abfs://") || lower.startsWith("abfss://"))) {
            return null;
        }
        try {
            URI uri = new URI(trimmed);
            String authority = uri.getRawAuthority();
            if (Strings.isNullOrEmpty(authority)) {
                return null;
            }
            int at = authority.indexOf('@');
            String hostPart = at >= 0 ? authority.substring(at + 1) : authority;
            if (!hostPart.endsWith(ADLS_GEN2_SUFFIX)) {
                return null;
            }
            return hostPart;
        } catch (URISyntaxException e) {
            LOG.warn("Unparseable ADLS table location '{}': {}", tableLocation, e.getMessage());
            return null;
        }
    }
}
