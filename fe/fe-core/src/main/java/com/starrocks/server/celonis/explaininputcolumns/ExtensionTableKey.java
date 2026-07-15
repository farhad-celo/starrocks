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

import java.util.Comparator;
import java.util.Locale;
import java.util.Objects;

/** Case-insensitive key for (catalog, database, table). */
public final class ExtensionTableKey {
    static final Comparator<ExtensionTableKey> CASE_INSENSITIVE_ORDER = Comparator
            .comparing(ExtensionTableKey::catalog)
            .thenComparing(ExtensionTableKey::database)
            .thenComparing(ExtensionTableKey::table);

    private final String catalog;
    private final String database;
    private final String table;

    public ExtensionTableKey(String catalog, String database, String table) {
        this.catalog = normalize(catalog);
        this.database = normalize(database);
        this.table = normalize(table);
    }

    public String catalog() {
        return catalog;
    }

    public String database() {
        return database;
    }

    public String table() {
        return table;
    }

    private static String normalize(String value) {
        return value == null ? "" : value.toLowerCase(Locale.ROOT);
    }

    @Override
    public boolean equals(Object otherObject) {
        if (this == otherObject) {
            return true;
        }
        if (otherObject == null || getClass() != otherObject.getClass()) {
            return false;
        }
        ExtensionTableKey otherKey = (ExtensionTableKey) otherObject;
        return Objects.equals(catalog, otherKey.catalog) && Objects.equals(database, otherKey.database)
                && Objects.equals(table, otherKey.table);
    }

    @Override
    public int hashCode() {
        return Objects.hash(catalog, database, table);
    }
}
