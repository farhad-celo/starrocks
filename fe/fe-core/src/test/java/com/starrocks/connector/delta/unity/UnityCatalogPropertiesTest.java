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

import com.google.common.collect.ImmutableMap;
import com.starrocks.sql.analyzer.SemanticException;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.ValueSource;

import java.util.HashMap;
import java.util.Map;

public class UnityCatalogPropertiesTest {

    @Test
    public void testBasicProperties() {
        Map<String, String> props = ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com/",
                "unity.catalog.token", "dapiXYZ",
                "unity.catalog.name", "main");
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertEquals("https://example.cloud.databricks.com", p.getHost());
        Assertions.assertEquals("dapiXYZ", p.getToken());
        Assertions.assertEquals("main", p.getUcCatalogName());
        Assertions.assertEquals(UnityCatalogProperties.AuthType.PAT, p.getAuthType());
        Assertions.assertEquals(UnityCatalogProperties.Flavor.DATABRICKS, p.getFlavor());
        Assertions.assertNull(p.getClientId());
        Assertions.assertNull(p.getClientSecret());
        Assertions.assertNull(p.getOAuthTokenUrl());
        Assertions.assertNull(p.getOAuthScope());
        Assertions.assertTrue(p.isVendedCredentialsEnabled(), "vended creds should default to true");
        Assertions.assertFalse(p.isSpoofUserAgent(), "Unity client user-agent spoofing should default to false");
        Assertions.assertEquals(30_000L, p.getRequestTimeoutMs());
        Assertions.assertEquals(3, p.getMaxRetries());
    }

    @Test
    public void testVendedCredentialsExplicitlyDisabled() {
        Map<String, String> props = ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiXYZ",
                "unity.catalog.name", "main",
                "unity.catalog.vended-credentials-enabled", "false");
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertFalse(p.isVendedCredentialsEnabled());
    }

    @Test
    public void testSpoofUserAgentExplicitlyEnabled() {
        Map<String, String> props = ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiXYZ",
                "unity.catalog.name", "main",
                "unity.catalog.spoof-user-agent", "true");
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertTrue(p.isSpoofUserAgent());
    }

    @Test
    public void testOssFlavorOptIn() {
        Map<String, String> props = ImmutableMap.of(
                "unity.catalog.host", "https://oss-uc.example.com",
                "unity.catalog.token", "uc-token",
                "unity.catalog.name", "main",
                "unity.catalog.flavor", "OSS");
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertEquals(UnityCatalogProperties.Flavor.OSS, p.getFlavor());
    }

    @Test
    public void testFabricFlavorWithCredentialVendingDisabled() {
        Map<String, String> props = ImmutableMap.of(
                "unity.catalog.host", "https://onelake.table.fabric.microsoft.com/delta/workspace/item",
                "unity.catalog.token", "fabric-token",
                "unity.catalog.name", "item",
                "unity.catalog.flavor", "FABRIC",
                "unity.catalog.vended-credentials-enabled", "false");
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertEquals(UnityCatalogProperties.Flavor.FABRIC, p.getFlavor());
        Assertions.assertFalse(p.isVendedCredentialsEnabled());
    }

    @Test
    public void testFabricFlavorRejectsCredentialVending() {
        Map<String, String> props = ImmutableMap.of(
                "unity.catalog.host", "https://onelake.table.fabric.microsoft.com/delta/workspace/item",
                "unity.catalog.token", "fabric-token",
                "unity.catalog.name", "item",
                "unity.catalog.flavor", "fabric");
        IllegalArgumentException ex = Assertions.assertThrows(IllegalArgumentException.class,
                () -> new UnityCatalogProperties(props));
        Assertions.assertTrue(ex.getMessage().contains("unity.catalog.vended-credentials-enabled"));
    }

    @Test
    public void testCustomTimeoutAndRetries() {
        Map<String, String> props = ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiXYZ",
                "unity.catalog.name", "main",
                "unity.catalog.request-timeout-ms", "9000",
                "unity.catalog.max-retries", "5");
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertEquals(9_000L, p.getRequestTimeoutMs());
        Assertions.assertEquals(5, p.getMaxRetries());
    }

    @ParameterizedTest(name = "missing {0}")
    @ValueSource(strings = {
            "unity.catalog.host",
            "unity.catalog.token",
            "unity.catalog.name"
    })
    public void testMissingRequiredPatPropertyFails(String property) {
        Map<String, String> props = validPatProperties();
        props.remove(property);
        Assertions.assertThrows(IllegalArgumentException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testInvalidNumericFails() {
        Map<String, String> props = new HashMap<>();
        props.put("unity.catalog.host", "https://example.cloud.databricks.com");
        props.put("unity.catalog.token", "dapiXYZ");
        props.put("unity.catalog.name", "main");
        props.put("unity.catalog.request-timeout-ms", "not-a-number");
        Assertions.assertThrows(SemanticException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testCachingDefaults() {
        Map<String, String> props = ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiXYZ",
                "unity.catalog.name", "main");
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertTrue(p.isCacheEnabled(), "cache should default to enabled");
        Assertions.assertTrue(p.isDeltaCacheEnabled(), "delta cache should default to enabled");
        Assertions.assertEquals(60L, p.getCacheTtlSec());
        Assertions.assertEquals(1200L, p.getCredentialsCacheTtlSec());
        Assertions.assertEquals(1200L, p.getCredentialsSafetyMarginSec());
    }

    @Test
    public void testCachingOverrides() {
        Map<String, String> props = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://example.cloud.databricks.com")
                .put("unity.catalog.token", "dapiXYZ")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.cache.enabled", "false")
                .put("unity.catalog.delta-cache.enabled", "false")
                .put("unity.catalog.cache.ttl-sec", "300")
                .put("unity.catalog.cache.credentials.ttl-sec", "1800")
                .put("unity.catalog.cache.credentials.safety-margin-sec", "30")
                .build();
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertFalse(p.isCacheEnabled());
        Assertions.assertFalse(p.isDeltaCacheEnabled());
        Assertions.assertEquals(300L, p.getCacheTtlSec());
        Assertions.assertEquals(1800L, p.getCredentialsCacheTtlSec());
        Assertions.assertEquals(30L, p.getCredentialsSafetyMarginSec());
    }

    @ParameterizedTest(name = "{0}={1}")
    @CsvSource({
            "unity.catalog.request-timeout-ms,                    -1",
            "unity.catalog.max-retries,                           -1",
            "unity.catalog.max-retries,                           2147483648",
            "unity.catalog.cache.ttl-sec,                         -1",
            "unity.catalog.cache.credentials.safety-margin-sec,  -5",
            "unity.catalog.cache.credentials.ttl-sec,             -1"
    })
    public void testOutOfRangeNumericRejected(String property, String value) {
        Map<String, String> props = validPatProperties();
        props.put(property, value);
        Assertions.assertThrows(IllegalArgumentException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testAwsRegionOverrideTrimmed() {
        Map<String, String> props = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://example.cloud.databricks.com")
                .put("unity.catalog.token", "dapiXYZ")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.aws.region", "  eu-central-1  ")
                .build();
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertEquals("eu-central-1", p.getAwsRegionOverride());
    }

    @ParameterizedTest(name = "region=''{0}''")
    @CsvSource(nullValues = "NULL", value = {"NULL", "''", "' '"})
    public void testAwsRegionOverrideUnsetOrBlankDefaultsToNull(String region) {
        Map<String, String> props = validPatProperties();
        if (region != null) {
            props.put("unity.catalog.aws.region", region);
        }
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertNull(p.getAwsRegionOverride(),
                "unset or blank region must read as null so UnityMetastore falls back to inference");
    }

    @Test
    public void testOAuthM2MAuth() {
        Map<String, String> props = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://example.cloud.databricks.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.auth.type", "oauth-m2m")
                .put("unity.catalog.client-id", "abc-client")
                .put("unity.catalog.client-secret", "shh-secret")
                .build();
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertEquals(UnityCatalogProperties.AuthType.OAUTH_M2M, p.getAuthType());
        Assertions.assertEquals("abc-client", p.getClientId());
        Assertions.assertEquals("shh-secret", p.getClientSecret());
        Assertions.assertEquals("https://example.cloud.databricks.com/oidc/v1/token", p.getOAuthTokenUrl());
        Assertions.assertEquals(UnityCatalogProperties.OAUTH_SCOPE_ALL_APIS, p.getOAuthScope());
        Assertions.assertNull(p.getToken());
    }

    @Test
    public void testDatabricksOAuthM2MBlankScopeDefaultsToAllApis() {
        Map<String, String> props = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://example.cloud.databricks.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.auth.type", "oauth-m2m")
                .put("unity.catalog.client-id", "abc-client")
                .put("unity.catalog.client-secret", "shh-secret")
                .put("unity.catalog.oauth-scope", " ")
                .build();
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertEquals(UnityCatalogProperties.OAUTH_SCOPE_ALL_APIS, p.getOAuthScope());
    }

    @Test
    public void testOAuthM2MCustomTokenUrl() {
        Map<String, String> props = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://oss-uc.example.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.flavor", "oss")
                .put("unity.catalog.auth.type", "oauth-m2m")
                .put("unity.catalog.client-id", "abc-client")
                .put("unity.catalog.client-secret", "shh-secret")
                .put("unity.catalog.oauth-token-url", " https://auth.example.com/oauth/token ")
                .put("unity.catalog.oauth-scope", "all-apis")
                .build();
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertEquals("https://auth.example.com/oauth/token", p.getOAuthTokenUrl());
        Assertions.assertEquals(UnityCatalogProperties.OAUTH_SCOPE_ALL_APIS, p.getOAuthScope());
    }

    @Test
    public void testOAuthM2MCustomScope() {
        Map<String, String> props = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://oss-uc.example.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.flavor", "oss")
                .put("unity.catalog.auth.type", "oauth-m2m")
                .put("unity.catalog.client-id", "abc-client")
                .put("unity.catalog.client-secret", "shh-secret")
                .put("unity.catalog.oauth-token-url", "https://auth.example.com/oauth/token")
                .put("unity.catalog.oauth-scope", " uc.read uc.write ")
                .build();
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertEquals("uc.read uc.write", p.getOAuthScope());
    }

    @ParameterizedTest(name = "scope=''{0}''")
    @CsvSource(nullValues = "NULL", value = {"NULL", "' '"})
    public void testOssOAuthM2MRequiresNonEmptyScope(String scope) {
        Map<String, String> props = validOAuthProperties();
        props.put("unity.catalog.flavor", "oss");
        props.put("unity.catalog.oauth-token-url", "https://auth.example.com/oauth/token");
        if (scope != null) {
            props.put("unity.catalog.oauth-scope", scope);
        }
        SemanticException ex = Assertions.assertThrows(SemanticException.class,
                () -> new UnityCatalogProperties(props));
        Assertions.assertTrue(ex.getMessage().contains("unity.catalog.oauth-scope"));
    }

    @Test
    public void testFabricOAuthM2MDefaultsToAzureStorageScope() {
        Map<String, String> props = validOAuthProperties();
        props.put("unity.catalog.flavor", "fabric");
        props.put("unity.catalog.oauth-token-url", "https://login.microsoftonline.com/tenant/oauth2/v2.0/token");
        props.put("unity.catalog.vended-credentials-enabled", "false");

        UnityCatalogProperties properties = new UnityCatalogProperties(props);
        Assertions.assertEquals(UnityCatalogProperties.OAUTH_SCOPE_AZURE_STORAGE, properties.getOAuthScope());
    }

    @Test
    public void testOssOAuthM2MRequiresTokenUrl() {
        Map<String, String> props = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://oss-uc.example.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.flavor", "oss")
                .put("unity.catalog.auth.type", "oauth-m2m")
                .put("unity.catalog.client-id", "abc-client")
                .put("unity.catalog.client-secret", "shh-secret")
                .build();
        SemanticException ex = Assertions.assertThrows(SemanticException.class,
                () -> new UnityCatalogProperties(props));
        Assertions.assertTrue(ex.getMessage().contains("unity.catalog.oauth-token-url"));
    }

    @ParameterizedTest(name = "missing {0}")
    @ValueSource(strings = {
            "unity.catalog.client-id",
            "unity.catalog.client-secret"
    })
    public void testOAuthM2MMissingCredentialFails(String property) {
        Map<String, String> props = validOAuthProperties();
        props.remove(property);
        Assertions.assertThrows(IllegalArgumentException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testOAuthM2MWithTokenFails() {
        Map<String, String> props = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://example.cloud.databricks.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.auth.type", "oauth-m2m")
                .put("unity.catalog.client-id", "abc-client")
                .put("unity.catalog.client-secret", "shh-secret")
                .put("unity.catalog.token", "dapiXYZ")
                .build();
        Assertions.assertThrows(IllegalArgumentException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testPatWithClientIdFails() {
        Map<String, String> props = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://example.cloud.databricks.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.token", "dapiXYZ")
                .put("unity.catalog.client-id", "abc-client")
                .build();
        Assertions.assertThrows(IllegalArgumentException.class, () -> new UnityCatalogProperties(props));
    }

    private static String scopeOf(Map<String, String> props) {
        return new UnityCatalogProperties(props).getPrincipalScope();
    }

    @Test
    public void testPrincipalScopeStableAcrossCatalogNamesForSamePrincipal() {
        Map<String, String> a = ImmutableMap.of(
                "unity.catalog.host", "https://ws.cloud.databricks.com",
                "unity.catalog.token", "dapiSAME",
                "unity.catalog.name", "main");
        Map<String, String> b = ImmutableMap.of(
                "unity.catalog.host", "https://ws.cloud.databricks.com",
                "unity.catalog.token", "dapiSAME",
                "unity.catalog.name", "other");
        Assertions.assertEquals(scopeOf(a), scopeOf(b),
                "same principal at same host must share a scope regardless of UC catalog name");
    }

    @ParameterizedTest(name = "different {0}")
    @CsvSource({
            "unity.catalog.token, dapiA, dapiB",
            "unity.catalog.host,  https://ws-a.cloud.databricks.com, https://ws-b.cloud.databricks.com"
    })
    public void testPrincipalScopeDiffersByPatProperty(String property, String first, String second) {
        Map<String, String> a = validPatProperties();
        Map<String, String> b = validPatProperties();
        a.put(property, first);
        b.put(property, second);
        Assertions.assertNotEquals(scopeOf(a), scopeOf(b));
    }

    @Test
    public void testPrincipalScopeDiffersBetweenPatAndOAuth() {
        Map<String, String> pat = ImmutableMap.of(
                "unity.catalog.host", "https://ws.cloud.databricks.com",
                "unity.catalog.token", "shared-identity",
                "unity.catalog.name", "main");
        Map<String, String> oauth = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://ws.cloud.databricks.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.auth.type", "oauth-m2m")
                .put("unity.catalog.client-id", "shared-identity")
                .put("unity.catalog.client-secret", "secret")
                .build();
        Assertions.assertNotEquals(scopeOf(pat), scopeOf(oauth),
                "auth type is part of the fingerprint so a PAT and an OAuth id cannot collide");
    }

    @Test
    public void testPrincipalScopeIgnoresOAuthSecretRotation() {
        Map<String, String> a = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://ws.cloud.databricks.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.auth.type", "oauth-m2m")
                .put("unity.catalog.client-id", "abc-client")
                .put("unity.catalog.client-secret", "secret-v1")
                .build();
        Map<String, String> b = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://ws.cloud.databricks.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.auth.type", "oauth-m2m")
                .put("unity.catalog.client-id", "abc-client")
                .put("unity.catalog.client-secret", "secret-v2")
                .build();
        Assertions.assertEquals(scopeOf(a), scopeOf(b),
                "the OAuth client id identifies the principal; a rotated secret is the same principal");
    }

    @ParameterizedTest(name = "different {0}")
    @CsvSource({
            "unity.catalog.oauth-token-url, https://auth-a.example.com/oauth/token, "
                    + "https://auth-b.example.com/oauth/token",
            "unity.catalog.oauth-scope,     scope-a, scope-b"
    })
    public void testPrincipalScopeDiffersByOAuthProperty(String property, String first, String second) {
        Map<String, String> a = validOAuthProperties();
        Map<String, String> b = validOAuthProperties();
        a.put(property, first);
        b.put(property, second);
        Assertions.assertNotEquals(scopeOf(a), scopeOf(b));
    }

    @Test
    public void testPrincipalScopeDoesNotEmbedSecrets() {
        String scope = scopeOf(ImmutableMap.of(
                "unity.catalog.host", "https://ws.cloud.databricks.com",
                "unity.catalog.token", "dapiSUPERSECRET",
                "unity.catalog.name", "main"));
        Assertions.assertFalse(scope.contains("dapiSUPERSECRET"),
                "the scope must hash secret material rather than carry it in the clear");
    }

    @Test
    public void testInvalidFlavorRejected() {
        Map<String, String> props = new HashMap<>();
        props.put("unity.catalog.host", "https://example.cloud.databricks.com");
        props.put("unity.catalog.token", "dapiXYZ");
        props.put("unity.catalog.name", "main");
        props.put("unity.catalog.flavor", "rest");
        Assertions.assertThrows(SemanticException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testInvalidAuthTypeRejected() {
        Map<String, String> props = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://example.cloud.databricks.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.token", "dapiXYZ")
                .put("unity.catalog.auth.type", "kerberos")
                .build();
        Assertions.assertThrows(SemanticException.class, () -> new UnityCatalogProperties(props));
    }

    private static Map<String, String> validPatProperties() {
        Map<String, String> props = new HashMap<>();
        props.put("unity.catalog.host", "https://example.cloud.databricks.com");
        props.put("unity.catalog.token", "dapiXYZ");
        props.put("unity.catalog.name", "main");
        return props;
    }

    private static Map<String, String> validOAuthProperties() {
        Map<String, String> props = new HashMap<>();
        props.put("unity.catalog.host", "https://example.cloud.databricks.com");
        props.put("unity.catalog.name", "main");
        props.put("unity.catalog.auth.type", "oauth-m2m");
        props.put("unity.catalog.client-id", "abc-client");
        props.put("unity.catalog.client-secret", "shh-secret");
        return props;
    }
}
