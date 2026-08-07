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

import io.unitycatalog.client.delta.model.DeltaCommit;
import io.unitycatalog.client.delta.model.DeltaCredentialsResponse;
import io.unitycatalog.client.delta.model.DeltaLoadTableResponse;
import io.unitycatalog.client.delta.model.DeltaStorageCredential;
import io.unitycatalog.client.delta.model.DeltaStorageCredentialConfig;
import io.unitycatalog.client.delta.model.DeltaTableMetadata;

import java.util.Arrays;
import java.util.Map;
import java.util.UUID;

/**
 * Shared builders for the Unity Catalog {@code delta/v1} model DTOs so the Unity test suite does not
 * repeat the fluent construction boilerplate in every test.
 */
final class UnityDeltaModelFixtures {
    static final String DEFAULT_LOCATION = "s3://bucket/prefix/orders";
    static final UUID TABLE_UUID = UUID.fromString("11111111-1111-1111-1111-111111111111");

    private UnityDeltaModelFixtures() {
    }

    static DeltaTableMetadata metadata(String location, Long createdTimeMs, Map<String, String> properties) {
        DeltaTableMetadata metadata = new DeltaTableMetadata();
        metadata.setTableUuid(TABLE_UUID);
        metadata.setLocation(location);
        metadata.setCreatedTime(createdTimeMs);
        if (properties != null) {
            metadata.setProperties(properties);
        }
        return metadata;
    }

    static DeltaLoadTableResponse loadResponse(DeltaTableMetadata metadata) {
        DeltaLoadTableResponse response = new DeltaLoadTableResponse();
        response.setMetadata(metadata);
        return response;
    }

    static DeltaLoadTableResponse loadResponse(String location, Long createdTimeMs, Map<String, String> properties) {
        return loadResponse(metadata(location, createdTimeMs, properties));
    }

    // Delta Kernel treats a table as catalog-managed when this feature is manually enabled.
    static final Map<String, String> CATALOG_MANAGED_PROPERTIES = Map.of("delta.feature.catalogManaged", "supported");

    // Staged-commit basename: "<20-digit version>.<uuid>.json" under _staged_commits/, as Kernel expects.
    static String stagedCommitFileName(long version) {
        return String.format("%020d.%08x-0000-0000-0000-000000000000.json", version, version);
    }

    static DeltaCommit commit(long version, long fileSize, long fileModificationTimestamp) {
        DeltaCommit commit = new DeltaCommit();
        commit.setVersion(version);
        commit.setFileName(stagedCommitFileName(version));
        commit.setFileSize(fileSize);
        commit.setFileModificationTimestamp(fileModificationTimestamp);
        return commit;
    }

    static DeltaLoadTableResponse catalogManagedLoadResponse(String location, Long latestTableVersion,
                                                             DeltaCommit... commits) {
        DeltaLoadTableResponse response = loadResponse(metadata(location, 1_700_000_000_000L, CATALOG_MANAGED_PROPERTIES));
        response.setLatestTableVersion(latestTableVersion);
        response.setCommits(Arrays.asList(commits));
        return response;
    }

    static DeltaStorageCredentialConfig awsConfig() {
        DeltaStorageCredentialConfig config = new DeltaStorageCredentialConfig();
        config.setS3AccessKeyId("AKIA_TEST");
        config.setS3SecretAccessKey("secret");
        config.setS3SessionToken("session");
        return config;
    }

    static DeltaStorageCredentialConfig azureConfig(String sasToken) {
        DeltaStorageCredentialConfig config = new DeltaStorageCredentialConfig();
        config.setAzureSasToken(sasToken);
        return config;
    }

    static DeltaStorageCredential credential(String prefix, DeltaStorageCredentialConfig config, Long expirationMs) {
        DeltaStorageCredential credential = new DeltaStorageCredential();
        credential.setPrefix(prefix);
        credential.setConfig(config);
        credential.setExpirationTimeMs(expirationMs);
        return credential;
    }

    static DeltaCredentialsResponse credsResponse(DeltaStorageCredential... credentials) {
        DeltaCredentialsResponse response = new DeltaCredentialsResponse();
        response.setStorageCredentials(Arrays.asList(credentials));
        return response;
    }

    static DeltaCredentialsResponse awsCredsResponse(String prefix, Long expirationMs) {
        return credsResponse(credential(prefix, awsConfig(), expirationMs));
    }

    static DeltaCredentialsResponse awsCredsResponse(Long expirationMs) {
        return awsCredsResponse(DEFAULT_LOCATION, expirationMs);
    }
}
