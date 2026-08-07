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

import com.google.common.base.Strings;
import com.starrocks.credential.CloudConfiguration;
import com.starrocks.credential.CloudConfigurationFactory;
import com.starrocks.credential.CloudType;
import io.unitycatalog.client.delta.model.DeltaCredentialsResponse;
import io.unitycatalog.client.delta.model.DeltaStorageCredential;
import io.unitycatalog.client.delta.model.DeltaStorageCredentialConfig;
import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;

import java.net.URI;
import java.net.URISyntaxException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static com.starrocks.connector.share.credential.CloudConfigurationConstants.AZURE_ADLS2_ENDPOINT;
import static com.starrocks.connector.share.credential.CloudConfigurationConstants.AZURE_ADLS2_SAS_TOKEN;
import static com.starrocks.connector.share.credential.CloudConfigurationConstants.AZURE_ADLS2_STORAGE_ACCOUNT;

/**
 * Translates a {@code delta/v1} {@link DeltaCredentialsResponse} into a StarRocks
 * {@link CloudConfiguration} via {@link CloudConfigurationFactory}. Picks the
 * {@link DeltaStorageCredential} whose {@code prefix} is the longest prefix of the table location.
 * v1 supports AWS S3 and Azure ADLS Gen2 only.
 */
public final class UnityCatalogCredentialTranslator {
    private static final Logger LOG = LogManager.getLogger(UnityCatalogCredentialTranslator.class);

    // Iceberg S3/AWS key names, understood by buildCloudConfigurationForVendedCredentials.
    private static final String S3_ACCESS_KEY_ID = "s3.access-key-id";
    private static final String S3_SECRET_ACCESS_KEY = "s3.secret-access-key";
    private static final String S3_SESSION_TOKEN = "s3.session-token";
    private static final String CLIENT_REGION = "client.region";

    private static final String ADLS_GEN2_SUFFIX = ".dfs.core.windows.net";

    private UnityCatalogCredentialTranslator() {
    }

    public static CloudConfiguration toCloudConfiguration(DeltaCredentialsResponse creds, String tableLocation) {
        return toCloudConfiguration(creds, tableLocation, null);
    }

    /**
     * Overload accepting an optional AWS region. UC does not return one, but the BE's AWS SDK
     * defaults to {@code us-east-1} and does not follow cross-region redirects, so a known region
     * must be threaded through onto the scan range.
     */
    public static CloudConfiguration toCloudConfiguration(DeltaCredentialsResponse creds, String tableLocation,
                                                          String awsRegion) {
        DeltaStorageCredentialConfig config = selectConfig(creds, tableLocation);
        if (config == null) {
            return emptyConfiguration();
        }
        CloudConfiguration aws = buildAwsConfiguration(config, awsRegion, tableLocation);
        if (aws != null) {
            return aws;
        }
        CloudConfiguration azure = buildAzureConfiguration(config, tableLocation);
        if (azure != null) {
            return azure;
        }
        if (!Strings.isNullOrEmpty(config.getGcsOauthToken())) {
            LOG.debug("Unity Catalog returned GCP OAuth token but GCS is not supported in v1; falling back " +
                    "to catalog-level credentials");
        }
        return emptyConfiguration();
    }

    private static CloudConfiguration emptyConfiguration() {
        return CloudConfigurationFactory.buildCloudConfigurationForStorage(new HashMap<>());
    }

    private static CloudConfiguration buildAwsConfiguration(DeltaStorageCredentialConfig config, String awsRegion,
                                                            String tableLocation) {
        if (Strings.isNullOrEmpty(config.getS3AccessKeyId())
                || Strings.isNullOrEmpty(config.getS3SecretAccessKey())
                || Strings.isNullOrEmpty(config.getS3SessionToken())) {
            return null;
        }
        Map<String, String> props = new HashMap<>();
        props.put(S3_ACCESS_KEY_ID, config.getS3AccessKeyId());
        props.put(S3_SECRET_ACCESS_KEY, config.getS3SecretAccessKey());
        props.put(S3_SESSION_TOKEN, config.getS3SessionToken());
        if (!Strings.isNullOrEmpty(awsRegion)) {
            props.put(CLIENT_REGION, awsRegion);
        }
        CloudConfiguration cc = CloudConfigurationFactory.buildCloudConfigurationForVendedCredentials(props);
        if (cc.getCloudType() != CloudType.DEFAULT) {
            return cc;
        }
        LOG.warn("Unity Catalog returned AWS credentials but CloudConfigurationFactory could not " +
                "build a cloud configuration from them (tableLocation={})", tableLocation);
        return null;
    }

    private static CloudConfiguration buildAzureConfiguration(DeltaStorageCredentialConfig config,
                                                              String tableLocation) {
        if (Strings.isNullOrEmpty(config.getAzureSasToken())) {
            return null;
        }
        String endpoint = extractAdlsEndpoint(tableLocation);
        if (endpoint == null) {
            LOG.warn("Unity Catalog returned an Azure SAS token but the table location '{}' does not " +
                    "expose an ADLS Gen2 endpoint; falling back to catalog-level credentials", tableLocation);
            return null;
        }
        Map<String, String> props = new HashMap<>();
        props.put(AZURE_ADLS2_SAS_TOKEN, config.getAzureSasToken());
        int firstDot = endpoint.indexOf('.');
        if (firstDot > 0) {
            props.put(AZURE_ADLS2_STORAGE_ACCOUNT, endpoint.substring(0, firstDot));
        }
        props.put(AZURE_ADLS2_ENDPOINT, endpoint);
        CloudConfiguration cc = CloudConfigurationFactory.buildCloudConfigurationForStorage(props);
        return cc.getCloudType() != CloudType.DEFAULT ? cc : null;
    }

    /** {@code true} when the credential for {@code tableLocation} carries an AWS access key. */
    public static boolean hasAwsCredential(DeltaCredentialsResponse creds, String tableLocation) {
        DeltaStorageCredentialConfig config = selectConfig(creds, tableLocation);
        return config != null && !Strings.isNullOrEmpty(config.getS3AccessKeyId());
    }

    /** Pick the config whose {@code prefix} is the longest path-boundary prefix of {@code tableLocation}. */
    private static DeltaStorageCredentialConfig selectConfig(DeltaCredentialsResponse creds, String tableLocation) {
        List<DeltaStorageCredential> credentials = creds == null ? null : creds.getStorageCredentials();
        if (credentials == null || tableLocation == null) {
            return null;
        }
        DeltaStorageCredential best = null;
        int bestLen = -1;
        for (DeltaStorageCredential candidate : credentials) {
            int matchLen = prefixMatchLength(tableLocation, candidate.getPrefix());
            if (matchLen > bestLen) {
                best = candidate;
                bestLen = matchLen;
            }
        }
        return best == null ? null : best.getConfig();
    }

    /**
     * Length of {@code prefix} when it path-boundary matches {@code location}, else {@code -1}:
     * {@code s3://bucket/foo} covers {@code s3://bucket/foo/tbl} but not {@code s3://bucket/foobar}.
     * A null or empty prefix is an account-wide credential that matches all (length 0).
     */
    private static int prefixMatchLength(String location, String prefix) {
        String p = prefix == null ? "" : prefix;
        if (p.isEmpty()) {
            return 0;
        }
        if (!location.startsWith(p)) {
            return -1;
        }
        boolean boundary = location.length() == p.length()
                || p.charAt(p.length() - 1) == '/'
                || location.charAt(p.length()) == '/';
        return boundary ? p.length() : -1;
    }

    /** Extract the {@code account.dfs.core.windows.net} endpoint from an ADLS Gen2 URI, else null. */
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
