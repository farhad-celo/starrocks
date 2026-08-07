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

import com.starrocks.credential.CloudConfiguration;
import com.starrocks.credential.CloudType;
import com.starrocks.credential.aws.AwsCloudConfiguration;
import io.unitycatalog.client.delta.model.DeltaCredentialsResponse;
import io.unitycatalog.client.delta.model.DeltaStorageCredentialConfig;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.MethodSource;

import java.util.stream.Stream;

public class UnityCatalogCredentialTranslatorTest {

    private static final String S3_LOCATION = "s3://bucket/path/table";
    private static final String ADLS_LOCATION = "abfss://container@account.dfs.core.windows.net/path/table";

    private static DeltaCredentialsResponse awsCreds(String prefix) {
        return UnityDeltaModelFixtures.credsResponse(
                UnityDeltaModelFixtures.credential(prefix, UnityDeltaModelFixtures.awsConfig(), 1_700_000_000_000L));
    }

    private static DeltaCredentialsResponse azureCreds(String prefix) {
        return UnityDeltaModelFixtures.credsResponse(UnityDeltaModelFixtures.credential(
                prefix, UnityDeltaModelFixtures.azureConfig("sv=2023-08-03&ss=b&sig=xxx"), 1L));
    }

    private static Stream<Arguments> toCloudConfigurationCases() {
        DeltaStorageCredentialConfig awsNoSession = new DeltaStorageCredentialConfig();
        awsNoSession.setS3AccessKeyId("ASIA_TEST");
        awsNoSession.setS3SecretAccessKey("secret");
        DeltaStorageCredentialConfig gcp = new DeltaStorageCredentialConfig();
        gcp.setGcsOauthToken("ya29.test");
        DeltaCredentialsResponse longestPrefix = UnityDeltaModelFixtures.credsResponse(
                UnityDeltaModelFixtures.credential("s3://bucket/", new DeltaStorageCredentialConfig(), 1L),
                UnityDeltaModelFixtures.credential("s3://bucket/path/", UnityDeltaModelFixtures.awsConfig(), 1L));
        return Stream.of(
                Arguments.of(awsCreds(S3_LOCATION), S3_LOCATION, CloudType.AWS),
                Arguments.of(azureCreds(ADLS_LOCATION), ADLS_LOCATION, CloudType.AZURE),
                Arguments.of(longestPrefix, S3_LOCATION, CloudType.AWS),
                Arguments.of(awsCreds(""), S3_LOCATION, CloudType.AWS),
                Arguments.of(awsCreds("s3://bucket/foo"), "s3://bucket/foo/table", CloudType.AWS),
                // A credential scoped to s3://bucket/foo must not leak to a sibling s3://bucket/foobar.
                Arguments.of(awsCreds("s3://bucket/foo"), "s3://bucket/foobar/table", CloudType.DEFAULT),
                Arguments.of(null, S3_LOCATION, CloudType.DEFAULT),
                Arguments.of(UnityDeltaModelFixtures.credsResponse(), S3_LOCATION, CloudType.DEFAULT),
                Arguments.of(UnityDeltaModelFixtures.credsResponse(
                        UnityDeltaModelFixtures.credential(S3_LOCATION, awsNoSession, 1L)), S3_LOCATION, CloudType.DEFAULT),
                Arguments.of(UnityDeltaModelFixtures.credsResponse(
                        UnityDeltaModelFixtures.credential("gs://bucket/path/table", gcp, 1L)),
                        "gs://bucket/path/table", CloudType.DEFAULT),
                Arguments.of(azureCreds(ADLS_LOCATION), null, CloudType.DEFAULT));
    }

    @ParameterizedTest
    @MethodSource("toCloudConfigurationCases")
    public void testResolvesExpectedCloudType(DeltaCredentialsResponse creds, String location, CloudType expected) {
        Assertions.assertEquals(expected,
                UnityCatalogCredentialTranslator.toCloudConfiguration(creds, location).getCloudType());
    }

    @Test
    public void testAwsSessionCredentialsPropagateRegionWhenProvided() {
        CloudConfiguration cc = UnityCatalogCredentialTranslator.toCloudConfiguration(awsCreds(S3_LOCATION),
                S3_LOCATION, "eu-central-1");
        Assertions.assertEquals(CloudType.AWS, cc.getCloudType());
        Assertions.assertInstanceOf(AwsCloudConfiguration.class, cc);
        Assertions.assertEquals("eu-central-1",
                ((AwsCloudConfiguration) cc).getAwsCloudCredential().getRegion());
    }

    @Test
    public void testAwsSessionCredentialsNullRegionLeavesRegionUnset() {
        CloudConfiguration cc = UnityCatalogCredentialTranslator.toCloudConfiguration(awsCreds(S3_LOCATION),
                S3_LOCATION, null);
        Assertions.assertEquals(CloudType.AWS, cc.getCloudType());
        Assertions.assertNotEquals("eu-central-1",
                ((AwsCloudConfiguration) cc).getAwsCloudCredential().getRegion());
    }

    private static Stream<Arguments> hasAwsCredentialCases() {
        return Stream.of(
                Arguments.of(awsCreds(S3_LOCATION), S3_LOCATION, true),
                Arguments.of(azureCreds(ADLS_LOCATION), ADLS_LOCATION, false),
                Arguments.of(UnityDeltaModelFixtures.credsResponse(), S3_LOCATION, false),
                Arguments.of(null, S3_LOCATION, false));
    }

    @ParameterizedTest
    @MethodSource("hasAwsCredentialCases")
    public void testHasAwsCredential(DeltaCredentialsResponse creds, String location, boolean expected) {
        Assertions.assertEquals(expected, UnityCatalogCredentialTranslator.hasAwsCredential(creds, location));
    }

    @ParameterizedTest
    @CsvSource(nullValues = "NULL", value = {
            "abfss://container@account.dfs.core.windows.net/path, account.dfs.core.windows.net",
            "abfs://container@account.dfs.core.windows.net/path,  account.dfs.core.windows.net",
            "s3://bucket/path,                                     NULL",
            "abfss://container@account.blob.core.windows.net/path, NULL",
            "'',                                                   NULL",
            "NULL,                                                 NULL",
    })
    public void testExtractAdlsEndpoint(String input, String expected) {
        Assertions.assertEquals(expected, UnityCatalogCredentialTranslator.extractAdlsEndpoint(input));
    }
}
