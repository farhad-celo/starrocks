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

import com.google.common.base.Preconditions;
import com.google.common.base.Strings;
import com.google.common.hash.Hashing;
import com.starrocks.sql.analyzer.SemanticException;

import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.Map;

public class UnityCatalogProperties {
    public static final String UNITY_CATALOG_HOST = "unity.catalog.host";
    public static final String UNITY_CATALOG_TOKEN = "unity.catalog.token";
    public static final String UNITY_CATALOG_NAME = "unity.catalog.name";
    public static final String UNITY_VENDED_CREDENTIALS_ENABLED = "unity.catalog.vended-credentials-enabled";
    public static final String UNITY_REQUEST_TIMEOUT_MS = "unity.catalog.request-timeout-ms";
    public static final String UNITY_MAX_RETRIES = "unity.catalog.max-retries";
    public static final String UNITY_CATALOG_SPOOF_USER_AGENT = "unity.catalog.spoof-user-agent";
    // Optional explicit AWS region for vended credentials. If unset we infer it from
    // Unity Catalog's metastore_summary endpoint on first use; setting it here is an
    // escape hatch for environments where the inference call is undesirable (extra
    // latency on cold start, restricted token scope, OSS UC deployments that do not
    // expose the endpoint, etc.).
    public static final String UNITY_CATALOG_AWS_REGION = "unity.catalog.aws.region";
    // Client-side metadata cache in front of the UC REST client. Credentials use the same TTL
    // and additionally honor the server-side expiration_time minus a safety margin.
    public static final String UNITY_CACHE_ENABLED = "unity.catalog.cache.enabled";
    // Unity-specific Delta caches: the catalog-level latest-snapshot cache plus the shared
    // checkpoint/JSON metadata file cache. This is intentionally separate from TableInfo caching.
    public static final String UNITY_DELTA_CACHE_ENABLED = "unity.catalog.delta-cache.enabled";
    public static final String UNITY_CACHE_TTL_SEC = "unity.catalog.cache.ttl-sec";
    public static final String UNITY_CACHE_CREDENTIALS_SAFETY_MARGIN_SEC =
            "unity.catalog.cache.credentials.safety-margin-sec";

    // Auth selector: "pat" (default) uses the Personal Access Token in unity.catalog.token;
    // "oauth-m2m" uses an OAuth machine-to-machine client_id/client_secret pair.
    public static final String UNITY_AUTH_TYPE = "unity.catalog.auth.type";
    public static final String UNITY_CLIENT_ID = "unity.catalog.client-id";
    public static final String UNITY_CLIENT_SECRET = "unity.catalog.client-secret";

    public static final String AUTH_TYPE_PAT = "pat";
    public static final String AUTH_TYPE_OAUTH_M2M = "oauth-m2m";

    private static final long DEFAULT_CACHE_TTL_SEC = 60L;
    private static final long DEFAULT_CREDENTIALS_SAFETY_MARGIN_SEC = 1200L;

    /** Mode used to authenticate against the Unity Catalog REST API. */
    public enum AuthType {
        PAT,
        OAUTH_M2M
    }

    private final String host;
    private final AuthType authType;
    private final String token;
    private final String clientId;
    private final String clientSecret;
    private final String ucCatalogName;
    private final boolean vendedCredentialsEnabled;
    private final boolean spoofUserAgent;
    private final long requestTimeoutMs;
    private final int maxRetries;
    private final boolean cacheEnabled;
    private final boolean deltaCacheEnabled;
    private final long cacheTtlSec;
    private final long credentialsSafetyMarginSec;
    // null when the operator did not specify an override -- callers fall back to the
    // inferred region from Unity Catalog's metastore_summary endpoint.
    private final String awsRegionOverride;

    // Lazily computed, memoized fingerprint of the Unity principal; see getPrincipalScope().
    private volatile String principalScope;

