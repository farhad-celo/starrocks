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

package com.starrocks.sql.ast.celonis.remaplogical;

import com.starrocks.analysis.TableName;
import com.starrocks.server.celonis.explaininputcolumns.ExtensionTableKey;

import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;

public class RemappingSet {
    private final Map<ExtensionTableKey, String> tableRemappings = new TreeMap<>(
            ExtensionTableKey.CASE_INSENSITIVE_ORDER);
    private final Map<ColumnKey, String> columnRemappings = new TreeMap<>();

    public void addTableMapping(TableName sourceTable, String targetTable) {
        tableRemappings.put(tableKey(sourceTable), targetTable);
    }

    public boolean hasTableMapping(TableName sourceTable) {
        return tableRemappings.containsKey(tableKey(sourceTable));
    }

    public TableName remapTableName(TableName sourceTable) {
        String targetTable = tableRemappings.get(tableKey(sourceTable));
        if (targetTable == null) {
            return copy(sourceTable);
        }
        // Mapping targets are rendered identifiers, such as wrapper CTE names, not tables in the source db/catalog.
        return new TableName(null, null, targetTable, sourceTable.getPos());
    }

    public void addColumnMapping(TableName sourceTable, String sourceColumn, String targetColumn) {
        columnRemappings.put(new ColumnKey(sourceTable, sourceColumn), targetColumn);
    }

    public boolean hasColumnMapping(TableName sourceTable, String sourceColumn) {
        return columnRemappings.containsKey(new ColumnKey(sourceTable, sourceColumn));
    }

    public String remapColumnName(TableName sourceTable, String sourceColumn) {
        String targetColumn = columnRemappings.get(new ColumnKey(sourceTable, sourceColumn));
        return targetColumn == null ? sourceColumn : targetColumn;
    }

    private static TableName copy(TableName tableName) {
        return new TableName(tableName.getCatalog(), tableName.getDb(), tableName.getTbl(), tableName.getPos());
    }

    private static ExtensionTableKey tableKey(TableName tableName) {
        return new ExtensionTableKey(tableName.getCatalog(), tableName.getDb(), tableName.getTbl());
    }

    private record ColumnKey(ExtensionTableKey tableKey, String column) implements Comparable<ColumnKey> {
        private ColumnKey(TableName tableName, String column) {
            this(RemappingSet.tableKey(tableName), column);
        }

        private ColumnKey {
            column = column == null ? "" : column.toLowerCase(Locale.ROOT);
        }

        @Override
        public int compareTo(ColumnKey other) {
            int tableCompare = ExtensionTableKey.CASE_INSENSITIVE_ORDER.compare(tableKey, other.tableKey);
            if (tableCompare != 0) {
                return tableCompare;
            }
            return column.compareTo(other.column);
        }
    }
}
