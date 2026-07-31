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

import com.starrocks.catalog.Type;

import java.util.Collection;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;

/**
 * Stores typed virtual extension columns by normalized table key.
 */
public final class CelostarSchemaExtension {
    private final Map<ExtensionTableKey, Map<String, SchemaExtensionColumn>> virtualColumns = new TreeMap<>(
            ExtensionTableKey.CASE_INSENSITIVE_ORDER);

    public void add(ExtensionTableKey tableKey, String columnName, Type type) {
        virtualColumns.computeIfAbsent(tableKey,
                        ignored -> new TreeMap<>(String.CASE_INSENSITIVE_ORDER))
                .put(columnName, new SchemaExtensionColumn(columnName, type));
    }

    public void addTable(ExtensionTableKey tableKey) {
        virtualColumns.computeIfAbsent(tableKey, ignored -> new TreeMap<>(String.CASE_INSENSITIVE_ORDER));
    }

    public boolean isEmpty() {
        return virtualColumns.isEmpty();
    }

    public boolean hasExtensionsForDatabase(String catalogName, String databaseName) {
        if (virtualColumns.isEmpty()) {
            return false;
        }
        String normalizedCatalog = normalizeName(catalogName);
        String normalizedDatabase = normalizeName(databaseName);
        for (ExtensionTableKey tableKey : virtualColumns.keySet()) {
            if (tableKey.catalog().equals(normalizedCatalog) && tableKey.database().equals(normalizedDatabase)) {
                return true;
            }
        }
        return false;
    }

    public boolean hasExtensionsFor(String catalogName, String databaseName, String tableName) {
        return virtualColumns.containsKey(new ExtensionTableKey(catalogName, databaseName, tableName));
    }

    public boolean hasVirtualColumnFor(String catalogName, String databaseName, String tableName, String columnName) {
        Map<String, SchemaExtensionColumn> columns =
                virtualColumns.get(new ExtensionTableKey(catalogName, databaseName, tableName));
        return columns != null && columns.containsKey(columnName);
    }

    public Collection<SchemaExtensionColumn> virtualColumnsFor(String catalogName, String databaseName,
                                                               String tableName) {
        ExtensionTableKey tableKey = new ExtensionTableKey(catalogName, databaseName, tableName);
        Map<String, SchemaExtensionColumn> columns = virtualColumns.get(tableKey);
        return columns == null ? List.of() : columns.values();
    }

    private static String normalizeName(String name) {
        if (name == null) {
            return "";
        }
        return name.toLowerCase(Locale.ROOT);
    }
}