    public UnityCatalogProperties(Map<String, String> properties) {
        String hostValue = properties.get(UNITY_CATALOG_HOST);
        Preconditions.checkArgument(!Strings.isNullOrEmpty(hostValue),
                "%s must be set when creating a Unity Catalog-backed delta lake catalog", UNITY_CATALOG_HOST);
        this.host = stripTrailingSlash(hostValue);

        this.authType = parseAuthType(properties.get(UNITY_AUTH_TYPE));
        this.token = emptyToNull(properties.get(UNITY_CATALOG_TOKEN));
        this.clientId = emptyToNull(properties.get(UNITY_CLIENT_ID));
        this.clientSecret = emptyToNull(properties.get(UNITY_CLIENT_SECRET));
        validateAuth();

        this.ucCatalogName = properties.get(UNITY_CATALOG_NAME);
        Preconditions.checkArgument(!Strings.isNullOrEmpty(this.ucCatalogName),
                "%s must be set when creating a Unity Catalog-backed delta lake catalog", UNITY_CATALOG_NAME);

        String vendedCredsRaw = properties.getOrDefault(UNITY_VENDED_CREDENTIALS_ENABLED, "true");
        this.vendedCredentialsEnabled = Boolean.parseBoolean(vendedCredsRaw);
        String spoofUserAgentRaw = properties.getOrDefault(UNITY_CATALOG_SPOOF_USER_AGENT, "false");
        this.spoofUserAgent = Boolean.parseBoolean(spoofUserAgentRaw);

        this.requestTimeoutMs = parseLong(properties, UNITY_REQUEST_TIMEOUT_MS, 30_000L);
        Preconditions.checkArgument(this.requestTimeoutMs >= 0,
                "%s must be >= 0", UNITY_REQUEST_TIMEOUT_MS);
        long maxRetriesLong = parseLong(properties, UNITY_MAX_RETRIES, 3L);
        Preconditions.checkArgument(maxRetriesLong >= 0 && maxRetriesLong < Integer.MAX_VALUE,
                "%s must be in [0, %s]", UNITY_MAX_RETRIES, Integer.MAX_VALUE - 1);
        this.maxRetries = (int) maxRetriesLong;

        String cacheEnabledRaw = properties.getOrDefault(UNITY_CACHE_ENABLED, "true");
        this.cacheEnabled = Boolean.parseBoolean(cacheEnabledRaw);
        String deltaCacheEnabledRaw = properties.getOrDefault(UNITY_DELTA_CACHE_ENABLED, "true");
        this.deltaCacheEnabled = Boolean.parseBoolean(deltaCacheEnabledRaw);
        this.cacheTtlSec = parseLong(properties, UNITY_CACHE_TTL_SEC, DEFAULT_CACHE_TTL_SEC);
        Preconditions.checkArgument(this.cacheTtlSec >= 0,
                "%s must be >= 0", UNITY_CACHE_TTL_SEC);
        this.credentialsSafetyMarginSec = parseLong(properties,
                UNITY_CACHE_CREDENTIALS_SAFETY_MARGIN_SEC, DEFAULT_CREDENTIALS_SAFETY_MARGIN_SEC);
        Preconditions.checkArgument(this.credentialsSafetyMarginSec >= 0,
                "%s must be >= 0", UNITY_CACHE_CREDENTIALS_SAFETY_MARGIN_SEC);

        String regionRaw = properties.get(UNITY_CATALOG_AWS_REGION);
        this.awsRegionOverride = Strings.isNullOrEmpty(regionRaw) ? null : regionRaw.trim();
    }

    public String getHost() {
        return host;
    }

    public AuthType getAuthType() {
        return authType;
    }

    public String getToken() {
        return token;
    }

    public String getClientId() {
        return clientId;
    }

    public String getClientSecret() {
        return clientSecret;
    }

    public String getUcCatalogName() {
        return ucCatalogName;
    }

    public boolean isVendedCredentialsEnabled() {
        return vendedCredentialsEnabled;
    }

    public boolean isSpoofUserAgent() {
        return spoofUserAgent;
    }

    public long getRequestTimeoutMs() {
        return requestTimeoutMs;
    }

    public int getMaxRetries() {
        return maxRetries;
    }

    public boolean isCacheEnabled() {
        return cacheEnabled;
    }

    public boolean isDeltaCacheEnabled() {
        return deltaCacheEnabled;
    }

    /** TTL applied to UC metadata caches and, when positive, the credential cache. */
    public long getCacheTtlSec() {
        return cacheTtlSec;
    }

