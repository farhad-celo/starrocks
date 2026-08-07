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

import com.starrocks.connector.metastore.MetastoreTable;
import io.unitycatalog.client.delta.model.DeltaLoadTableResponse;

/**
 * {@link MetastoreTable} carrying the {@code delta/v1} {@code loadTable} response it was built from,
 * so {@link UnityBackedDeltaMetastore#loadSnapshot} can reuse its metadata and inline commits
 * instead of issuing a second {@code loadTable} per load.
 */
public class UnityMetastoreTable extends MetastoreTable {
    private final DeltaLoadTableResponse loadTableResponse;

    public UnityMetastoreTable(String dbName, String tableName, String tableLocation, long createTime,
                               DeltaLoadTableResponse loadTableResponse) {
        super(dbName, tableName, tableLocation, createTime);
        this.loadTableResponse = loadTableResponse;
    }

    public DeltaLoadTableResponse getLoadTableResponse() {
        return loadTableResponse;
    }
}
