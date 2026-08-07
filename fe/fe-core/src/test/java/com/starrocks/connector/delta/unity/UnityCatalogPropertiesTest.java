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
        Assertions.assertNull(p.getClientId());
        Assertions.assertNull(p.getClientSecret());
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

    @Test
    public void testMissingHostFails() {
        Map<String, String> props = ImmutableMap.of(
                "unity.catalog.token", "dapiXYZ",
                "unity.catalog.name", "main");
        Assertions.assertThrows(IllegalArgumentException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testMissingTokenFails() {
        Map<String, String> props = ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.name", "main");
        Assertions.assertThrows(IllegalArgumentException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testMissingCatalogNameFails() {
        Map<String, String> props = ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiXYZ");
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
                .put("unity.catalog.cache.credentials.safety-margin-sec", "30")
                .build();
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertFalse(p.isCacheEnabled());
        Assertions.assertFalse(p.isDeltaCacheEnabled());
        Assertions.assertEquals(300L, p.getCacheTtlSec());
        Assertions.assertEquals(30L, p.getCredentialsSafetyMarginSec());
    }

    @Test
    public void testNegativeRequestTimeoutRejected() {
        Map<String, String> props = new HashMap<>();
        props.put("unity.catalog.host", "https://example.cloud.databricks.com");
        props.put("unity.catalog.token", "dapiXYZ");
        props.put("unity.catalog.name", "main");
        props.put("unity.catalog.request-timeout-ms", "-1");
        Assertions.assertThrows(IllegalArgumentException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testNegativeMaxRetriesRejected() {
        Map<String, String> props = new HashMap<>();
        props.put("unity.catalog.host", "https://example.cloud.databricks.com");
        props.put("unity.catalog.token", "dapiXYZ");
        props.put("unity.catalog.name", "main");
        props.put("unity.catalog.max-retries", "-1");
        Assertions.assertThrows(IllegalArgumentException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testMaxRetriesOverflowRejected() {
        Map<String, String> props = new HashMap<>();
        props.put("unity.catalog.host", "https://example.cloud.databricks.com");
        props.put("unity.catalog.token", "dapiXYZ");
        props.put("unity.catalog.name", "main");
        // Just past Integer.MAX_VALUE -- would silently overflow on the long->int cast.
        props.put("unity.catalog.max-retries", Long.toString(((long) Integer.MAX_VALUE) + 1L));
        Assertions.assertThrows(IllegalArgumentException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testNegativeTtlRejected() {
        Map<String, String> props = new HashMap<>();
        props.put("unity.catalog.host", "https://example.cloud.databricks.com");
        props.put("unity.catalog.token", "dapiXYZ");
        props.put("unity.catalog.name", "main");
        props.put("unity.catalog.cache.ttl-sec", "-1");
        Assertions.assertThrows(IllegalArgumentException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testNegativeSafetyMarginRejected() {
        Map<String, String> props = new HashMap<>();
        props.put("unity.catalog.host", "https://example.cloud.databricks.com");
        props.put("unity.catalog.token", "dapiXYZ");
        props.put("unity.catalog.name", "main");
        props.put("unity.catalog.cache.credentials.safety-margin-sec", "-5");
        Assertions.assertThrows(IllegalArgumentException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testAwsRegionOverrideDefaultsToNull() {
        Map<String, String> props = ImmutableMap.of(
                "unity.catalog.host", "https://example.cloud.databricks.com",
                "unity.catalog.token", "dapiXYZ",
                "unity.catalog.name", "main");
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertNull(p.getAwsRegionOverride(),
                "unset region must read as null so UnityMetastore falls back to inference");
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

    @Test
    public void testAwsRegionOverrideEmptyTreatedAsUnset() {
        Map<String, String> props = new HashMap<>();
        props.put("unity.catalog.host", "https://example.cloud.databricks.com");
        props.put("unity.catalog.token", "dapiXYZ");
        props.put("unity.catalog.name", "main");
        props.put("unity.catalog.aws.region", "");
        UnityCatalogProperties p = new UnityCatalogProperties(props);
        Assertions.assertNull(p.getAwsRegionOverride(),
                "blank region property must read as null instead of an empty string");
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
        Assertions.assertNull(p.getToken());
    }

    @Test
    public void testOAuthM2MMissingClientIdFails() {
        Map<String, String> props = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://example.cloud.databricks.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.auth.type", "oauth-m2m")
                .put("unity.catalog.client-secret", "shh-secret")
                .build();
        Assertions.assertThrows(IllegalArgumentException.class, () -> new UnityCatalogProperties(props));
    }

    @Test
    public void testOAuthM2MMissingClientSecretFails() {
        Map<String, String> props = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://example.cloud.databricks.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.auth.type", "oauth-m2m")
                .put("unity.catalog.client-id", "abc-client")
                .build();
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

    @Test
    public void testPrincipalScopeDiffersByToken() {
        Map<String, String> a = ImmutableMap.of(
                "unity.catalog.host", "https://ws.cloud.databricks.com",
                "unity.catalog.token", "dapiA",
                "unity.catalog.name", "main");
        Map<String, String> b = ImmutableMap.of(
                "unity.catalog.host", "https://ws.cloud.databricks.com",
                "unity.catalog.token", "dapiB",
                "unity.catalog.name", "main");
        Assertions.assertNotEquals(scopeOf(a), scopeOf(b));
    }

    @Test
    public void testPrincipalScopeDiffersByHost() {
        Map<String, String> a = ImmutableMap.of(
                "unity.catalog.host", "https://ws-a.cloud.databricks.com",
                "unity.catalog.token", "dapiSAME",
                "unity.catalog.name", "main");
        Map<String, String> b = ImmutableMap.of(
                "unity.catalog.host", "https://ws-b.cloud.databricks.com",
                "unity.catalog.token", "dapiSAME",
                "unity.catalog.name", "main");
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
    public void testInvalidAuthTypeRejected() {
        Map<String, String> props = ImmutableMap.<String, String>builder()
                .put("unity.catalog.host", "https://example.cloud.databricks.com")
                .put("unity.catalog.name", "main")
                .put("unity.catalog.token", "dapiXYZ")
                .put("unity.catalog.auth.type", "kerberos")
                .build();
        Assertions.assertThrows(SemanticException.class, () -> new UnityCatalogProperties(props));
    }
}
