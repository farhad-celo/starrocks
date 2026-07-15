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

package com.starrocks.server.celonis.explaininputcolumns;

import com.starrocks.catalog.Column;
import com.starrocks.catalog.Table;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

/**
 * Analyzer-only table synthesized for EXPLAIN INPUT COLUMNS EXTENSIONS that refer to a table not present in metadata.
 * Real tables keep their original metadata object; their extension columns are injected into QueryAnalyzer relation
 * fields instead.
 */
public final class VirtualExtensionTable extends Table {
    private final List<Column> baseSchema;

    public static Table virtualTable(String tableName, Collection<SchemaExtensionColumn> virtualColumns) {
        return new VirtualExtensionTable(tableName, virtualColumns);
    }

    public VirtualExtensionTable(String tableName, Collection<SchemaExtensionColumn> extensionColumns) {
        super(-1L, tableName, TableType.VIEW, toColumns(extensionColumns));
        this.baseSchema = List.of();
    }

    @Override
    public List<Column> getBaseSchema() {
        return baseSchema;
    }

    @Override
    public boolean isSupported() {
        return true;
    }

    private static List<Column> toColumns(Collection<SchemaExtensionColumn> extensionColumns) {
        List<Column> schema = new ArrayList<>(extensionColumns.size());
        for (SchemaExtensionColumn column : extensionColumns) {
            schema.add(new Column(column.name(), column.type(), true));
        }
        return schema;
    }
}