    /** Seconds subtracted from each credential's {@code expirationTime} before re-vending. */
    public long getCredentialsSafetyMarginSec() {
        return credentialsSafetyMarginSec;
    }

    /**
     * Operator-supplied AWS region override. {@code null} means "not set"; in that case
     * {@link UnityMetastore} infers the region from Unity Catalog's {@code metastore_summary}
     * endpoint on first use. When this returns a non-null value we trust it and skip the
     * inference REST call entirely.
     */
    public String getAwsRegionOverride() {
        return awsRegionOverride;
    }

    /**
     * Fingerprint of the Unity principal, used to partition the shared Delta metadata
     * cache so entries cannot cross a credential boundary. Keyed on {@code (host, authType, identity)}
     * -- the PAT token or the OAuth {@code clientId} -- hashed so no secret lands in a cache key. The
     * UC catalog name is excluded because Unity's vend authority is principal-scoped, not per-catalog.
     */
    public String getPrincipalScope() {
        String scope = principalScope;
        if (scope == null) {
            String identity = authType == AuthType.PAT ? Strings.nullToEmpty(token)
                    : Strings.nullToEmpty(clientId);
            scope = Hashing.sha256()
                    .hashString(host + "\u0000" + authType.name() + "\u0000" + identity,
                            StandardCharsets.UTF_8)
                    .toString();
            principalScope = scope;
        }
        return scope;
    }

    private static AuthType parseAuthType(String raw) {
        if (Strings.isNullOrEmpty(raw)) {
            return AuthType.PAT;
        }
        String normalized = raw.trim().toLowerCase(Locale.ROOT);
        switch (normalized) {
            case AUTH_TYPE_PAT:
                return AuthType.PAT;
            case AUTH_TYPE_OAUTH_M2M:
                return AuthType.OAUTH_M2M;
            default:
                throw new SemanticException("Invalid value for property %s: %s. Allowed values: %s, %s",
                        UNITY_AUTH_TYPE, raw, AUTH_TYPE_PAT, AUTH_TYPE_OAUTH_M2M);
        }
    }

    private void validateAuth() {
        if (authType == AuthType.PAT) {
            Preconditions.checkArgument(!Strings.isNullOrEmpty(token),
                    "%s must be set when %s=%s", UNITY_CATALOG_TOKEN, UNITY_AUTH_TYPE, AUTH_TYPE_PAT);
            Preconditions.checkArgument(Strings.isNullOrEmpty(clientId) && Strings.isNullOrEmpty(clientSecret),
                    "%s and %s must not be set when %s=%s",
                    UNITY_CLIENT_ID, UNITY_CLIENT_SECRET, UNITY_AUTH_TYPE, AUTH_TYPE_PAT);
        } else {
            Preconditions.checkArgument(!Strings.isNullOrEmpty(clientId),
                    "%s must be set when %s=%s", UNITY_CLIENT_ID, UNITY_AUTH_TYPE, AUTH_TYPE_OAUTH_M2M);
            Preconditions.checkArgument(!Strings.isNullOrEmpty(clientSecret),
                    "%s must be set when %s=%s", UNITY_CLIENT_SECRET, UNITY_AUTH_TYPE, AUTH_TYPE_OAUTH_M2M);
            Preconditions.checkArgument(Strings.isNullOrEmpty(token),
                    "%s must not be set when %s=%s", UNITY_CATALOG_TOKEN, UNITY_AUTH_TYPE, AUTH_TYPE_OAUTH_M2M);
        }
    }

    private static String stripTrailingSlash(String value) {
        String trimmed = value.trim();
        while (trimmed.endsWith("/")) {
            trimmed = trimmed.substring(0, trimmed.length() - 1);
        }
        return trimmed;
    }

    private static String emptyToNull(String value) {
        return Strings.isNullOrEmpty(value) ? null : value;
    }

    private static long parseLong(Map<String, String> properties, String key, long defaultValue) {
        String raw = properties.get(key);
        if (Strings.isNullOrEmpty(raw)) {
            return defaultValue;
        }
        try {
            return Long.parseLong(raw.trim());
        } catch (NumberFormatException e) {
            throw new SemanticException("Invalid numeric value for property %s: %s", key, raw);
        }
    }
}
